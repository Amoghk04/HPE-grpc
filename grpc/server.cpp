#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <grpcpp/grpcpp.h>
#include "service.grpc.pb.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <mutex>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ClientContext;
using grpc::Channel;
using myservice::Greeter;
using myservice::HelloRequest;
using myservice::HelloReply;
using myservice::NetworkConfig;
using myservice::IPConfigRequest;
using myservice::IPConfigResponse;
using json = nlohmann::json;

// Global storage for our data
std::unordered_map<std::string, json> data_store;
std::mutex data_store_mutex; // For thread safety

// gRPC Greeter Service Implementation
class GreeterServiceImpl final : public Greeter::Service {
public:
    Status SayHello(ServerContext* context, const HelloRequest* request, HelloReply* reply) override {
        std::string response_message = "Hello, " + request->name();
        reply->set_message(response_message);
        std::cout << "[SERVER] Sent Response: " << response_message << std::endl;
        return Status::OK;
    }

    Status SayHelloAgain(ServerContext* context, const HelloRequest* request, HelloReply* reply) override {
        std::string response_message = "Hello again, " + request->name();
        reply->set_message(response_message);
        std::cout << "[SERVER] Sent Response: " << response_message << std::endl;
        return Status::OK;
    }
};

// gRPC Network Configuration Service Implementation
class NetworkConfigServiceImpl final : public NetworkConfig::Service {
public:
    Status ConfigureIP(ServerContext* context, const IPConfigRequest* request, IPConfigResponse* response) override {
        std::cout << "[SERVER] Received ConfigureIP request from client\n";
        std::cout << "[SERVER] Interface: " << request->interface_name() << " | DHCP: " << (request->use_dhcp() ? "Yes" : "No") << std::endl;

        if (request->use_dhcp()) {
            // Simulate DHCP Configuration
            response->set_ip_address("192.168.1.100");
            response->set_subnet_mask("255.255.255.0");
            response->set_default_gateway("192.168.1.1");
            response->add_dns_servers("8.8.8.8");
            response->add_dns_servers("8.8.4.4");
            response->set_status_message("DHCP configuration assigned.");
        } else {
            // Static IP Configuration
            response->set_ip_address(request->requested_ip());
            response->set_subnet_mask(request->requested_subnet_mask());
            response->set_default_gateway(request->requested_gateway());
            for (const auto& dns : request->requested_dns()) {
                response->add_dns_servers(dns);
            }
            response->set_status_message("Static IP configuration assigned.");
        }

        std::cout << "[SERVER] Sent IP configuration response.\n";
        return Status::OK;
    }
};

// Forward declaration of SetupHttpRoutes function
template <typename Server>
void SetupHttpRoutes(Server &server);

// HTTP Server with cpp-httplib
void RunHttpServer() {
    // Create a regular HTTP server
    httplib::Server http_server;

    // Setup routes
    http_server.Get("/hi", [](const httplib::Request&, httplib::Response& res) {
        std::cout << "[SERVER] Received GET /hi request" << std::endl;
        res.set_content("Hello HI from the HTTP server!", "text/plain");
    });

    http_server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::cout << "[SERVER] Received GET / request" << std::endl;
        res.set_content("Hello FROM SERVER !", "text/plain");
    });

    http_server.Post("/echo", [](const httplib::Request& req, httplib::Response& res) {
        std::cout << "[SERVER] Received POST /echo request" << std::endl;
        res.set_content("Echo: " + req.body, "text/plain");
    });

    // Add API endpoints that bridge to gRPC services
    http_server.Post("/api/hello", [](const httplib::Request& req, httplib::Response& res) {
        std::cout << "[SERVER] Received POST /api/hello request" << std::endl;
        try {
            json request_data = json::parse(req.body);
            std::string name = request_data["name"];
            
            // Create gRPC client and make the call
            auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
            std::unique_ptr<Greeter::Stub> stub = Greeter::NewStub(channel);
            
            HelloRequest grpc_req;
            HelloReply grpc_reply;
            ClientContext context;
            
            grpc_req.set_name(name);
            Status status = stub->SayHello(&context, grpc_req, &grpc_reply);
            
            if (status.ok()) {
                json response = {
                    {"message", grpc_reply.message()},
                    {"status", "success"}
                };
                res.set_content(response.dump(), "application/json");
            } else {
                json error = {
                    {"error", "gRPC call failed"},
                    {"status", "error"}
                };
                res.status = 500;
                res.set_content(error.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            json error = {
                {"error", e.what()},
                {"status", "error"}
            };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // GET endpoint to fetch server status
    http_server.Get("/status", [](const httplib::Request&, httplib::Response& res) {
        std::cout << "[SERVER] Received GET /status request" << std::endl;
        json status = {
            {"status", "running"},
            {"timestamp", std::time(nullptr)},
            {"version", "1.0"}
        };
        res.set_content(status.dump(), "application/json");
    });

    // GET endpoint to list all stored data (this must come BEFORE the /data/:id route)
    http_server.Get("/data", [](const httplib::Request&, httplib::Response& res) {
        std::cout << "[SERVER] Received GET /data request" << std::endl;
        
        json response = json::array();
        std::lock_guard<std::mutex> lock(data_store_mutex);
        for (const auto& [id, value] : data_store) {
            response.push_back(value);
        }
        
        res.set_content(response.dump(), "application/json");
    });

    // GET endpoint to retrieve data by ID
    http_server.Get(R"(/data/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        std::cout << "[SERVER] Received GET /data/" << id << " request" << std::endl;
        
        std::lock_guard<std::mutex> lock(data_store_mutex);
        if (data_store.find(id) != data_store.end()) {
            res.set_content(data_store[id].dump(), "application/json");
        } else {
            json error = {
                {"error", "Not found"},
                {"message", "No data found with ID: " + id}
            };
            res.status = 404;
            res.set_content(error.dump(), "application/json");
        }
    });

    // POST endpoint to store data
    http_server.Post("/data", [](const httplib::Request& req, httplib::Response& res) {
        std::cout << "[SERVER] Received POST /data request" << std::endl;
        try {
            json request_data = json::parse(req.body);
            
            if (!request_data.contains("id")) {
                throw std::runtime_error("Data must contain an 'id' field");
            }
            
            std::string id = request_data["id"].get<std::string>();
            
            {
                std::lock_guard<std::mutex> lock(data_store_mutex);
                data_store[id] = request_data;
            }
            
            json response = {
                {"message", "Data stored successfully"},
                {"stored_data", request_data}
            };
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            json error = {
                {"error", "Invalid data"},
                {"details", e.what()}
            };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // GET endpoint with query parameters
    http_server.Get("/query", [](const httplib::Request& req, httplib::Response& res) {
        std::cout << "[SERVER] Received GET /query request" << std::endl;
        json response = {
            {"params", json::object()}
        };
        
        // Add all query parameters to response
        for (const auto& param : req.params) {
            response["params"][param.first] = param.second;
        }
        
        res.set_content(response.dump(), "application/json");
    });

    // PUT endpoint to update data
    http_server.Put(R"(/update/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        std::cout << "[SERVER] Received PUT /update/" << id << " request" << std::endl;
        
        try {
            json update_data = json::parse(req.body);
            
            std::lock_guard<std::mutex> lock(data_store_mutex);
            if (data_store.find(id) != data_store.end()) {
                // Update existing data
                data_store[id].merge_patch(update_data);
                
                json response = {
                    {"message", "Update successful"},
                    {"updated_data", data_store[id]}
                };
                res.set_content(response.dump(), "application/json");
            } else {
                json error = {
                    {"error", "Not found"},
                    {"message", "No data found with ID: " + id}
                };
                res.status = 404;
                res.set_content(error.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            json error = {
                {"error", "Invalid data"},
                {"details", e.what()}
            };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    std::cout << "[SERVER] HTTP Server starting on port 8080...\n";
    if (!http_server.listen("0.0.0.0", 8080)) {
        std::cerr << "[SERVER] ERROR: Failed to start HTTP server on port 8080\n";
    }
}

// gRPC Server
void RunGrpcServer() {
    std::string server_address("0.0.0.0:50051");
    GreeterServiceImpl greeter_service;
    NetworkConfigServiceImpl network_service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&greeter_service);
    builder.RegisterService(&network_service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "[SERVER] gRPC Server Listening on " << server_address << " without SSL\n";
    server->Wait();
}

int main() {
    std::cout << "[SERVER] Starting gRPC and HTTP servers...\n";
    
    // Run gRPC Server in a separate thread
    std::thread grpc_thread(RunGrpcServer);
    
    // Run HTTP Server in the main thread to ensure it doesn't terminate
    RunHttpServer();
    
    // This code will only be reached if the HTTP server fails to start
    // Wait for gRPC thread to complete
    grpc_thread.join();
    
    return 0;
}
