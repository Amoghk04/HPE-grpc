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
#include <iomanip>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using myservice::FileService;
using myservice::FileUploadRequest;
using myservice::FileUploadResponse;


struct VVolConfig {
    std::string vvol_id;
    std::string storage_container;
    std::string storage_profile;
    size_t size_gb;
    std::string protocol;
    std::string qos_profile;
    bool thin_provisioned;
    std::vector<std::string> host_initiators;
    std::string replication_state;
};


VVolConfig generateRandomVVolConfig(int client_id, int write_num) {
    static const std::vector<std::string> storage_containers = {
        "HPE_3PAR_SC1", "HPE_3PAR_SC2", "HPE_Primera_SC1", "HPE_Nimble_SC1"
    };
    static const std::vector<std::string> storage_profiles = {
        "Gold_SSD", "Silver_FC", "Bronze_NL", "Platinum_NVMe"
    };
    static const std::vector<std::string> protocols = {
        "FC", "iSCSI", "FCoE", "NVMeOF"
    };
    static const std::vector<std::string> qos_profiles = {
        "High_Priority", "Medium_Priority", "Low_Priority", "Custom"
    };
    static const std::vector<std::string> replication_states = {
        "Synchronized", "Syncing", "Failed", "Paused"
    };

    std::random_device rd;
    std::mt19937 gen(rd());

    VVolConfig config;
    config.vvol_id = "VVOL_" + std::to_string(client_id) + "_" + std::to_string(write_num);
    config.storage_container = storage_containers[gen() % storage_containers.size()];
    config.storage_profile = storage_profiles[gen() % storage_profiles.size()];
    config.size_gb = (gen() % 1000) + 100;  
    config.protocol = protocols[gen() % protocols.size()];
    config.qos_profile = qos_profiles[gen() % qos_profiles.size()];
    config.thin_provisioned = (gen() % 2) == 0;
    
    
    int num_initiators = (gen() % 3) + 1;
    for (int i = 0; i < num_initiators; i++) {
        std::stringstream initiator;
        initiator << std::hex << std::setfill('0') << std::setw(16) << (gen() % 0xFFFFFFFFFFFFFFFF);
        config.host_initiators.push_back("20:" + initiator.str());
    }
    
    config.replication_state = replication_states[gen() % replication_states.size()];
    return config;
}

// Convert VVOL config to JSON-like string
std::string vvolConfigToString(const VVolConfig& config) {
    std::stringstream ss;
    ss << "{\n"
       << "  \"vvol_id\": \"" << config.vvol_id << "\",\n"
       << "  \"storage_container\": \"" << config.storage_container << "\",\n"
       << "  \"storage_profile\": \"" << config.storage_profile << "\",\n"
       << "  \"size_gb\": " << config.size_gb << ",\n"
       << "  \"protocol\": \"" << config.protocol << "\",\n"
       << "  \"qos_profile\": \"" << config.qos_profile << "\",\n"
       << "  \"thin_provisioned\": " << (config.thin_provisioned ? "true" : "false") << ",\n"
       << "  \"host_initiators\": [\n";
    
    for (size_t i = 0; i < config.host_initiators.size(); ++i) {
        ss << "    \"" << config.host_initiators[i] << "\"";
        if (i < config.host_initiators.size() - 1) ss << ",";
        ss << "\n";
    }
    
    ss << "  ],\n"
       << "  \"replication_state\": \"" << config.replication_state << "\"\n"
       << "}";
    return ss.str();
}

std::string read_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    std::string content((std::istreambuf_iterator<char>(file)), 
                        std::istreambuf_iterator<char>());
    return content;
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
    
            // Add metadata for client identification
            context.AddMetadata("client-id", std::to_string(client_id));
            
            Status status = stub_->UploadFile(&context, request, &response);
    
            if (status.ok()) {
                if (response.success()) {
                    std::cout << "Client " << client_id << " wrote VVOL config successfully to "
                             << response.filepath() << std::endl;
                    return true;
                } else {
                    std::cerr << "Client " << client_id << " write failed: " 
                             << response.message() << std::endl;
                    return false;
                }
            } else {
                std::cerr << "Client " << client_id << " RPC failed: " 
                         << status.error_message() << std::endl;
                return false;
            }
        }
    
private:
    std::unique_ptr<FileService::Stub> stub_;
};

void RunClientThread(ConsistencyTestClient* client, const std::string& filename, int client_id, int num_writes) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> delay(100, 500); 

    for (int i = 0; i < num_writes; i++) {
       
        VVolConfig config = generateRandomVVolConfig(client_id, i);
        
       
        std::stringstream content;
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
        
        content << "[" << timestamp << "] VVOL Configuration Update:\n" 
                << vvolConfigToString(config) << "\n\n";
        
        client->WriteToFile(filename, content.str(), client_id);
        
       
        std::this_thread::sleep_for(std::chrono::milliseconds(delay(gen)));
    }
}

int main() {
    try {
        // Load SSL credentials
        std::string server_key = read_file("../../certs/server.key");
        std::string server_cert = read_file("../../certs/server.crt");
        std::string ca_cert = read_file("../../certs/ca.crt");

        grpc::SslCredentialsOptions ssl_opts;
        ssl_opts.pem_root_certs = ca_cert;
        ssl_opts.pem_private_key = server_key;
        ssl_opts.pem_cert_chain = server_cert;
        
        auto channel_creds = grpc::SslCredentials(ssl_opts);
        auto channel = grpc::CreateChannel("localhost:50051", channel_creds);

        const int NUM_CLIENTS = 50;
        const int WRITES_PER_CLIENT = 100;
        const std::string TEST_FILENAME = "vvol_config_test.txt";
        
        std::vector<std::unique_ptr<ConsistencyTestClient>> clients;
        std::vector<std::thread> threads;
        
        std::cout << "Starting VVOL configuration consistency test with " << NUM_CLIENTS 
                 << " clients, each performing " << WRITES_PER_CLIENT 
                 << " configuration updates..." << std::endl;

        // Ensure uploads directory exists
        // if (!std::filesystem::exists("uploads")) {
        //     std::filesystem::create_directory("uploads");
        // }

        for (int i = 0; i < NUM_CLIENTS; i++) {
            clients.push_back(std::make_unique<ConsistencyTestClient>(channel));
            threads.emplace_back(RunClientThread, clients[i].get(), 
                               TEST_FILENAME, i, WRITES_PER_CLIENT);
        }

        for (auto& thread : threads) {
            thread.join();
        }

        std::cout << "All clients finished writing VVOL configurations. Check " 
                 << TEST_FILENAME << " in the uploads directory to verify consistency." 
                 << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}