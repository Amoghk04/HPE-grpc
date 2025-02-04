#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using namespace std;
using namespace myservice;

class GreeterServiceImpl final : public myservice::Greeter::Service {
    public:
        Status SayHello(ServerContext* context, const HelloRequest* request, HelloReply* reply) {
            string greeting = "Hello, " + request->name();
            reply->set_message(greeting);
            return Status::OK;
        }
};

void RunServer() {
    string server_address("0.0.0.0:50051");
    GreeterServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    unique_ptr<Server> server(builder.BuildAndStart());
    cout << "Server Listening on " << server_address << endl;

    server->Wait();
}

int main() {
    RunServer();
    return 0;
}