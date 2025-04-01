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
const int MAX_MESSAGE_LENGTH = 50 * 1024 * 1024;

namespace fs = std::filesystem;

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

// Updated GreeterServiceImpl with configuration passed in its constructor
class GreeterServiceImpl final : public Greeter::Service {
public:
    GreeterServiceImpl(const std::string& ip, int port, int max_memory_mb)
      : server_ip_(ip), server_port_(port), max_memory_mb_(max_memory_mb) {}

    grpc::Status SayHello(ServerContext* context, 
        const HelloRequest* request,
        HelloReply* reply) override {
        // Get metadata from the client
        auto metadata = context->client_metadata();
        for (auto it = metadata.begin(); it != metadata.end(); ++it) {
            std::string key(it->first.data(), it->first.length());
            std::string value(it->second.data(), it->second.length());
            std::cout << "Metadata: " << key << " = " << value << std::endl;
        }
        context->AddInitialMetadata("content-type", "application/grpc-web+proto");
        context->AddInitialMetadata("x-grpc-web", "1");

        reply->set_message("Hello " + request->name());
        return grpc::Status::OK;
    }

    grpc::Status SayHelloAgain(ServerContext* context, const HelloRequest* request, HelloReply* reply) override {
        reply->set_message("Hello again " + request->name());
        return Status::OK;
    }

    grpc::Status Hi(ServerContext* context, const EmptyRequest* request, HelloReply* reply) override {
        reply->set_message("Hi! Secure gRPC server is running");
        return Status::OK;
    }

    // The Status RPC now returns the correct configuration including the RAM allotment.
    grpc::Status Status(ServerContext* context, const EmptyRequest* request, StatusResponse* response) override {
        response->set_status("running");
        response->set_timestamp(std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now()));
        response->set_version("1.2.0");
        
        auto* config = response->mutable_configuration();
        config->set_ip(server_ip_);
        config->set_port(server_port_);
        config->set_max_memory(max_memory_mb_);  // In MB
        
        return Status::OK;
    }

private:
    std::string server_ip_;
    int server_port_;
    int max_memory_mb_;
};

class NetworkConfigImpl final : public NetworkConfig::Service {
public:
    Status ConfigureIP(ServerContext* context, const IPConfigRequest* request, IPConfigResponse* response) override {
        std::cout << "Received IP configuration request for interface: " 
                  << request->interface_name() << std::endl;
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
        std::cout << "IP configuration completed for interface: " 
                  << request->interface_name() << std::endl;
        return Status::OK;
    }
};

class FileServiceImpl final : public FileService::Service {
private:
    const std::string upload_dir = "uploads/";

    bool ensure_upload_directory() {
        try {
            if (!fs::exists(upload_dir)) {
                fs::create_directories(upload_dir);
            }
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error creating upload directory: " << e.what() << std::endl;
            return false;
        }
    }

public:
    Status UploadFile(ServerContext* context, const FileUploadRequest* request,
                      FileUploadResponse* response) override {
        std::cout << "Received FileUploadRequest: " << request->DebugString() << std::endl;
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

        std::cout << "File uploaded: " << filepath << " (size: " 
                  << request->content().size() << " bytes)" << std::endl;
        return Status::OK;
    }

    Status DownloadFile(ServerContext* context, const FileDownloadRequest* request,
                        FileDownloadResponse* response) override {
        const std::string filepath = upload_dir + request->filename();
        if (!fs::exists(filepath)) {
            return Status(grpc::StatusCode::NOT_FOUND, "File not found: " + request->filename());
        }
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            return Status(grpc::StatusCode::INTERNAL, "Failed to open file: " + filepath);
        }
        std::string content((std::istreambuf_iterator<char>(file)), 
                            std::istreambuf_iterator<char>());
        file.close();
        response->set_content(content);
        std::cout << "File downloaded: " << filepath << " (size: " 
                  << content.size() << " bytes)" << std::endl;
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

int main() {
    try {
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

        // Enforce the memory limit on the process
        set_memory_limit(max_memory_bytes);

        // Build server address string from config values
        std::string server_address = ip + ":" + std::to_string(port);

        // Read SSL certificate files
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

        // Create service implementations and pass configuration to the Greeter service
        GreeterServiceImpl greeter_service(ip, port, max_memory_mb);
        NetworkConfigImpl network_service;
        FileServiceImpl file_service;

        ServerBuilder builder;
        // Use SSL credentials and bind to the configured address
        builder.AddListeningPort(server_address, grpc::SslServerCredentials(ssl_opts));
        // Set max message length limits
        builder.SetMaxReceiveMessageSize(MAX_MESSAGE_LENGTH);
        builder.SetMaxSendMessageSize(MAX_MESSAGE_LENGTH);
        // Register all services
        builder.RegisterService(&greeter_service);
        builder.RegisterService(&network_service);
        builder.RegisterService(&file_service);

        // Create upload directory if it doesn't exist
        if (!fs::exists("uploads")) {
            fs::create_directory("uploads");
        }

        std::unique_ptr<Server> server(builder.BuildAndStart());
        std::cout << "Server listening on " << server_address << std::endl;
        server->Wait();
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
