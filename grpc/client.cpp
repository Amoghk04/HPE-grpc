#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using myservice::Greeter;
using myservice::HelloRequest;
using myservice::HelloReply;
using myservice::NetworkConfig;
using myservice::IPConfigRequest;
using myservice::IPConfigResponse;

class Client {
public:
    Client(std::shared_ptr<Channel> channel)
        : greeter_stub_(Greeter::NewStub(channel)), network_stub_(NetworkConfig::NewStub(channel)), file_stub_(myservice::FileService::NewStub(channel)) {}

    void SayHello(const std::string& name) {
        HelloRequest request;
        request.set_name(name);
        HelloReply reply;
        ClientContext context;

        Status status = greeter_stub_->SayHello(&context, request, &reply);
        if (status.ok()) {
            std::cout << "[CLIENT] Server Response: " << reply.message() << std::endl;
        } else {
            std::cerr << "[CLIENT] gRPC Request failed" << std::endl;
        }
    }

    void ConfigureIP() {
        std::string interface_name;
        char use_dhcp;
        IPConfigRequest request;
        IPConfigResponse response;
        ClientContext context;

        std::cout << "Enter network interface name: ";
        std::cin >> interface_name;
        request.set_interface_name(interface_name);

        std::cout << "Use DHCP? (y/n): ";
        std::cin >> use_dhcp;
        request.set_use_dhcp(use_dhcp == 'y');

        if (use_dhcp == 'n') {
            std::string ip, subnet, gateway, dns;
            std::cout << "Enter IP Address: ";
            std::cin >> ip;
            std::cout << "Enter Subnet Mask: ";
            std::cin >> subnet;
            std::cout << "Enter Default Gateway: ";
            std::cin >> gateway;
            std::cout << "Enter DNS Servers (space-separated, end with '.'): ";
            while (std::cin >> dns && dns != ".") {
                request.add_requested_dns(dns);
            }
            request.set_requested_ip(ip);
            request.set_requested_subnet_mask(subnet);
            request.set_requested_gateway(gateway);
        }

        Status status = network_stub_->ConfigureIP(&context, request, &response);
        if (status.ok()) {
            std::cout << "[CLIENT] Received IP Configuration:\n";
            std::cout << "  IP Address: " << response.ip_address() << "\n";
            std::cout << "  Subnet Mask: " << response.subnet_mask() << "\n";
            std::cout << "  Default Gateway: " << response.default_gateway() << "\n";
            std::cout << "  DNS Servers: ";
            for (const auto& dns : response.dns_servers()) {
                std::cout << dns << " ";
            }
            std::cout << "\n  Status: " << response.status_message() << "\n";
        } else {
            std::cerr << "[CLIENT] IP Configuration Request failed" << std::endl;
        }
    }

    void MakeHttpRequests() {
        httplib::Client cli("http://localhost", 8080);

        auto res = cli.Get("/hi");
        if (res && res->status == 200) {
            std::cout << "[CLIENT] GET /hi Response: " << res->body << std::endl;
        } else {
            std::cerr << "[CLIENT] GET /hi Request failed" << std::endl;
            if (!res) {
                std::cerr << "[CLIENT] HTTP request failed with error: " << httplib::to_string(res.error()) << std::endl;
            } else {
                std::cerr << "[CLIENT] HTTP request failed with status: " << res->status << std::endl;
            }
        }

        auto res2 = cli.Post("/echo", "Hello, Server!", "text/plain");
        if (res2 && res2->status == 200) {
            std::cout << "[CLIENT] POST /echo Response: " << res2->body << std::endl;
        } else {
            std::cerr << "[CLIENT] POST /echo Request failed" << std::endl;
            if (!res2) {
                std::cerr << "[CLIENT] HTTP request failed with error: " << httplib::to_string(res2.error()) << std::endl;
            } else {
                std::cerr << "[CLIENT] HTTP request failed with status: " << res2->status << std::endl;
            }
        }
    }

    void UploadFile(const std::string& filename) {
        myservice::FileUploadRequest request;
        myservice::FileUploadResponse response;

        // Read file content
        std::ifstream infile(filename, std::ios::binary);
        if (!infile) {
            std::cerr << "[CLIENT] Failed to open file: " << filename << "\n";
            return;
        }
        std::ostringstream buffer;
        buffer << infile.rdbuf();

        request.set_filename(filename);
        request.set_content(buffer.str());

        grpc::ClientContext context;
        grpc::Status status = file_stub_->UploadFile(&context, request, &response);

        if (status.ok()) {
            std::cout << "[CLIENT] " << response.message() << "\n";
        } else {
            std::cerr << "[CLIENT] Upload failed: " << status.error_message() << "\n";
        }
    }

    void DownloadFile(const std::string& filename) {
        myservice::FileDownloadRequest request;
        myservice::FileDownloadResponse response;

        request.set_filename(filename);

        grpc::ClientContext context;
        grpc::Status status = file_stub_->DownloadFile(&context, request, &response);

        if (status.ok()) {
            // Save file locally
            std::ofstream outfile(filename, std::ios::binary);
            outfile.write(response.content().data(), response.content().size());
            outfile.close();
            
            std::cout << "[CLIENT] File downloaded successfully: " << filename << "\n";
        } else {
            std::cerr << "[CLIENT] Download failed: " << status.error_message() << "\n";
        }
    }


private:
    std::unique_ptr<Greeter::Stub> greeter_stub_;
    std::unique_ptr<NetworkConfig::Stub> network_stub_;
    std::unique_ptr<myservice::FileService::Stub> file_stub_;
};

void TestHttpEndpoints() {
    std::cout << "\n[CLIENT] Testing HTTP endpoints..." << std::endl;
    
    // Create HTTP client with IP address instead of hostname
    std::cout << "[CLIENT] Creating HTTP client to connect to 127.0.0.1:8080" << std::endl;
    httplib::Client http_cli("127.0.0.1", 8080);
    
    // Set longer timeout for better chance of connection
    http_cli.set_connection_timeout(10); // 10 seconds timeout
    http_cli.set_read_timeout(10, 0);    // 10 seconds read timeout
    
    // Test GET /hi endpoint
    std::cout << "[CLIENT] Sending GET /hi request..." << std::endl;
    if (auto res = http_cli.Get("/hi")) {
        if (res->status == 200) {
            std::cout << "[CLIENT] GET /hi Response: " << res->body << std::endl;
        } else {
            std::cout << "[CLIENT] GET /hi Request failed with status: " << res->status << std::endl;
        }
    } else {
        auto err = res.error();
        std::cout << "[CLIENT] GET /hi Request failed with error: " << httplib::to_string(err) << std::endl;
        std::cout << "[CLIENT] Error code: " << static_cast<int>(err) << std::endl;
    }

    // Test POST /echo endpoint
    std::cout << "[CLIENT] Sending POST /echo request..." << std::endl;
    if (auto res = http_cli.Post("/echo", "Hello from client!", "text/plain")) {
        if (res->status == 200) {
            std::cout << "[CLIENT] POST /echo Response: " << res->body << std::endl;
        } else {
            std::cout << "[CLIENT] POST /echo Request failed with status: " << res->status << std::endl;
        }
    } else {
        auto err = res.error();
        std::cout << "[CLIENT] POST /echo Request failed with error: " << httplib::to_string(err) << std::endl;
        std::cout << "[CLIENT] Error code: " << static_cast<int>(err) << std::endl;
    }
    
    // Test API endpoints
    json hello_request = {
        {"name", "Test User"}
    };
    
    std::cout << "[CLIENT] Sending POST /api/hello request..." << std::endl;
    if (auto res = http_cli.Post("/api/hello", hello_request.dump(), "application/json")) {
        if (res->status == 200) {
            std::cout << "[CLIENT] POST /api/hello Response: " << res->body << std::endl;
        } else {
            std::cout << "[CLIENT] POST /api/hello Request failed with status: " << res->status << std::endl;
        }
    } else {
        auto err = res.error();
        std::cout << "[CLIENT] POST /api/hello Request failed with error: " << httplib::to_string(err) << std::endl;
        std::cout << "[CLIENT] Error code: " << static_cast<int>(err) << std::endl;
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " host:port" << std::endl;
        return 1;
    }

    Client client(grpc::CreateChannel(argv[1], grpc::InsecureChannelCredentials()));

    std::string name;
    std::cout << "Enter your name: ";
    std::cin >> name;
    client.SayHello(name);

    client.ConfigureIP();
    
    // Test HTTP endpoints after gRPC tests
    TestHttpEndpoints();

    return 0;
}