#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"
#include <fstream>
#include <csignal>
#include <thread>
#include <atomic>
#include <chrono>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using namespace std;
using namespace myservice;

unique_ptr<Server> g_server = nullptr;
atomic<bool> shutdown_requested(false);

void SignalHandler(int signum) {

    cout << "\n[SERVER] Caught signal (" << signum << "). Shutting down..." << endl;
    shutdown_requested.store(true);
    cout << "[SERVER] Server shut down successfully." << endl;
    
}

class GreeterServiceImpl final : public myservice::Greeter::Service {
    public:
        Status SayHello(ServerContext* context, const HelloRequest* request, HelloReply* reply) override {
            string client_peer = context->peer();
            cout << "[SERVER] Received request from: " << client_peer << endl;

            string greeting = "Hello, " + request->name();
            reply->set_message(greeting);

            cout << "[SERVER] Sent Response: " << greeting << endl;
            return Status::OK;
        }

        Status SayHelloAgain(ServerContext* context, const HelloRequest* request, HelloReply* reply) {
            string client_peer = context->peer();
            cout << "[SERVER] Received request from: " << client_peer << endl;

            string greeting = "Hello again, " + request->name();
            reply->set_message(greeting);

            cout << "[SERVER] Sent Response: " << greeting << endl;
            return Status::OK;
        }
};

// The new service implementation for network config.
class NetworkConfigServiceImpl final : public NetworkConfig::Service {
    public:
    Status ConfigureIP(ServerContext* context, const IPConfigRequest* request, IPConfigResponse* reply) override {
        string client_peer = context->peer();
        cout << "[SERVER] Received ConfigureIP request from: " << client_peer << endl;
        cout << "[SERVER] Interface: " << request->interface_name() 
             << " | DHCP: " << (request->use_dhcp() ? "Yes" : "No") << endl;
        
        // For demonstration, if DHCP is requested, return dummy DHCP settings.
        if (request->use_dhcp()) {
            reply->set_ip_address("192.168.1.100");
            reply->set_subnet_mask("255.255.255.0");
            reply->set_default_gateway("192.168.1.1");
            reply->add_dns_servers("8.8.8.8");
            reply->add_dns_servers("8.8.4.4");
            reply->set_status_message("DHCP configuration assigned.");
        } else {
            // If static, echo back the requested settings or modify as needed.
            reply->set_ip_address(request->requested_ip());
            reply->set_subnet_mask(request->requested_subnet_mask());
            reply->set_default_gateway(request->requested_gateway());
            for (const auto& dns : request->requested_dns())
                reply->add_dns_servers(dns);
            reply->set_status_message("Static configuration assigned.");
        }

        cout << "[SERVER] Sent IP configuration response." << endl;
        return Status::OK;
    }
};

string read_file(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "Error: Could not open file " << filename << endl;
        exit(1);
    }
    return string((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
}

void RunServer() {
    string server_address("0.0.0.0:50051");

    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    string server_cert = read_file("server.crt");
    string server_key = read_file("server.key");
    string root_cert = read_file("ca.crt");

    cout << "[SERVER] Loaded SSL certificates successfully!" << endl;

    grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert = {server_key, server_cert};
    grpc::SslServerCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = root_cert;
    ssl_opts.pem_key_cert_pairs.push_back(key_cert);
    ssl_opts.client_certificate_request = GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;

    auto server_creds = grpc::SslServerCredentials(ssl_opts);

    GreeterServiceImpl greeter_service;
    NetworkConfigServiceImpl network_config_service;
    ServerBuilder builder;
    builder.AddListeningPort(server_address, server_creds);
    builder.RegisterService(&greeter_service);
    builder.RegisterService(&network_config_service);

    g_server = builder.BuildAndStart();
    cout << "[SERVER] Secure gRPC Server Listening on " << server_address << " with SSL encryption" << endl;

    std::thread shutdown_thread([](){
        while (!shutdown_requested.load()) {
            this_thread::sleep_for(chrono::milliseconds(1));
        }
        if (g_server) {
            cout << "[SERVER] Shutting down server as requested." << endl;
            g_server->Shutdown();
        }
    });

    g_server->Wait();
    shutdown_thread.join();
    g_server.reset();
}

int main() {
    RunServer();
    return 0;
}