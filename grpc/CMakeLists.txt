cmake_minimum_required(VERSION 3.16)
project(gRPC_Https_Server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set vcpkg toolchain file (adjust path if needed)
set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

# Find dependencies
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)

# Manually include the cpp-httplib header file
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Manually specify the generated source files
set(PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/service.pb.cc)
set(GRPC_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/service.grpc.pb.cc)

# Compile gRPC server
add_executable(server server.cpp ${PROTO_SRCS} ${GRPC_SRCS})
target_include_directories(server PRIVATE ${Protobuf_INCLUDE_DIRS})
target_link_libraries(server PRIVATE
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc++_reflection
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)

# Compile gRPC client
add_executable(client client.cpp ${PROTO_SRCS} ${GRPC_SRCS})
target_include_directories(client PRIVATE ${Protobuf_INCLUDE_DIRS})
target_link_libraries(client PRIVATE
    protobuf::libprotobuf
    gRPC::grpc++
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)

# Output messages
message(STATUS "Building gRPC and HTTP server with vcpkg dependencies")