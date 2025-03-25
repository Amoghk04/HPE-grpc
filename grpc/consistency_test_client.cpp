#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"
#include <fstream>
#include <sstream>
#include <random>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using myservice::FileService;
using myservice::FileUploadRequest;
using myservice::FileUploadResponse;

// Helper function to read file contents
std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

class ConsistencyTestClient {
public:
    ConsistencyTestClient(std::shared_ptr<Channel> channel) 
        : stub_(FileService::NewStub(channel)) {}

    bool WriteToFile(const std::string& filename, const std::string& content, int client_id) {
        FileUploadRequest request;
        request.set_filename(filename);
        request.set_content(content);

        FileUploadResponse response;
        ClientContext context;
        Status status = stub_->UploadFile(&context, request, &response);

        if (status.ok()) {
            std::cout << "Client " << client_id << " wrote successfully: " << content << std::endl;
            return true;
        } else {
            std::cerr << "Client " << client_id << " write failed: " << status.error_message() << std::endl;
            return false;
        }
    }

private:
    std::unique_ptr<FileService::Stub> stub_;
};

void RunClientThread(ConsistencyTestClient* client, const std::string& filename, int client_id, int num_writes) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> delay(50, 200);  // Random delay between 50-200ms

    for (int i = 0; i < num_writes; i++) {
        // Create a unique message for this write
        std::stringstream content;
        content << "Client " << client_id << " Write " << i << " Timestamp " << 
                std::chrono::system_clock::now().time_since_epoch().count() << "\n";
        
        client->WriteToFile(filename, content.str(), client_id);
        
        // Random delay between writes
        std::this_thread::sleep_for(std::chrono::milliseconds(delay(gen)));
    }
}

int main() {
    // Create SSL credentials for gRPC
    grpc::SslCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = read_file("../certs/server.crt");
    
    auto channel_creds = grpc::SslCredentials(ssl_opts);
    auto channel = grpc::CreateChannel("localhost:50052", channel_creds);

    // Test parameters
    const int NUM_CLIENTS = 5;
    const int WRITES_PER_CLIENT = 10;
    const std::string TEST_FILENAME = "consistency_test.txt";
    
    // Create clients and their threads
    std::vector<std::unique_ptr<ConsistencyTestClient>> clients;
    std::vector<std::thread> threads;
    
    std::cout << "Starting consistency test with " << NUM_CLIENTS << 
              " clients, each performing " << WRITES_PER_CLIENT << " writes..." << std::endl;

    // Create and start client threads
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients.push_back(std::make_unique<ConsistencyTestClient>(channel));
        threads.emplace_back(RunClientThread, clients[i].get(), TEST_FILENAME, i, WRITES_PER_CLIENT);
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    std::cout << "All clients finished writing. Check " << TEST_FILENAME << 
              " in the uploads directory to verify consistency." << std::endl;

    return 0;
} 