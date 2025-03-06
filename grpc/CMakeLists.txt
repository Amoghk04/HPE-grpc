cmake_minimum_required(VERSION 3.16)
project(grpc_http_server)

set(CMAKE_CXX_STANDARD 17)

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpp-httplib CONFIG REQUIRED)

# Generate the gRPC and Protobuf source files
set(PROTO_SRC service.pb.cc service.grpc.pb.cc)

# Add the server executable
add_executable(grpc_http_server server.cpp ${PROTO_SRC})
target_link_libraries(grpc_http_server
    gRPC::grpc++
    protobuf::libprotobuf
    nlohmann_json::nlohmann_json
    httplib::httplib
)

# Add the client executable (if needed)
add_executable(client client.cpp ${PROTO_SRC})
target_link_libraries(client
    gRPC::grpc++
    protobuf::libprotobuf
)