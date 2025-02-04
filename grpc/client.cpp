#include <iostream>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"

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

            Status status = stub_->SayHello(&context, request, &reply);

            if (status.ok()) {
                return reply.message();
            } else {
                return "RPC failed";
            }
        }
    private:
        unique_ptr<Greeter::Stub> stub_;
};

int main() {
    GreeterClient client(grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()));
    string my_name;
    cout << "Enter your name: " << endl;
    cin >> my_name;
    string response = client.SayHello(my_name);
    cout << "Server Response: " << response << endl;
    return 0;
}