#include <iostream>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"
#include <fstream>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using myservice::Greeter;
using myservice::HelloRequest;
using myservice::HelloReply;
using namespace std;

class GreeterClient {
    public:
        GreeterClient(shared_ptr<Channel> channel) : stub_(Greeter::NewStub(channel)) {}

        string SayHello(const string& name) {
            HelloRequest request;
            request.set_name(name);
            HelloReply reply;
            ClientContext context;

            cout << "[CLIENT] Sending request to server: " << name << endl;
            Status status = stub_->SayHello(&context, request, &reply);

            if (status.ok()) {
                cout << "[CLIENT] Server response received: " << reply.message() << endl;
                return reply.message();
            } else {
                cerr << "[CLIENT] RPC failed: " << status.error_message() << endl;
                return "RPC failed";
            }
        }
    private:
        unique_ptr<Greeter::Stub> stub_;
};

string read_file(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "[CLIENT] Error: Could not open file " << filename <<endl;
        exit(1);
    }
    return string((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
}

int main() {
    string root_cert = read_file("ca.crt");
    string client_cert = read_file("client.crt");
    string client_key = read_file("client.key");

    cout << "[CLIENT] Loaded SSL certificates successfully!" << endl;

    grpc::SslCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = root_cert;
    ssl_opts.pem_private_key = client_key;
    ssl_opts.pem_cert_chain = client_cert;

    auto channel_creds = grpc::SslCredentials(ssl_opts);
    auto channel = grpc::CreateChannel("localhost:50051", channel_creds);
    cout << "[CLIENT] Connected to server using SSL🚀" << endl;

    GreeterClient client(channel);

    string my_name;
    cout << "Enter your name: " << endl;
    cin >> my_name;
    
    string response = client.SayHello(my_name);
    cout << "[CLIENT] Final Response: " << response << endl;
    
    return 0;
}