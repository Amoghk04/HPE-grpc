#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <random>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <filesystem>
#include <cmath>
#include <unistd.h>
#include <sys/resource.h>

#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include "service.grpc.pb.h"
#include <nlohmann/json.hpp>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using myservice::Greeter;
using myservice::HelloRequest;
using myservice::HelloReply;
using myservice::EmptyRequest;
using myservice::StatusResponse;
using myservice::ConfigInfo;
using myservice::NetworkConfig;
using myservice::IPConfigRequest;
using myservice::IPConfigResponse;
using myservice::FileService;
using myservice::FileUploadRequest;
using myservice::FileUploadResponse;
using myservice::FileDownloadRequest;
using myservice::FileDownloadResponse;

using json = nlohmann::json;
namespace fs = std::filesystem;

// Helper function to read file contents
std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    return std::string(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
}

// Generate random data for testing
std::string generate_random_data(size_t size) {
    std::string data;
    data.reserve(size);
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);
    
    for (size_t i = 0; i < size; ++i) {
        data.push_back(charset[dist(gen)]);
    }
    
    return data;
}

// Class to store latency measurements and calculate statistics
class LatencyStats {
private:
    std::vector<double> latencies_ms;
    mutable std::mutex mutex;

public:
    void add_sample(std::chrono::nanoseconds duration) {
        double ms = duration.count() / 1000000.0;
        std::lock_guard<std::mutex> lock(mutex);
        latencies_ms.push_back(ms);
    }
    
    double min() const {
        std::lock_guard<std::mutex> lock(mutex);
        if (latencies_ms.empty()) return 0;
        return *std::min_element(latencies_ms.begin(), latencies_ms.end());
    }
    
    double max() const {
        std::lock_guard<std::mutex> lock(mutex);
        if (latencies_ms.empty()) return 0;
        return *std::max_element(latencies_ms.begin(), latencies_ms.end());
    }
    
    double avg() const {
        std::lock_guard<std::mutex> lock(mutex);
        if (latencies_ms.empty()) return 0;
        return std::accumulate(latencies_ms.begin(), latencies_ms.end(), 0.0) / latencies_ms.size();
    }
    
    double percentile(double p) const {
        std::lock_guard<std::mutex> lock(mutex);
        if (latencies_ms.empty()) return 0;
        
        std::vector<double> sorted = latencies_ms;
        std::sort(sorted.begin(), sorted.end());
        
        size_t idx = static_cast<size_t>(p * sorted.size());
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    }
    
    double median() const { return percentile(0.5); }
    double p90() const { return percentile(0.9); }
    double p95() const { return percentile(0.95); }
    double p99() const { return percentile(0.99); }
    
    double jitter() const {
        std::lock_guard<std::mutex> lock(mutex);
        if (latencies_ms.size() <= 1) return 0;
        
        double mean = avg();
        double sum_squared_diff = 0.0;
        
        for (double latency : latencies_ms) {
            double diff = latency - mean;
            sum_squared_diff += diff * diff;
        }
        
        return std::sqrt(sum_squared_diff / latencies_ms.size());
    }
    
    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex);
        return latencies_ms.size();
    }
    
    // Export statistics to JSON
    json to_json() const {
        json stats;
        stats["count"] = count();
        stats["min_ms"] = min();
        stats["max_ms"] = max();
        stats["avg_ms"] = avg();
        stats["median_ms"] = median();
        stats["p90_ms"] = p90();
        stats["p95_ms"] = p95();
        stats["p99_ms"] = p99();
        stats["jitter_ms"] = jitter();
        return stats;
    }
};

// Performance metrics collector
class BenchmarkMetrics {
public:
    LatencyStats hi_latency;
    LatencyStats say_hello_latency;
    LatencyStats say_hello_again_latency;
    LatencyStats status_latency;
    LatencyStats network_config_latency;
    LatencyStats file_upload_latency;
    LatencyStats file_download_latency;
    
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> successful_requests{0};
    std::atomic<uint64_t> failed_requests{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint32_t> active_connections{0};
    std::atomic<uint32_t> peak_connections{0};
    
    std::map<grpc::StatusCode, uint64_t> error_codes;
    mutable std::mutex error_mutex;
    
    // Resource usage tracking
    struct ResourceUsage {
        double cpu_usage_percent = 0;
        size_t memory_usage_bytes = 0;
    };
    
    std::vector<ResourceUsage> resource_samples;
    std::mutex resource_mutex;
    
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    
    void start_benchmark() {
        start_time = std::chrono::system_clock::now();
    }
    
    void end_benchmark() {
        end_time = std::chrono::system_clock::now();
    }
    
    void increment_active_connections() {
        uint32_t current = ++active_connections;
        uint32_t peak = peak_connections.load();
        if (current > peak) {
            peak_connections.store(current);
        }
    }
    
    void decrement_active_connections() {
        --active_connections;
    }
    
    void add_error(grpc::StatusCode code) {
        std::lock_guard<std::mutex> lock(error_mutex);
        error_codes[code]++;
    }
    
    double duration_seconds() const {
        auto duration = end_time - start_time;
        return std::chrono::duration<double>(duration).count();
    }
    
    double requests_per_second() const {
        return total_requests.load() / duration_seconds();
    }
    
    double error_rate() const {
        uint64_t total = total_requests.load();
        if (total == 0) return 0.0;
        return static_cast<double>(failed_requests.load()) / total;
    }
    
    double bandwidth_mbps() const {
        double duration = duration_seconds();
        if (duration <= 0) return 0.0;
        // Convert bytes to bits and seconds to megabits per second
        return ((bytes_sent.load() + bytes_received.load()) * 8.0) / (duration * 1000000);
    }
    
    void record_resource_usage() {
        // Get CPU and memory usage of this process
        ResourceUsage usage;
        
        // CPU usage is complex to measure accurately, this is simplified
        // In a real benchmark you'd use platform-specific API
        usage.cpu_usage_percent = 0; // Placeholder
        
        // Memory usage via getrusage
        struct rusage r_usage;
        getrusage(RUSAGE_SELF, &r_usage);
        usage.memory_usage_bytes = r_usage.ru_maxrss * 1024; // KB to bytes
        
        std::lock_guard<std::mutex> lock(resource_mutex);
        resource_samples.push_back(usage);
    }
    
    // Export all metrics to JSON
    json to_json() const {
        json report;
        
        // Basic info
        report["timestamp"] = std::chrono::system_clock::to_time_t(end_time);
        report["duration_seconds"] = duration_seconds();
        report["total_requests"] = total_requests.load();
        report["successful_requests"] = successful_requests.load();
        report["failed_requests"] = failed_requests.load();
        report["error_rate"] = error_rate();
        report["requests_per_second"] = requests_per_second();
        
        // Throughput
        report["bytes_sent"] = bytes_sent.load();
        report["bytes_received"] = bytes_received.load();
        report["bandwidth_mbps"] = bandwidth_mbps();
        
        // Concurrency
        report["peak_connections"] = peak_connections.load();
        
        // Latency metrics
        report["latency"]["hi"] = hi_latency.to_json();
        report["latency"]["say_hello"] = say_hello_latency.to_json();
        report["latency"]["say_hello_again"] = say_hello_again_latency.to_json();
        report["latency"]["status"] = status_latency.to_json();
        report["latency"]["network_config"] = network_config_latency.to_json();
        report["latency"]["file_upload"] = file_upload_latency.to_json();
        report["latency"]["file_download"] = file_download_latency.to_json();
        
        // Error distribution
        std::lock_guard<std::mutex> lock(error_mutex);
        for (const auto& [code, count] : error_codes) {
            report["errors"][std::to_string(static_cast<int>(code))] = count;
        }
        
        return report;
    }
};

// Benchmark configuration
struct BenchmarkConfig {
    std::string server_address = "localhost:50051";
    bool use_tls = true;
    std::string ca_cert = "../../certs/ca.crt";
    std::string client_cert = "../../certs/client.crt";
    std::string client_key = "../../certs/client.key";
    
    size_t num_threads = 10;
    int duration_seconds = 30;
    int warmup_seconds = 5;
    
    size_t greeting_size = 100;
    size_t file_size_kb = 1024;  // 1MB
    
    bool verbose = false;
    std::string output_file = "benchmark_results.json";
};

// Main benchmark class
class Benchmarker {
private:
    BenchmarkConfig config;
    BenchmarkMetrics metrics;
    std::shared_ptr<Channel> channel;
    std::string temp_dir;
    std::atomic<bool> running{false};
    std::vector<std::thread> threads;
    std::mutex cout_mutex;
    
    // Create secure channel with TLS
    std::shared_ptr<Channel> create_secure_channel() {
        grpc::SslCredentialsOptions ssl_opts;
        try {
            ssl_opts.pem_root_certs = read_file(config.ca_cert);
            ssl_opts.pem_private_key = read_file(config.client_key);
            ssl_opts.pem_cert_chain = read_file(config.client_cert);
        } catch (const std::exception& e) {
            std::cerr << "Failed to read certificates: " << e.what() << std::endl;
            throw;
        }
        
        auto credentials = grpc::SslCredentials(ssl_opts);
        return grpc::CreateChannel(config.server_address, credentials);
    }
    
    // Generate random test files
    void prepare_test_files() {
        temp_dir = "benchmark_temp_" + std::to_string(std::time(nullptr));
        fs::create_directories(temp_dir);
        fs::create_directories(temp_dir + "/downloads");
        
        // Create test file for upload
        std::string test_file = temp_dir + "/test_upload.dat";
        std::ofstream file(test_file, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to create test file: " + test_file);
        }
        
        std::string data = generate_random_data(config.file_size_kb * 1024);
        file.write(data.data(), data.size());
        file.close();
        
        if (config.verbose) {
            std::cout << "Created test file: " << test_file << " (" 
                      << (config.file_size_kb) << " KB)" << std::endl;
        }
    }
    
    // Worker thread function
    void worker_thread(int thread_id) {
        // Create service stubs
        auto greeter = Greeter::NewStub(channel);
        auto network = NetworkConfig::NewStub(channel);
        auto file_service = FileService::NewStub(channel);
        
        // Random number generator for selecting operations
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> op_dist(0, 6);
        
        int request_id = 0;
        
        while (running) {
            try {
                int operation = op_dist(gen);
                request_id++;
                
                switch (operation) {
                    case 0:
                        benchmark_hi(greeter.get());
                        break;
                    case 1:
                        benchmark_say_hello(greeter.get());
                        break;
                    case 2:
                        benchmark_say_hello_again(greeter.get());
                        break;
                    case 3:
                        benchmark_status(greeter.get());
                        break;
                    case 4:
                        benchmark_network_config(network.get());
                        break;
                    case 5:
                        benchmark_file_upload(file_service.get(), request_id);
                        break;
                    case 6:
                        benchmark_file_download(file_service.get());
                        break;
                }
                
                // Small random delay to prevent flooding
                std::this_thread::sleep_for(std::chrono::milliseconds(gen() % 10));
            }
            catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cerr << "Thread " << thread_id << " error: " << e.what() << std::endl;
            }
        }
    }
    
    // Monitor thread for resource usage
    void monitor_thread() {
        while (running) {
            metrics.record_resource_usage();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    // Benchmark individual operations
    void benchmark_hi(Greeter::Stub* stub) {
        EmptyRequest request;
        HelloReply reply;
        ClientContext context;
        
        metrics.increment_active_connections();
        metrics.total_requests++;
        
        auto start = std::chrono::high_resolution_clock::now();
        Status status = stub->Hi(&context, request, &reply);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (status.ok()) {
            metrics.successful_requests++;
            metrics.hi_latency.add_sample(end - start);
            metrics.bytes_received += reply.ByteSizeLong();
        } else {
            metrics.failed_requests++;
            metrics.add_error(status.error_code());
        }
        
        metrics.decrement_active_connections();
    }
    
    void benchmark_say_hello(Greeter::Stub* stub) {
        HelloRequest request;
        request.set_name(generate_random_data(config.greeting_size));
        HelloReply reply;
        ClientContext context;
        
        metrics.increment_active_connections();
        metrics.total_requests++;
        metrics.bytes_sent += request.ByteSizeLong();
        
        auto start = std::chrono::high_resolution_clock::now();
        Status status = stub->SayHello(&context, request, &reply);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (status.ok()) {
            metrics.successful_requests++;
            metrics.say_hello_latency.add_sample(end - start);
            metrics.bytes_received += reply.ByteSizeLong();
        } else {
            metrics.failed_requests++;
            metrics.add_error(status.error_code());
        }
        
        metrics.decrement_active_connections();
    }
    
    void benchmark_say_hello_again(Greeter::Stub* stub) {
        HelloRequest request;
        request.set_name(generate_random_data(config.greeting_size));
        HelloReply reply;
        ClientContext context;
        
        metrics.increment_active_connections();
        metrics.total_requests++;
        metrics.bytes_sent += request.ByteSizeLong();
        
        auto start = std::chrono::high_resolution_clock::now();
        Status status = stub->SayHelloAgain(&context, request, &reply);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (status.ok()) {
            metrics.successful_requests++;
            metrics.say_hello_again_latency.add_sample(end - start);
            metrics.bytes_received += reply.ByteSizeLong();
        } else {
            metrics.failed_requests++;
            metrics.add_error(status.error_code());
        }
        
        metrics.decrement_active_connections();
    }
    
    void benchmark_status(Greeter::Stub* stub) {
        EmptyRequest request;
        StatusResponse response;
        ClientContext context;
        
        metrics.increment_active_connections();
        metrics.total_requests++;
        
        auto start = std::chrono::high_resolution_clock::now();
        Status status = stub->Status(&context, request, &response);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (status.ok()) {
            metrics.successful_requests++;
            metrics.status_latency.add_sample(end - start);
            metrics.bytes_received += response.ByteSizeLong();
        } else {
            metrics.failed_requests++;
            metrics.add_error(status.error_code());
        }
        
        metrics.decrement_active_connections();
    }
    
    void benchmark_network_config(NetworkConfig::Stub* stub) {
        IPConfigRequest request;
        IPConfigResponse response;
        ClientContext context;
        
        // Alternate between DHCP and static IP
        static std::atomic<bool> use_dhcp{true};
        bool dhcp = use_dhcp.exchange(!use_dhcp);
        
        request.set_interface_name("eth" + std::to_string(rand() % 5));
        request.set_use_dhcp(dhcp);
        
        if (!dhcp) {
            request.set_requested_ip("192.168.1." + std::to_string(rand() % 254 + 1));
            request.set_requested_subnet_mask("255.255.255.0");
            request.set_requested_gateway("192.168.1.1");
            request.add_requested_dns("8.8.8.8");
            request.add_requested_dns("8.8.4.4");
        }
        
        metrics.increment_active_connections();
        metrics.total_requests++;
        metrics.bytes_sent += request.ByteSizeLong();
        
        auto start = std::chrono::high_resolution_clock::now();
        Status status = stub->ConfigureIP(&context, request, &response);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (status.ok()) {
            metrics.successful_requests++;
            metrics.network_config_latency.add_sample(end - start);
            metrics.bytes_received += response.ByteSizeLong();
        } else {
            metrics.failed_requests++;
            metrics.add_error(status.error_code());
        }
        
        metrics.decrement_active_connections();
    }
    
    void benchmark_file_upload(FileService::Stub* stub, int request_id) {
        try {
            std::string content = read_file(temp_dir + "/test_upload.dat");
            
            FileUploadRequest request;
            request.set_filename("benchmark_" + std::to_string(request_id) + ".dat");
            request.set_content(content);
            
            FileUploadResponse response;
            ClientContext context;
            
            metrics.increment_active_connections();
            metrics.total_requests++;
            metrics.bytes_sent += request.ByteSizeLong();
            
            auto start = std::chrono::high_resolution_clock::now();
            Status status = stub->UploadFile(&context, request, &response);
            auto end = std::chrono::high_resolution_clock::now();
            
            if (status.ok()) {
                metrics.successful_requests++;
                metrics.file_upload_latency.add_sample(end - start);
                metrics.bytes_received += response.ByteSizeLong();
            } else {
                metrics.failed_requests++;
                metrics.add_error(status.error_code());
            }
            
            metrics.decrement_active_connections();
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cerr << "File upload error: " << e.what() << std::endl;
        }
    }
    
    void benchmark_file_download(FileService::Stub* stub) {
        FileDownloadRequest request;
        request.set_filename("test_upload.dat");  // Must exist on server
        
        FileDownloadResponse response;
        ClientContext context;
        
        metrics.increment_active_connections();
        metrics.total_requests++;
        metrics.bytes_sent += request.ByteSizeLong();
        
        auto start = std::chrono::high_resolution_clock::now();
        Status status = stub->DownloadFile(&context, request, &response);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (status.ok()) {
            metrics.successful_requests++;
            metrics.file_download_latency.add_sample(end - start);
            metrics.bytes_received += response.ByteSizeLong();
            
            // Occasionally save downloaded file to verify integrity
            if (rand() % 50 == 0) {
                std::string path = temp_dir + "/downloads/download_" + 
                                   std::to_string(rand() % 1000) + ".dat";
                std::ofstream file(path, std::ios::binary);
                if (file) {
                    file.write(response.content().data(), response.content().size());
                }
            }
        } else {
            metrics.failed_requests++;
            metrics.add_error(status.error_code());
        }
        
        metrics.decrement_active_connections();
    }
    
    void print_progress(int percent) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "\rProgress: [";
        int pos = percent / 2;
        for (int i = 0; i < 50; ++i) {
            if (i < pos) std::cout << "=";
            else if (i == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << percent << "% ";
        
        // Show current stats
        if (percent % 10 == 0) {
            std::cout << "(RPS: " << std::fixed << std::setprecision(1) 
                      << metrics.total_requests.load() / (percent / 100.0 * config.duration_seconds)
                      << ", Active: " << metrics.active_connections.load() << ")";
        }
        
        std::cout.flush();
    }
    
    void print_summary() {
        std::cout << "\n\n==== Benchmark Results ====" << std::endl;
        std::cout << "Duration: " << metrics.duration_seconds() << " seconds" << std::endl;
        std::cout << "Total Requests: " << metrics.total_requests.load() << std::endl;
        std::cout << "Successful: " << metrics.successful_requests.load() << std::endl;
        std::cout << "Failed: " << metrics.failed_requests.load() << std::endl;
        std::cout << "Requests/sec: " << std::fixed << std::setprecision(2) 
                  << metrics.requests_per_second() << std::endl;
        std::cout << "Error Rate: " << (metrics.error_rate() * 100) << "%" << std::endl;
        std::cout << "Bandwidth: " << metrics.bandwidth_mbps() << " Mbps" << std::endl;
        std::cout << "Peak Connections: " << metrics.peak_connections.load() << std::endl;
        
        std::cout << "\n--- Latency (milliseconds) ---" << std::endl;
        auto print_latency = [](const std::string& name, const LatencyStats& stats) {
            if (stats.count() == 0) return;
            std::cout << name << " (" << stats.count() << " calls):" << std::endl;
            std::cout << "  Min: " << std::fixed << std::setprecision(3) << stats.min() << std::endl;
            std::cout << "  Avg: " << std::fixed << std::setprecision(3) << stats.avg() << std::endl;
            std::cout << "  p50: " << std::fixed << std::setprecision(3) << stats.median() << std::endl;
            std::cout << "  p90: " << std::fixed << std::setprecision(3) << stats.p90() << std::endl;
            std::cout << "  p95: " << std::fixed << std::setprecision(3) << stats.p95() << std::endl;
            std::cout << "  p99: " << std::fixed << std::setprecision(3) << stats.p99() << std::endl;
            std::cout << "  Max: " << std::fixed << std::setprecision(3) << stats.max() << std::endl;
            std::cout << "  Jitter: " << std::fixed << std::setprecision(3) << stats.jitter() << std::endl;
        };
        
        print_latency("Hi", metrics.hi_latency);
        print_latency("SayHello", metrics.say_hello_latency);
        print_latency("SayHelloAgain", metrics.say_hello_again_latency);
        print_latency("Status", metrics.status_latency);
        print_latency("NetworkConfig", metrics.network_config_latency);
        print_latency("FileUpload", metrics.file_upload_latency);
        print_latency("FileDownload", metrics.file_download_latency);
    }
    
    void save_results() {
        json results = metrics.to_json();
        std::ofstream out(config.output_file);
        if (out) {
            out << std::setw(4) << results;
            std::cout << "Results saved to " << config.output_file << std::endl;
        } else {
            std::cerr << "Failed to write results to " << config.output_file << std::endl;
        }
    }
    
    void cleanup() {
        try {
            fs::remove_all(temp_dir);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to clean up temp files: " << e.what() << std::endl;
        }
    }

public:
    Benchmarker(const BenchmarkConfig& cfg) : config(cfg) {}
    
    ~Benchmarker() {
        cleanup();
    }
    
    void run() {
        try {
            // Create channel
            std::cout << "Connecting to " << config.server_address << std::endl;
            channel = config.use_tls ? create_secure_channel() : 
                      grpc::CreateChannel(config.server_address, grpc::InsecureChannelCredentials());
            
            // Prepare test files
            prepare_test_files();
            
            // Warmup phase
            if (config.warmup_seconds > 0) {
                std::cout << "Warming up for " << config.warmup_seconds << " seconds..." << std::endl;
                running = true;
                
                // Use half the threads for warmup
                for (size_t i = 0; i < config.num_threads / 2; ++i) {
                    threads.emplace_back(&Benchmarker::worker_thread, this, i);
                }
                
                std::this_thread::sleep_for(std::chrono::seconds(config.warmup_seconds));
                running = false;
                
                for (auto& t : threads) {
                    if (t.joinable()) t.join();
                }
                threads.clear();
                
                // Reset metrics after warmup
                metrics.~BenchmarkMetrics();
                new (&metrics) BenchmarkMetrics();
            }
            
            // Main benchmark phase
            std::cout << "Running benchmark for " << config.duration_seconds << " seconds..." << std::endl;
            running = true;
            metrics.start_benchmark();
            
            // Start worker threads
            for (size_t i = 0; i < config.num_threads; ++i) {
                threads.emplace_back(&Benchmarker::worker_thread, this, i);
            }
            
            // Start resource monitoring thread
            threads.emplace_back(&Benchmarker::monitor_thread, this);
            
            // Monitor progress
            for (int i = 0; i <= 100; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    config.duration_seconds * 10));  // 100 updates
                print_progress(i);
            }
            
            // Stop benchmark
            running = false;
            metrics.end_benchmark();
            
            // Wait for threads to finish
            for (auto& t : threads) {
                if (t.joinable()) t.join();
            }
            
            // Print and save results
            print_summary();
            save_results();
            
        } catch (const std::exception& e) {
            std::cerr << "Benchmark failed: " << e.what() << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    BenchmarkConfig config;
    
    // Parse command line arguments (simple implementation)
    for (int i = 1; i < argc; i += 2) {
        std::string arg = argv[i];
        
        if (i + 1 >= argc) {
            std::cerr << "Missing value for argument: " << arg << std::endl;
            return 1;
        }
        
        if (arg == "--server") {
            config.server_address = argv[i+1];
        } else if (arg == "--threads") {
            config.num_threads = std::stoi(argv[i+1]);
        } else if (arg == "--duration") {
            config.duration_seconds = std::stoi(argv[i+1]);
        } else if (arg == "--warmup") {
            config.warmup_seconds = std::stoi(argv[i+1]);
        } else if (arg == "--file-size") {
            config.file_size_kb = std::stoi(argv[i+1]);
        } else if (arg == "--greeting-size") {
            config.greeting_size = std::stoi(argv[i+1]);
        } else if (arg == "--output") {
            config.output_file = argv[i+1];
        } else if (arg == "--no-tls") {
            config.use_tls = false;
            i--;  // No value for this flag
        } else if (arg == "--verbose") {
            config.verbose = true;
            i--;  // No value for this flag
        }
    }
    
    std::cout << "===== gRPC Benchmark Tool =====" << std::endl;
    std::cout << "Server: " << config.server_address << std::endl;
    std::cout << "Threads: " << config.num_threads << std::endl;
    std::cout << "Duration: " << config.duration_seconds << " seconds" << std::endl;
    std::cout << "TLS Enabled: " << (config.use_tls ? "Yes" : "No") << std::endl;
    std::cout << "=============================" << std::endl;
    
    Benchmarker benchmark(config);
    benchmark.run();
    
    return 0;
}
