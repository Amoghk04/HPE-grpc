cmake_minimum_required(VERSION 3.16)
project(gRPC_Https_Server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set vcpkg toolchain file (adjust path if needed)
set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

# Find dependencies
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
# find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)

# Generate protobuf files
set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/service.proto")
set(PROTO_SRC_DIR "${CMAKE_CURRENT_BINARY_DIR}")

add_custom_command(
    OUTPUT 
        "${PROTO_SRC_DIR}/service.pb.cc"
        "${PROTO_SRC_DIR}/service.pb.h"
        "${PROTO_SRC_DIR}/service.grpc.pb.cc"
        "${PROTO_SRC_DIR}/service.grpc.pb.h"
    COMMAND protoc
    ARGS 
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
        --cpp_out=${PROTO_SRC_DIR}
        --grpc_out=${PROTO_SRC_DIR}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
)

# Manually include the cpp-httplib header file
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${PROTO_SRC_DIR})

# Create a custom target for the generated files
add_custom_target(generate_protos
    DEPENDS
        "${PROTO_SRC_DIR}/service.pb.cc"
        "${PROTO_SRC_DIR}/service.pb.h"
        "${PROTO_SRC_DIR}/service.grpc.pb.cc"
        "${PROTO_SRC_DIR}/service.grpc.pb.h"
)

# Compile gRPC server
add_executable(server 
    server.cpp
    ${PROTO_SRC_DIR}/service.pb.cc
    ${PROTO_SRC_DIR}/service.grpc.pb.cc
)
add_dependencies(server generate_protos)
target_include_directories(server PRIVATE ${Protobuf_INCLUDE_DIRS})
target_link_libraries(server PRIVATE
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc++_reflection
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    httplib::httplib
)

# Compile gRPC client
add_executable(client 
    client.cpp
    ${PROTO_SRC_DIR}/service.pb.cc
    ${PROTO_SRC_DIR}/service.grpc.pb.cc
)
add_dependencies(client generate_protos)
target_include_directories(client PRIVATE ${Protobuf_INCLUDE_DIRS})
target_link_libraries(client PRIVATE
    protobuf::libprotobuf
    gRPC::grpc++
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    httplib::httplib
)

# Output messages
message(STATUS "Building gRPC and HTTP server with vcpkg dependencies")

# Add these lines instead
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Then in the target_link_libraries, replace nlohmann_json::nlohmann_json with:
# For server
target_link_libraries(server PRIVATE
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc++_reflection
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)

# For client
target_link_libraries(client PRIVATE
    protobuf::libprotobuf
    gRPC::grpc++
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)

# Add this after the existing FetchContent for json
FetchContent_Declare(
    cpp_httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.14.1.tar.gz
)
FetchContent_MakeAvailable(cpp_httplib)

# Update the server target to include cpp-httplib
target_link_libraries(server PRIVATE
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc++_reflection
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    httplib::httplib
)

# Update the client target to include cpp-httplib
target_link_libraries(client PRIVATE
    protobuf::libprotobuf
    gRPC::grpc++
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    httplib::httplib
)