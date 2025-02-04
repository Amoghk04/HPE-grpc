cmake_minimum_required(VERSION 3.28)
project(gRPCExample CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)

add_executable(server server.cpp service.pb.cc service.grpc.pb.cc)
target_link_libraries(server gRPC::grpc++ protobuf::libprotobuf)

add_executable(client client.cpp service.pb.cc service.grpc.pb.cc)
target_link_libraries(client gRPC::grpc++ protobuf::libprotobuf)
