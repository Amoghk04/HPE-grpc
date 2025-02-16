#include <iostream>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"
#include <fstream>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using namespace std;
using namespace myservice;

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
                return reply.message();
            } else {
                cerr << "[CLIENT] RPC failed: " << status.error_message() << endl;
                return "RPC failed";
            }
        }

        string SayHelloAgain(const string& name) {
            HelloRequest request;
            request.set_name(name);
            HelloReply reply;
            ClientContext context;

            cout << "[CLIENT] Sending another request to server: " << name << endl;
            Status again = stub_->SayHelloAgain(&context, request, &reply);

            if (again.ok()) {
                return reply.message();
            } else {
                cerr << "[CLIENT] RPC failed: " << again.error_message() << endl; 
                return "RPC failed";
            }
        }
    private:
        unique_ptr<Greeter::Stub> stub_;
};

class NetworkConfigClient {
public:
    NetworkConfigClient(shared_ptr<Channel> channel) : stub_(NetworkConfig::NewStub(channel)) {}

    // Send configuration request and receive an IP configuration response.
    IPConfigResponse ConfigureIP(const IPConfigRequest& request) {
        IPConfigResponse reply;
        ClientContext context;

        cout << "[CLIENT] Sending ConfigureIP request..." << endl;
        Status status = stub_->ConfigureIP(&context, request, &reply);

        if (!status.ok()) {
            cerr << "[CLIENT] RPC failed: " << status.error_message() << endl;
            exit(1);
        }
        return reply;
    }
private:
    unique_ptr<NetworkConfig::Stub> stub_;
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

    GreeterClient greeter_client(channel);
    NetworkConfigClient network_client(channel);

    string my_name;
    cout << "Enter your name: " << endl;
    cin >> my_name;
    
    string response = greeter_client.SayHello(my_name);
    string again = greeter_client.SayHelloAgain(my_name);
    cout << "[CLIENT] Final Hello Response: " << response << endl;
    cout << "[CLIENT] Final Hello Again Response: " << again << endl;

    IPConfigRequest request;
    string interface_name;
    char dhcp_choice;

    cout << "Enter network interface name: " << endl;
    cin >> interface_name;
    request.set_interface_name(interface_name);

    cout << "Use DHCP? (y/n): " << endl;
    cin >> dhcp_choice;
    if(dhcp_choice == 'y' || dhcp_choice == 'Y') {
        request.set_use_dhcp(true);
    } else {
        request.set_use_dhcp(false);
        string ip, subnet, gateway, dns;
        cout << "Enter desired IP address: " << endl;
        cin >> ip;
        cout << "Enter desired subnet mask: " << endl;
        cin >> subnet;
        cout << "Enter desired default gateway: " << endl;
        cin >> gateway;
        cout << "Enter desired DNS server (enter one at a time, type 'done' when finished): " << endl;
        while(cin >> dns && dns != "done") {
            request.add_requested_dns(dns);
            cout << "Enter another DNS server or 'done': " << endl;
        }
        request.set_requested_ip(ip);
        request.set_requested_subnet_mask(subnet);
        request.set_requested_gateway(gateway);
    }

    IPConfigResponse ip_response = network_client.ConfigureIP(request);
    cout << "[CLIENT] Received IP Configuration:" << endl;
    cout << "IP Address: " << ip_response.ip_address() << endl;
    cout << "Subnet Mask: " << ip_response.subnet_mask() << endl;
    cout << "Default Gateway: " << ip_response.default_gateway() << endl;
    cout << "DNS Servers: ";
    for (const auto &dns : ip_response.dns_servers())
        cout << dns << " ";
    cout << endl;
    cout << "Status: " << ip_response.status_message() << endl;

    return 0;
}