#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"
#include <fstream>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using namespace std;
using namespace myservice;

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

    GreeterServiceImpl service;
    ServerBuilder builder;
    builder.AddListeningPort(server_address, server_creds);
    builder.RegisterService(&service);

    unique_ptr<Server> server(builder.BuildAndStart());
    cout << "[SERVER] Secure gRPC Server Listening on " << server_address << " with SSL encryption" << endl;

    server->Wait();
}

int main() {
    RunServer();
    return 0;
}