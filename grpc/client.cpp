#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using myservice::Greeter;
using myservice::HelloRequest;
using myservice::HelloReply;
using myservice::NetworkConfig;
using myservice::IPConfigRequest;
using myservice::IPConfigResponse;
using myservice::FileService;
using myservice::FileUploadRequest;
using myservice::FileUploadResponse;
using myservice::FileDownloadRequest;
using myservice::FileDownloadResponse;
using myservice::EmptyRequest;
using myservice::StatusResponse;
using myservice::ConfigInfo;
using json = nlohmann::json;

// Helper function to read file contents
std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

class GreeterClient {
public:
    GreeterClient(std::shared_ptr<Channel> channel) : stub_(Greeter::NewStub(channel)) {}

    std::string SayHello(const std::string& name) {
        HelloRequest request;
        request.set_name(name);
        HelloReply reply;
        ClientContext context;
        Status status = stub_->SayHello(&context, request, &reply);

        if (status.ok()) {
            return reply.message();
        } else {
            std::cout << "gRPC failed: " << status.error_code() << ": " << status.error_message() << std::endl;
            return "RPC failed";
        }
    }

    std::string SayHelloAgain(const std::string& name) {
        HelloRequest request;
        request.set_name(name);
        HelloReply reply;
        ClientContext context;
        Status status = stub_->SayHelloAgain(&context, request, &reply);

        if (status.ok()) {
            return reply.message();
        } else {
            std::cout << "gRPC failed: " << status.error_code() << ": " << status.error_message() << std::endl;
            return "RPC failed";
        }
    }

    std::string Hi() {
        EmptyRequest request;
        HelloReply reply;
        ClientContext context;
        Status status = stub_->Hi(&context, request, &reply);

        if (status.ok()) {
            return reply.message();
        } else {
            std::cout << "gRPC failed: " << status.error_code() << ": " 
                     << status.error_message() << std::endl;
            return "RPC failed";
        }
    }

    struct StatusInfo {
        std::string status;
        std::string version;
        time_t timestamp;
        struct {
            std::string ip;
            int port;
            int max_memory;
        } config;
    };

    StatusInfo GetStatus() {
        EmptyRequest request;
        StatusResponse response;
        ClientContext context;
        Status status = stub_->Status(&context, request, &response);

        StatusInfo info;
        if (status.ok()) {
            info.status = response.status();
            info.version = response.version();
            info.timestamp = response.timestamp();
            info.config.ip = response.configuration().ip();
            info.config.port = response.configuration().port();
            info.config.max_memory = response.configuration().max_memory();
        } else {
            std::cout << "gRPC failed: " << status.error_code() << ": " 
                     << status.error_message() << std::endl;
        }
        return info;
    }

private:
    std::unique_ptr<Greeter::Stub> stub_;
};

class NetworkConfigClient {
public:
    NetworkConfigClient(std::shared_ptr<Channel> channel) : stub_(NetworkConfig::NewStub(channel)) {}

    IPConfigResponse ConfigureIP(const std::string& interface_name, bool use_dhcp,
                               const std::string& ip = "", const std::string& subnet_mask = "",
                               const std::string& gateway = "", const std::vector<std::string>& dns = {}) {
        IPConfigRequest request;
        request.set_interface_name(interface_name);
        request.set_use_dhcp(use_dhcp);
        
        if (!use_dhcp) {
            request.set_requested_ip(ip);
            request.set_requested_subnet_mask(subnet_mask);
            request.set_requested_gateway(gateway);
            for (const auto& dns_server : dns) {
                request.add_requested_dns(dns_server);
            }
        }

        IPConfigResponse response;
        ClientContext context;
        Status status = stub_->ConfigureIP(&context, request, &response);

        if (!status.ok()) {
            std::cout << "gRPC failed: " << status.error_code() << ": " << status.error_message() << std::endl;
        }

        return response;
    }

private:
    std::unique_ptr<NetworkConfig::Stub> stub_;
};

class FileClient {
public:
    FileClient(std::shared_ptr<Channel> channel) : stub_(FileService::NewStub(channel)) {}

    bool UploadFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        FileUploadRequest request;
        request.set_filename(std::filesystem::path(filename).filename().string());
        request.set_content(content);

        FileUploadResponse response;
        ClientContext context;
        Status status = stub_->UploadFile(&context, request, &response);

        if (status.ok()) {
            std::cout << "Upload successful: " << response.message() << std::endl;
            return true;
        } else {
            std::cerr << "Upload failed: " << status.error_message() << std::endl;
            return false;
        }
    }

        bool DownloadFile(const std::string& filename) {
        FileDownloadRequest request;
        request.set_filename(filename);

        FileDownloadResponse response;
        ClientContext context;
        Status status = stub_->DownloadFile(&context, request, &response);

        if (status.ok()) {
            std::ofstream file("./downloads/" + filename, std::ios::binary);
            if (!file) {
                std::cerr << "Failed to create file: " << filename << std::endl;
                return false;
            }
            file.write(response.content().data(), response.content().size());
            file.close();
            std::cout << "Download successful" << std::endl;
            return true;
        } else {
            std::cerr << "Download failed: " << status.error_message() << std::endl;
            return false;
        }
    }

private:
    std::unique_ptr<FileService::Stub> stub_;
};

int main() {
    // Create SSL credentials for gRPC
    grpc::SslCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = read_file("../../certs/ca.crt");
    ssl_opts.pem_private_key = read_file("../../certs/client.key");
    ssl_opts.pem_cert_chain = read_file("../../certs/client.crt");
    
    auto channel_creds = grpc::SslCredentials(ssl_opts);
    auto channel = grpc::CreateChannel("localhost:50051", channel_creds);

    std::cout << "Creating secure channel to localhost:50051..." << std::endl;

    // Create clients
    GreeterClient greeter(channel);
    NetworkConfigClient network(channel);
    FileClient file(channel);

    std::cout << "\n=== Testing Greeter Service ===\n" << std::endl;

    // Test Hi endpoint
    std::cout << "Testing Hi endpoint..." << std::endl;
    auto hi_response = greeter.Hi();
    std::cout << "Response: " << hi_response << "\n" << std::endl;

    // Test SayHello
    std::cout << "Testing SayHello..." << std::endl;
    std::string hello_response = greeter.SayHello("Alice");
    std::cout << "Response: " << hello_response << "\n" << std::endl;

    // Test SayHelloAgain
    std::cout << "Testing SayHelloAgain..." << std::endl;
    std::string hello_again_response = greeter.SayHelloAgain("Bob");
    std::cout << "Response: " << hello_again_response << "\n" << std::endl;

    // Test Status endpoint
    std::cout << "\n=== Testing Status Service ===\n" << std::endl;
    auto status = greeter.GetStatus();
    std::cout << "Status: " << status.status << std::endl;
    std::cout << "Version: " << status.version << std::endl;
    std::cout << "Timestamp: " << std::ctime(&status.timestamp);
    std::cout << "Configuration:" << std::endl;
    std::cout << "  IP: " << status.config.ip << std::endl;
    std::cout << "  Port: " << status.config.port << std::endl;
    std::cout << "  Max Memory: " << status.config.max_memory << "MB\n" << std::endl;

    std::cout << "\n=== Testing Network Config Service ===\n" << std::endl;

    // Test DHCP Configuration
    std::cout << "Testing DHCP Configuration..." << std::endl;
    auto dhcp_response = network.ConfigureIP("eth0", true);
    std::cout << "DHCP Response:" << std::endl;
    std::cout << "Status: " << dhcp_response.status_message() << std::endl;
    std::cout << "IP Address: " << dhcp_response.ip_address() << std::endl;
    std::cout << "Subnet Mask: " << dhcp_response.subnet_mask() << std::endl;
    std::cout << "Gateway: " << dhcp_response.default_gateway() << std::endl;
    std::cout << "DNS Servers:" << std::endl;
    for (const auto& dns : dhcp_response.dns_servers()) {
        std::cout << "  - " << dns << std::endl;
    }

    // Test Static IP Configuration
    std::cout << "\nTesting Static IP Configuration..." << std::endl;
    auto static_response = network.ConfigureIP("eth0", false,
        "192.168.1.100", "255.255.255.0", "192.168.1.1",
        {"8.8.8.8", "8.8.4.4"});
    std::cout << "Static IP Response:" << std::endl;
    std::cout << "Status: " << static_response.status_message() << std::endl;
    std::cout << "IP Address: " << static_response.ip_address() << std::endl;
    std::cout << "Subnet Mask: " << static_response.subnet_mask() << std::endl;
    std::cout << "Gateway: " << static_response.default_gateway() << std::endl;

    std::cout << "\n=== Testing File Service ===\n" << std::endl;

    // Create test file
    std::cout << "Creating test file..." << std::endl;
    {
        std::ofstream test_file("test_upload.txt");
        test_file << "Hello, this is a test file for gRPC file transfer!";
    }

    // Test file upload
    std::cout << "Testing file upload..." << std::endl;
    if (file.UploadFile("test_upload.txt")) {
        std::cout << "Upload successful!\n" << std::endl;
    }

    // Test file download
    std::cout << "Testing file download..." << std::endl;
    if (file.DownloadFile("test_upload.txt")) {
        std::cout << "Download successful!\n" << std::endl;
        
        // Verify downloaded content
        std::ifstream downloaded_file("./downloads/test_upload.txt");
        std::string content((std::istreambuf_iterator<char>(downloaded_file)),
                           std::istreambuf_iterator<char>());
        std::cout << "Downloaded file content: " << content << "\n" << std::endl;
    }

    // Clean up test files
    std::filesystem::remove("test_upload.txt");
    
    std::cout << "All tests completed!" << std::endl;
    return 0;
}