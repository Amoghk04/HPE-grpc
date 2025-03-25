#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <mutex>
#include <filesystem>
#include "service.pb.h"

// Helper function declarations
std::string read_file(const std::string& path);
nlohmann::json load_config(const std::string& config_path);

// Helper function implementations
std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// Add this after your includes and using statements
nlohmann::json load_config(const std::string& config_path);  // Function declaration

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ClientContext;
using grpc::Channel;
using myservice::Greeter;
using myservice::HelloRequest;
using myservice::HelloReply;
using myservice::NetworkConfig;
using myservice::IPConfigRequest;
using myservice::IPConfigResponse;
using json = nlohmann::json;

// Global storage for our data
std::unordered_map<std::string, json> data_store;
std::mutex data_store_mutex; // For thread safety
std::mutex file_mutex;

// gRPC Greeter Service Implementation
class GreeterServiceImpl final : public Greeter::Service {
public:
    Status SayHello(ServerContext* context, const HelloRequest* request, HelloReply* reply) override {
        std::string response_message = "Hello, " + request->name();
        reply->set_message(response_message);
        std::cout << "[SERVER] Sent Response: " << response_message << std::endl;
        return Status::OK;
    }

    Status SayHelloAgain(ServerContext* context, const HelloRequest* request, HelloReply* reply) override {
        std::string response_message = "Hello again, " + request->name();
        reply->set_message(response_message);
        std::cout << "[SERVER] Sent Response: " << response_message << std::endl;
        return Status::OK;
    }
};

// gRPC Network Configuration Service Implementation
class NetworkConfigServiceImpl final : public NetworkConfig::Service {
public:
    Status ConfigureIP(ServerContext* context, const IPConfigRequest* request, IPConfigResponse* response) override {
        std::cout << "[SERVER] Received ConfigureIP request from client\n";
        std::cout << "[SERVER] Interface: " << request->interface_name() << " | DHCP: " << (request->use_dhcp() ? "Yes" : "No") << std::endl;

        if (request->use_dhcp()) {
            // Simulate DHCP Configuration
            response->set_ip_address("192.168.1.100");
            response->set_subnet_mask("255.255.255.0");
            response->set_default_gateway("192.168.1.1");
            response->add_dns_servers("8.8.8.8");
            response->add_dns_servers("8.8.4.4");
            response->set_status_message("DHCP configuration assigned.");
        } else {
            // Static IP Configuration
            response->set_ip_address(request->requested_ip());
            response->set_subnet_mask(request->requested_subnet_mask());
            response->set_default_gateway(request->requested_gateway());
            for (const auto& dns : request->requested_dns()) {
                response->add_dns_servers(dns);
            }
            response->set_status_message("Static IP configuration assigned.");
        }

        std::cout << "[SERVER] Sent IP configuration response.\n";
        return Status::OK;
    }
};

class FileServiceImpl final : public myservice::FileService::Service {
private:
    std::mutex file_mutex;  // Per-instance mutex for file operations
    std::unordered_map<std::string, std::mutex> file_mutexes;  // Per-file mutexes
    std::mutex mutexes_mutex;  // Mutex for the file_mutexes map

    // Get or create a mutex for a specific file
    std::mutex& getFileMutex(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutexes_mutex);
        return file_mutexes[filename];
    }

public:
    grpc::Status UploadFile(grpc::ServerContext* context, const myservice::FileUploadRequest* request, myservice::FileUploadResponse* response) override {
        // Get the mutex for this specific file
        std::mutex& specific_file_mutex = getFileMutex(request->filename());
        std::lock_guard<std::mutex> file_lock(specific_file_mutex);
        
        // Create uploads directory if it doesn't exist
        std::filesystem::create_directories("./uploads");
        
        // Get current timestamp for logging
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
        
        std::cout << "\n[SERVER][" << timestamp << "] Received file: " << request->filename() << std::endl;
        
        // Open file in append mode to maintain write order
        std::ofstream outfile("./uploads/" + request->filename(), std::ios::app);
        if (!outfile) {
            std::cerr << "[SERVER][" << timestamp << "] Failed to open file: " << request->filename() << std::endl;
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to open file for writing");
        }
        
        // Write the content with timestamp
        outfile << "[" << timestamp << "] " << request->content();
        outfile.flush();  // Ensure content is written immediately
        outfile.close();
        
        std::cout << "[SERVER][" << timestamp << "] Successfully wrote to file: " << request->filename() << std::endl;
        
        response->set_message("File uploaded successfully");
        return grpc::Status::OK;
    }

    grpc::Status DownloadFile(grpc::ServerContext* context, const myservice::FileDownloadRequest* request, myservice::FileDownloadResponse* response) override {
        std::mutex& specific_file_mutex = getFileMutex(request->filename());
        std::lock_guard<std::mutex> file_lock(specific_file_mutex);
        
        std::ifstream infile("./uploads/" + request->filename(), std::ios::binary);
        if (!infile) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "File not found");
        }
        std::ostringstream buffer;
        buffer << infile.rdbuf();
        response->set_content(buffer.str());
        return grpc::Status::OK;
    }
};

// Forward declaration of SetupHttpRoutes function
template <typename Server>
void SetupHttpRoutes(Server &server);

// Keep the function definition where it is
nlohmann::json load_config(const std::string& config_path) {
    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        throw std::runtime_error("Failed to open configuration file: " + config_path);
    }
    nlohmann::json config;
    config_file >> config;
    return config;
}

// HTTP Server with cpp-httplib
void RunHttpServer() {
    // Create an HTTPS server
    httplib::SSLServer http_server("../certs/server.crt", "../certs/server.key");

    // Load configuration at the start of RunHttpServer
    nlohmann::json config;
    try {
        config = load_config("../config.json");
    } catch (const std::exception& e) {
        std::cerr << "[SERVER] Error loading configuration: " << e.what() << std::endl;
        return;
    }

    // Setup routes
    http_server.Get("/hi", [](const httplib::Request&, httplib::Response& res) {
        std::cout << "[SERVER] Received GET /hi request" << std::endl;
        res.set_content("Hello HI from the HTTPS server!", "text/plain");
    });

    http_server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::cout << "[SERVER] Received GET / request" << std::endl;
        res.set_content("Hello FROM SERVER !", "text/plain");
    });

    http_server.Post("/echo", [](const httplib::Request& req, httplib::Response& res) {
        std::cout << "[SERVER] Received POST /echo request" << std::endl;
        res.set_content("Echo: " + req.body, "text/plain");
    });

    // Add API endpoints that bridge to gRPC services
    http_server.Post("/api/hello", [](const httplib::Request& req, httplib::Response& res) {
        std::cout << "[SERVER] Received POST /api/hello request" << std::endl;
        try {
            json request_data = json::parse(req.body);
            std::string name = request_data["name"];
            
            // Create secure gRPC client and make the call
            grpc::SslCredentialsOptions ssl_opts;
            ssl_opts.pem_root_certs = read_file("../certs/server.crt");
            ssl_opts.pem_private_key = read_file("../certs/server.key");
            ssl_opts.pem_cert_chain = read_file("../certs/server.crt");
            
            auto channel_creds = grpc::SslCredentials(ssl_opts);
            auto channel = grpc::CreateChannel("localhost:50051", channel_creds);
            std::unique_ptr<Greeter::Stub> stub = Greeter::NewStub(channel);
            
            HelloRequest grpc_req;
            HelloReply grpc_reply;
            ClientContext context;
            
            grpc_req.set_name(name);
            Status status = stub->SayHello(&context, grpc_req, &grpc_reply);
            
            if (status.ok()) {
                json response = {
                    {"message", grpc_reply.message()},
                    {"status", "success"}
                };
                res.set_content(response.dump(), "application/json");
            } else {
                json error = {
                    {"error", "gRPC call failed"},
                    {"status", "error"}
                };
                res.status = 500;
                res.set_content(error.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            json error = {
                {"error", e.what()},
                {"status", "error"}
            };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // GET endpoint to fetch server status
    http_server.Get("/status", [&config](const httplib::Request&, httplib::Response& res) {
        std::cout << "[SERVER] Received GET /status request" << std::endl;
        json status = {
            {"status", "running"},
            {"timestamp", std::time(nullptr)},
            {"version", "1.0"},
            {"configuration", {
                {"ip", config["server"]["ip"]},
                {"port", config["server"]["port"]},
                {"max_memory", config["limits"]["max_memory"]}
            }}
        };
        res.set_content(status.dump(), "application/json");
    });

    // GET endpoint to list all stored data
    http_server.Get("/data", [](const httplib::Request&, httplib::Response& res) {
        std::cout << "[SERVER] Received GET /data request" << std::endl;
        
        json response = json::array();
        std::lock_guard<std::mutex> lock(data_store_mutex);
        for (const auto& [id, value] : data_store) {
            response.push_back(value);
        }
        
        res.set_content(response.dump(), "application/json");
    });

    // GET endpoint to retrieve data by ID
    http_server.Get(R"(/data/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        std::cout << "[SERVER] Received GET /data/" << id << " request" << std::endl;
        
        std::lock_guard<std::mutex> lock(data_store_mutex);
        if (data_store.find(id) != data_store.end()) {
            res.set_content(data_store[id].dump(), "application/json");
        } else {
            json error = {
                {"error", "Not found"},
                {"message", "No data found with ID: " + id}
            };
            res.status = 404;
            res.set_content(error.dump(), "application/json");
        }
    });

    // POST endpoint to store data
    http_server.Post("/data", [](const httplib::Request& req, httplib::Response& res) {
        std::cout << "[SERVER] Received POST /data request" << std::endl;
        try {
            json request_data = json::parse(req.body);
            
            if (!request_data.contains("id")) {
                throw std::runtime_error("Data must contain an 'id' field");
            }
            
            std::string id = request_data["id"].get<std::string>();
            
            {
                std::lock_guard<std::mutex> lock(data_store_mutex);
                data_store[id] = request_data;
            }
            
            json response = {
                {"message", "Data stored successfully"},
                {"stored_data", request_data}
            };
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            json error = {
                {"error", "Invalid data"},
                {"details", e.what()}
            };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // GET endpoint with query parameters
    http_server.Get("/query", [](const httplib::Request& req, httplib::Response& res) {
        std::cout << "[SERVER] Received GET /query request" << std::endl;
        json response = {
            {"params", json::object()}
        };
        
        // Add all query parameters to response
        for (const auto& param : req.params) {
            response["params"][param.first] = param.second;
        }
        
        res.set_content(response.dump(), "application/json");
    });

    // PUT endpoint to update data
    http_server.Put(R"(/update/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        std::cout << "[SERVER] Received PUT /update/" << id << " request" << std::endl;
        
        try {
            json update_data = json::parse(req.body);
            
            std::lock_guard<std::mutex> lock(data_store_mutex);
            if (data_store.find(id) != data_store.end()) {
                // Update existing data
                data_store[id].merge_patch(update_data);
                
                json response = {
                    {"message", "Update successful"},
                    {"updated_data", data_store[id]}
                };
                res.set_content(response.dump(), "application/json");
            } else {
                json error = {
                    {"error", "Not found"},
                    {"message", "No data found with ID: " + id}
                };
                res.status = 404;
                res.set_content(error.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            json error = {
                {"error", "Invalid data"},
                {"details", e.what()}
            };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    http_server.Post("/upload", [](const httplib::Request& req, httplib::Response& res) {
        if (req.has_file("file")) {
            const auto& file = req.get_file_value("file");
            std::string filename = file.filename;
            const std::string& content = file.content;
    
            // Ensure uploads directory exists
            std::filesystem::create_directories("./uploads");
    
            // Open file for writing
            std::ofstream outfile("./uploads/" + filename, std::ios::binary);
            if (!outfile) {
                res.status = 500;
                res.set_content("Failed to save file", "text/plain");
                return;
            }
    
            // Simulate progress logging
            size_t total_size = content.size();
            size_t chunk_size = total_size / 10; // Divide into 10 chunks for progress
            size_t written = 0;
    
            for (size_t i = 0; i < total_size; i += chunk_size) {
                size_t write_size = std::min(chunk_size, total_size - written);
                outfile.write(content.data() + written, write_size);
                written += write_size;
    
                // Log progress
                int progress = static_cast<int>((static_cast<double>(written) / total_size) * 100);
                std::cout << "[SERVER] Upload Progress: " << progress << "%\n";
    
                // Simulate delay to show progress (optional)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
    
            outfile.close();
            res.set_content("File uploaded successfully", "text/plain");
        } else {
            res.status = 400;
            res.set_content("No file provided in request", "text/plain");
        }
    });    
    
    http_server.Get(R"(/download/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        auto filename = req.matches[1].str();
    
        std::lock_guard<std::mutex> lock(file_mutex);
        std::ifstream infile("./uploads/" + filename, std::ios::binary);
        if (!infile) {
            res.status = 404;
            res.set_content("File not found", "text/plain");
            return;
        }
        std::ostringstream buffer;
        buffer << infile.rdbuf();
        res.set_content(buffer.str(), "application/octet-stream");
    });

    std::cout << "[SERVER] HTTPS Server starting on port 8443...\n";
    if (!http_server.listen("0.0.0.0", 8443)) {
        std::cerr << "[SERVER] ERROR: Failed to start HTTPS server on port 8443\n";
    }
}

// gRPC Server
void RunGrpcServer() {
    std::cout << "[SERVER] Starting gRPC and HTTPS servers...\n";

    // Load configuration
    nlohmann::json config;
    try {
        config = load_config("../config.json");
    } catch (const std::exception& e) {
        std::cerr << "[SERVER] Error loading configuration: " << e.what() << std::endl;
        return;
    }

    // Extract server settings
    std::string server_ip = config["server"]["ip"];
    int server_port = config["server"]["port"];
    int max_memory = config["limits"]["max_memory"];

    std::cout << "[SERVER] Configuration loaded: IP=" << server_ip << ", Port=" << server_port << ", Max Memory=" << max_memory << "MB\n";

    // Use the configuration to set up the server
    std::string server_address = server_ip + ":" + std::to_string(server_port);
    GreeterServiceImpl greeter_service;
    NetworkConfigServiceImpl network_service;
    FileServiceImpl file_service;

    grpc::SslServerCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = read_file("../certs/server.crt");
    ssl_opts.pem_key_cert_pairs.push_back({
        read_file("../certs/server.key"),
        read_file("../certs/server.crt")
    });

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::SslServerCredentials(ssl_opts));
    builder.RegisterService(&greeter_service);
    builder.RegisterService(&network_service);
    builder.RegisterService(&file_service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "[SERVER] gRPC Server Listening on " << server_address << " with SSL\n";
    server->Wait();
}

int main() {
    std::cout << "[SERVER] Starting gRPC and HTTPS servers...\n";
    
    // Run gRPC Server in a separate thread
    std::thread grpc_thread(RunGrpcServer);
    
    // Run HTTPS Server in the main thread to ensure it doesn't terminate
    RunHttpServer();
    
    // This code will only be reached if the HTTPS server fails to start
    // Wait for gRPC thread to complete
    grpc_thread.join();
    
    return 0;
}
