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
    ssl_opts.pem_root_certs = read_file("./certs/server.crt");
    
    auto channel_creds = grpc::SslCredentials(ssl_opts);
    auto channel = grpc::CreateChannel("localhost:50051", channel_creds);

    // Create clients
    GreeterClient greeter(channel);
    NetworkConfigClient network(channel);
    FileClient file(channel);

    // Create HTTPS client
    httplib::SSLClient http_client("localhost", 8443);
    http_client.set_ca_cert_path("./certs/server.crt");
    // No need for client certificate in this case
    
    // Test gRPC calls
    std::cout << "Testing gRPC calls..." << std::endl;
    
    // Test Greeter service
    std::string reply = greeter.SayHello("World");
    std::cout << "Greeter received: " << reply << std::endl;

    reply = greeter.SayHelloAgain("World");
    std::cout << "Greeter received: " << reply << std::endl;

    // Test Network Config service
    auto network_response = network.ConfigureIP("eth0", true);
    std::cout << "Network Config received: " << network_response.status_message() << std::endl;
    std::cout << "IP Address: " << network_response.ip_address() << std::endl;
    std::cout << "Subnet Mask: " << network_response.subnet_mask() << std::endl;
    std::cout << "Default Gateway: " << network_response.default_gateway() << std::endl;
    std::cout << "DNS Servers:" << std::endl;
    for (const auto& dns : network_response.dns_servers()) {
        std::cout << "  - " << dns << std::endl;
    }

    // Test File service
    std::filesystem::create_directories("./downloads");
    file.UploadFile("test.txt");
    file.DownloadFile("test.txt");

    // Test HTTPS calls
    std::cout << "\nTesting HTTPS calls..." << std::endl;

    // Test GET request
    auto res = http_client.Get("/hi");
    if (res && res->status == 200) {
        std::cout << "GET /hi response: " << res->body << std::endl;
    }

    // Test POST request
    json post_data = {{"name", "World"}};
    res = http_client.Post("/api/hello", post_data.dump(), "application/json");
    if (res && res->status == 200) {
        std::cout << "POST /api/hello response: " << res->body << std::endl;
    }

    // Test file upload
    httplib::MultipartFormDataItems items = {
        {"file", "Hello, World!", "test.txt", "text/plain"}
    };
    res = http_client.Post("/upload", items);
    if (res && res->status == 200) {
        std::cout << "File upload response: " << res->body << std::endl;
    }

    // Test file download
    res = http_client.Get("/download/test.txt");
    if (res && res->status == 200) {
        std::ofstream outfile("./downloads/downloaded_test.txt");
        outfile << res->body;
        outfile.close();
        std::cout << "File downloaded successfully" << std::endl;
    }

    return 0;
}