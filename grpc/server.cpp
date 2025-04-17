#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <sys/resource.h>      // For setrlimit
#include <stdexcept>
#include <queue>
#include <condition_variable>
#include <thread>
#include <functional>
#include <future>
#include <atomic>

#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include "service.grpc.pb.h"
#include <nlohmann/json.hpp>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::SslServerCredentials;
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

constexpr auto SERVER_CERT = "../../certs/server.crt";
constexpr auto SERVER_KEY = "../../certs/server.key";
constexpr auto ROOT_CERT = "../../certs/ca.crt";

// const int MAX_MESSAGE_LENGTH = 10 * 1024 * 1024; // 10MB
const int MAX_MESSAGE_LENGTH = 50 * 1024 * 1024;

namespace fs = std::filesystem;

// Thread-safe logging function for debugging and monitoring.
std::mutex log_mutex;
template<typename... Args>
void log(Args&&... args) {
    std::lock_guard<std::mutex> lock(log_mutex);
    (std::cout << ... << std::forward<Args>(args));
    std::cout << std::endl;
}

// Request Queue Implementation with detailed logging
/*
A thread-safe task queue that manages incoming requests using worker threads.
Tasks are enqueued with Enqueue() and processed by worker threads in WorkerThread().
Provides statistics like queue size (GetQueueSize()) and tasks processed (GetTasksProcessed()).
*/
class RequestQueue {
public:
    RequestQueue(int num_workers = 1) : stop_(false), task_counter_(0), completed_counter_(0) {
        log("RequestQueue: Initializing with ", num_workers, " worker thread(s)");
        
        // Start worker threads
        for (int i = 0; i < num_workers; ++i) {
            workers_.emplace_back(&RequestQueue::WorkerThread, this, i);
        }
    }

    ~RequestQueue() {
        log("RequestQueue: Shutting down, processed ", completed_counter_, " tasks total");
        
        // Signal to stop
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        
        // Join all worker threads
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // Add a task to the queue and return a future for the result
    template<typename Func, typename... Args>
    auto Enqueue(const std::string& task_name, Func&& func, Args&&... args) 
        -> std::future<typename std::invoke_result<Func, Args...>::type> {
        
        using ReturnType = typename std::invoke_result<Func, Args...>::type;
        
        // Generate unique task ID
        int task_id = ++task_counter_;
        
        log("RequestQueue: Enqueuing task #", task_id, " (", task_name, ")");
        
        // Create a packaged task to execute the function
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            [this, task_id, task_name, func = std::forward<Func>(func), 
             args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                
                log("RequestQueue: Starting task #", task_id, " (", task_name, ")");
                
                // Execute the actual function using the tuple of arguments
                auto start_time = std::chrono::high_resolution_clock::now();
                
                auto result = std::apply(func, args);
                
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                
                log("RequestQueue: Completed task #", task_id, " (", task_name, ") in ", duration, "ms");
                ++completed_counter_;
                
                return result;
            }
        );
        
        // Get the future result before adding to queue
        std::future<ReturnType> result = task->get_future();
        
        // Add to queue
        {
            std::unique_lock<std::mutex> lock(mutex_);
            int MAX_QUEUE_SIZE = 100;

            if (tasks_.size() > MAX_QUEUE_SIZE) {
                log("RequestQueue: Task #", task_id, " (", task_name, ") rejected due to queue size limit (", MAX_QUEUE_SIZE, ")");
                throw std::runtime_error("Queue size limit exceeded");
            }

            tasks_.emplace([task]() { (*task)(); });
            log("RequestQueue: Task #", task_id, " queued. Queue size: ", tasks_.size());

            // std::unique_lock<std::mutex> lock(mutex_);
            // tasks_.emplace([task]() { (*task)(); });
            // log("RequestQueue: Task #", task_id, " queued. Queue size: ", tasks_.size());
        
        }
        
        // Notify one worker
        cv_.notify_one();
        
        return result;
    }
    
    // Get queue statistics
    size_t GetQueueSize() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return tasks_.size();
    }
    
    int GetTasksProcessed() const {
        return completed_counter_;
    }

private:
    void WorkerThread(int worker_id) {
        log("RequestQueue: Worker #", worker_id, " started");
        
        while (true) {
            std::function<void()> task;
            
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { 
                    return stop_ || !tasks_.empty(); 
                });
                
                if (stop_ && tasks_.empty()) {
                    log("RequestQueue: Worker #", worker_id, " stopping");
                    return;
                }
                
                task = std::move(tasks_.front());
                tasks_.pop();
                log("RequestQueue: Worker #", worker_id, " dequeued task. Queue size: ", tasks_.size());
            }
            
            // Execute the task
            task();
        }
    }

    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    bool stop_;
    std::atomic<int> task_counter_;
    std::atomic<int> completed_counter_;
};

// Helper function to read files
std::string ReadFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    return std::string(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
}

// Function to set the memory limit for the process using setrlimit.
// limit_bytes should be the maximum allowed memory in bytes.
void set_memory_limit(size_t limit_bytes) {
    struct rlimit rl;
    rl.rlim_cur = limit_bytes;
    rl.rlim_max = limit_bytes;
    if (setrlimit(RLIMIT_AS, &rl) != 0) {
        std::cerr << "Error setting memory limit to " << limit_bytes << " bytes." << std::endl;
    } else {
        std::cout << "Memory limit set to " << limit_bytes << " bytes." << std::endl;
    }
}

// Updated GreeterServiceImpl with request queue and detailed logging
/*
Implements the Greeter service defined in the protobuf file.
Handles methods like SayHello, SayHelloAgain, Hi, and Status.
Uses the RequestQueue to process requests asynchronously.
*/
class GreeterServiceImpl final : public Greeter::Service {
public:
    GreeterServiceImpl(const std::string& ip, int port, int max_memory_mb, std::shared_ptr<RequestQueue> queue)
      : server_ip_(ip), server_port_(port), max_memory_mb_(max_memory_mb), queue_(queue) {
        log("GreeterService: Initialized with IP:", ip, " Port:", port);
    }

    grpc::Status SayHello(ServerContext* context, 
        const HelloRequest* request,
        HelloReply* reply) override {
        
        log("GreeterService: Received SayHello request for: ", request->name());
        
        // Extract client IP from context
        std::string peer = context->peer();
        
        // Enqueue the request and wait for the result
        auto future = queue_->Enqueue("SayHello", &GreeterServiceImpl::ProcessSayHello, this, 
                                     context, request, reply);
        return future.get();
    }

    grpc::Status SayHelloAgain(ServerContext* context, const HelloRequest* request, HelloReply* reply) override {
        log("GreeterService: Received SayHelloAgain request for: ", request->name());
        
        // Enqueue the request and wait for the result
        auto future = queue_->Enqueue("SayHelloAgain", &GreeterServiceImpl::ProcessSayHelloAgain, this, 
                                     context, request, reply);
        return future.get();
    }

    grpc::Status Hi(ServerContext* context, const EmptyRequest* request, HelloReply* reply) override {
        log("GreeterService: Received Hi request");
        
        // Enqueue the request and wait for the result
        auto future = queue_->Enqueue("Hi", &GreeterServiceImpl::ProcessHi, this, 
                                     context, request, reply);
        return future.get();
    }

    grpc::Status Status(ServerContext* context, const EmptyRequest* request, StatusResponse* response) override {
        log("GreeterService: Received Status request");
        
        // Enqueue the request and wait for the result
        auto future = queue_->Enqueue("Status", &GreeterServiceImpl::ProcessStatus, this, 
                                     context, request, response);
        return future.get();
    }

private:
    // Actual implementation methods that will be queued
    grpc::Status ProcessSayHello(ServerContext* context, 
        const HelloRequest* request,
        HelloReply* reply) {
        // Get metadata from the client
        auto metadata = context->client_metadata();
        for (auto it = metadata.begin(); it != metadata.end(); ++it) {
            std::string key(it->first.data(), it->first.length());
            std::string value(it->second.data(), it->second.length());
            log("GreeterService: Metadata: ", key, " = ", value);
        }
        context->AddInitialMetadata("content-type", "application/grpc-web+proto");
        context->AddInitialMetadata("x-grpc-web", "1");

        // Add log to show request is being processed from queue
        log("GreeterService: Processing SayHello request for: ", request->name());
        
        // Add artificial delay to demonstrate queue behavior (remove in production)
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        reply->set_message("Hello " + request->name());
        log("GreeterService: SayHello response created: ", reply->message());
        return grpc::Status::OK;
    }

    grpc::Status ProcessSayHelloAgain(ServerContext* context, const HelloRequest* request, HelloReply* reply) {
        log("GreeterService: Processing SayHelloAgain request for: ", request->name());
        
        // Add artificial delay to demonstrate queue behavior (remove in production)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        reply->set_message("Hello again " + request->name());
        log("GreeterService: SayHelloAgain response created: ", reply->message());
        return Status::OK;
    }

    grpc::Status ProcessHi(ServerContext* context, const EmptyRequest* request, HelloReply* reply) {
        log("GreeterService: Processing Hi request");
        reply->set_message("Hi! Secure gRPC server is running");
        log("GreeterService: Hi response created: ", reply->message());
        return Status::OK;
    }

    grpc::Status ProcessStatus(ServerContext* context, const EmptyRequest* request, StatusResponse* response) {
        log("GreeterService: Processing Status request");
        response->set_status("running");
        response->set_timestamp(std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now()));
        response->set_version("1.2.0");
        
        auto* config = response->mutable_configuration();
        config->set_ip(server_ip_);
        config->set_port(server_port_);
        config->set_max_memory(max_memory_mb_);  // In MB
        
        log("GreeterService: Status response created, timestamp: ", response->timestamp());
        return Status::OK;
    }

    std::string server_ip_;
    int server_port_;
    int max_memory_mb_;
    std::shared_ptr<RequestQueue> queue_;
};

/*
Implements the NetworkConfig service.
Handles IP configuration requests (ConfigureIP) for DHCP or static IP setups.
Uses the RequestQueue for asynchronous processing.
*/
class NetworkConfigImpl final : public NetworkConfig::Service {
public:
    NetworkConfigImpl(std::shared_ptr<RequestQueue> queue) : queue_(queue) {
        log("NetworkConfigService: Initialized");
    }
    
    Status ConfigureIP(ServerContext* context, const IPConfigRequest* request, IPConfigResponse* response) override {
        log("NetworkConfigService: Received ConfigureIP request for interface: ", request->interface_name());
        
        // Enqueue the request and wait for the result
        auto future = queue_->Enqueue("ConfigureIP", &NetworkConfigImpl::ProcessConfigureIP, this, 
                                     context, request, response);
        return future.get();
    }

private:
    Status ProcessConfigureIP(ServerContext* context, const IPConfigRequest* request, IPConfigResponse* response) {
        log("NetworkConfigService: Processing ConfigureIP request for interface: ", 
            request->interface_name(), ", DHCP: ", request->use_dhcp() ? "Yes" : "No");
        
        // Add artificial delay to demonstrate queue behavior (remove in production)
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        if (request->use_dhcp()) {
            response->set_status_message("DHCP configuration successful");
            response->set_ip_address("192.168.1.100");
            response->set_subnet_mask("255.255.255.0");
            response->set_default_gateway("192.168.1.1");
            response->add_dns_servers("8.8.8.8");
            response->add_dns_servers("8.8.4.4");
        } else {
            response->set_status_message("Static IP configuration successful");
            response->set_ip_address(request->requested_ip());
            response->set_subnet_mask(request->requested_subnet_mask());
            response->set_default_gateway(request->requested_gateway());
            for (const auto& dns : request->requested_dns()) {
                response->add_dns_servers(dns);
            }
        }
        log("NetworkConfigService: ConfigureIP response created for interface: ", 
            request->interface_name(), ", IP: ", response->ip_address());
        return Status::OK;
    }
    
    std::shared_ptr<RequestQueue> queue_;
};

/*
Implements the FileService for file upload and download.
Handles UploadFile and DownloadFile requests.
Ensures the upload directory exists and processes file operations asynchronously.
*/
class FileServiceImpl final : public FileService::Service {
private:
    const std::string upload_dir = "uploads/";
    std::shared_ptr<RequestQueue> queue_;

    bool ensure_upload_directory() {
        try {
            if (!fs::exists(upload_dir)) {
                fs::create_directories(upload_dir);
            }
            return true;
        } catch (const std::exception& e) {
            log("FileService: Error creating upload directory: ", e.what());
            return false;
        }
    }

public:
    FileServiceImpl(std::shared_ptr<RequestQueue> queue) : queue_(queue) {
        log("FileService: Initialized");
    }
    
    Status UploadFile(ServerContext* context, const FileUploadRequest* request,
                      FileUploadResponse* response) override {
        log("FileService: Received UploadFile request for file: ", request->filename(), 
            " (size: ", request->content().size(), " bytes)");
        
        // Enqueue the request and wait for the result
        auto future = queue_->Enqueue("UploadFile", &FileServiceImpl::ProcessUploadFile, this, 
                                     context, request, response);
        return future.get();
    }

    Status DownloadFile(ServerContext* context, const FileDownloadRequest* request,
                        FileDownloadResponse* response) override {
        log("FileService: Received DownloadFile request for file: ", request->filename());
        
        // Enqueue the request and wait for the result
        auto future = queue_->Enqueue("DownloadFile", &FileServiceImpl::ProcessDownloadFile, this, 
                                     context, request, response);
        return future.get();
    }

private:
    Status ProcessUploadFile(ServerContext* context, const FileUploadRequest* request,
                      FileUploadResponse* response) {
        log("FileService: Processing UploadFile request for: ", request->filename(),
            " (size: ", request->content().size(), " bytes)");
        
        // Add artificial delay to demonstrate queue behavior (remove in production)
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        if (!ensure_upload_directory()) {
            return Status(grpc::StatusCode::INTERNAL, "Failed to create upload directory");
        }
        if (request->filename().empty()) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "Filename cannot be empty");
        }
        const std::string filepath = upload_dir + request->filename();
        std::ofstream file(filepath, std::ios::binary);
        if (!file) {
            return Status(grpc::StatusCode::INTERNAL, "Failed to create file: " + filepath);
        }
        file.write(request->content().data(), request->content().size());
        file.close();

        response->set_success(true);
        response->set_filepath(filepath);
        response->set_message("File uploaded successfully");

        log("FileService: File uploaded successfully: ", filepath);
        return Status::OK;
    }

    Status ProcessDownloadFile(ServerContext* context, const FileDownloadRequest* request,
                        FileDownloadResponse* response) {
        log("FileService: Processing DownloadFile request for: ", request->filename());
        
        // Add artificial delay to demonstrate queue behavior (remove in production)
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        const std::string filepath = upload_dir + request->filename();
        if (!fs::exists(filepath)) {
            log("FileService: File not found: ", filepath);
            return Status(grpc::StatusCode::NOT_FOUND, "File not found: " + request->filename());
        }
        
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            log("FileService: Failed to open file: ", filepath);
            return Status(grpc::StatusCode::INTERNAL, "Failed to open file: " + filepath);
        }
        
        std::string content((std::istreambuf_iterator<char>(file)), 
                            std::istreambuf_iterator<char>());
        file.close();
        response->set_content(content);
        
        log("FileService: File downloaded: ", filepath, " (size: ", content.size(), " bytes)");
        return Status::OK;
    }
};

std::string read_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    std::string content((std::istreambuf_iterator<char>(file)), 
                        std::istreambuf_iterator<char>());
    return content;
}


#include <fstream>
#include <string>
#include <sstream>

void log_memory_usage() {
    log("log_memory_usage: Reading /proc/self/status");
    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open()) {
        log("log_memory_usage: Failed to open /proc/self/status");
        return;
    }
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream iss(line);
            std::string label;
            long memory_kb;
            iss >> label >> memory_kb;
            log("Memory Usage: ", memory_kb, " KB");
            return;
        }
    }
    log("log_memory_usage: VmRSS not found in /proc/self/status");
}

#include <sys/resource.h>

void log_cpu_usage() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);

    long user_cpu_time_ms = usage.ru_utime.tv_sec * 1000 + usage.ru_utime.tv_usec / 1000;
    long sys_cpu_time_ms = usage.ru_stime.tv_sec * 1000 + usage.ru_stime.tv_usec / 1000;

    log("CPU Usage: [User: ", user_cpu_time_ms, " ms, System: ", sys_cpu_time_ms, " ms]");
}

/*
Reads server configuration from config.json.
Initializes SSL credentials using certificate files.
Creates a shared RequestQueue for handling tasks.
Instantiates service implementations (GreeterServiceImpl, NetworkConfigImpl, FileServiceImpl) and registers them with the gRPC server.
Starts a monitoring thread to log queue statistics.
Builds and starts the gRPC server.
*/
int main() {
    try {
        // Setup logging with timestamps
        std::cout << std::fixed;
        
        log("Server: Starting up");
        
        // Read configuration from config.json
        std::ifstream config_file("../config.json");
        if (!config_file.is_open()) {
            throw std::runtime_error("Failed to open config.json");
        }
        nlohmann::json config_json;
        config_file >> config_json;

        // Extract server configuration
        std::string ip = config_json["server"]["ip"];
        int port = config_json["server"]["port"];
        // Assume max_memory is in MB in the config file
        int max_memory_mb = config_json["limits"]["max_memory"];
        size_t max_memory_bytes = static_cast<size_t>(max_memory_mb) * 1024 * 1024;

        // Get the number of worker threads for the queue (default to 1 for sequential processing)
        // int queue_workers = 2;
        int queue_workers = 1;
        if (config_json.contains("server") && config_json["server"].contains("queue_workers")) {
            queue_workers = config_json["server"]["queue_workers"];
        }

        log("Server: Request queue will use ", queue_workers, " worker thread(s)");

        // Enforce the memory limit on the process
        set_memory_limit(max_memory_bytes);

        // Build server address string from config values
        std::string server_address = ip + ":" + std::to_string(port);

        // Read SSL certificate files
        log("Server: Reading SSL certificates");
        std::string server_key = read_file("../../certs/server.key");
        std::string server_cert = read_file("../../certs/server.crt");
        std::string ca_cert = read_file("../../certs/ca.crt");

        grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp = {
            server_key,
            server_cert
        };

        grpc::SslServerCredentialsOptions ssl_opts(
            GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
        );
        ssl_opts.pem_root_certs = ca_cert;
        ssl_opts.pem_key_cert_pairs.push_back(pkcp);

        // Create a shared request queue
        auto request_queue = std::make_shared<RequestQueue>(queue_workers);

        // Create service implementations and pass configuration and queue to the services
        log("Server: Initializing services");
        GreeterServiceImpl greeter_service(ip, port, max_memory_mb, request_queue);
        NetworkConfigImpl network_service(request_queue);
        FileServiceImpl file_service(request_queue);

        ServerBuilder builder;
        // Use SSL credentials and bind to the configured address
        builder.AddListeningPort(server_address, grpc::SslServerCredentials(ssl_opts));
        // Set max message length limits
        builder.SetMaxReceiveMessageSize(MAX_MESSAGE_LENGTH);
        builder.SetMaxSendMessageSize(MAX_MESSAGE_LENGTH);

        // Limit concurrent streams
        // // Enable gRPC compression
        builder.SetDefaultCompressionAlgorithm(GRPC_COMPRESS_GZIP); 


        // Register all services
        builder.RegisterService(&greeter_service);
        builder.RegisterService(&network_service);
        builder.RegisterService(&file_service);

        // Create upload directory if it doesn't exist
        if (!fs::exists("uploads")) {
            fs::create_directory("uploads");
            log("Server: Created uploads directory");
        }

        // Start a monitoring thread for queue statistics
        bool monitor_running = true;
        std::thread monitor_thread([&monitor_running, &request_queue]() {
            while (monitor_running) {

                std::this_thread::sleep_for(std::chrono::seconds(10)); // Increase interval
                log_memory_usage();
                log_cpu_usage();
                // std::this_thread::sleep_for(std::chrono::seconds(5));
                log("Monitor: Queue size: ", request_queue->GetQueueSize(), 
                    ", Tasks processed: ", request_queue->GetTasksProcessed());
            }

            // while (monitor_running) {
            //     std::this_thread::sleep_for(std::chrono::seconds(5));
        
            //     // Get CPU and memory stats
            //     struct rusage usage;
            //     getrusage(RUSAGE_SELF, &usage);
        
            //     long user_cpu_time_ms = usage.ru_utime.tv_sec * 1000 + usage.ru_utime.tv_usec / 1000;
            //     long sys_cpu_time_ms = usage.ru_stime.tv_sec * 1000 + usage.ru_stime.tv_usec / 1000;
        
            //     // Linux specific: Read current memory usage from /proc/self/status
            //     long memory_kb = 0;
            //     std::ifstream status_file("/proc/self/status");
            //     std::string line;
            //     while (std::getline(status_file, line)) {
            //         if (line.rfind("VmRSS:", 0) == 0) { // Resident Set Size
            //             std::istringstream iss(line);
            //             std::string label;
            //             iss >> label >> memory_kb;
            //             break;
            //         }
            //     }
        
            //     log("Monitor: Queue size: ", request_queue->GetQueueSize(), 
            //         ", Tasks processed: ", request_queue->GetTasksProcessed(),
            //         ", CPU time: [user: ", user_cpu_time_ms, "ms, sys: ", sys_cpu_time_ms, "ms]",
            //         ", Memory: ", memory_kb, " KB");
            // }
        });

        // Build and start the server
        log("Server: Building and starting server");
        std::unique_ptr<Server> server(builder.BuildAndStart());
        log("Server: Listening on ", server_address);
        
        // Wait for the server to shut down
        server->Wait();
        
        // Clean up the monitoring thread
        monitor_running = false;
        if (monitor_thread.joinable()) {
            monitor_thread.join();
        }
        
    } catch (const std::exception& e) {
        log("Server: Fatal error: ", e.what());
        return 1;
    }

    return 0;
}