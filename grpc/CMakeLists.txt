cmake_minimum_required(VERSION 3.16)
project(grpc_http_server)

set(CMAKE_CXX_STANDARD 17)

# Add the vcpkg toolchain file
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

include_directories(${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

# Define the .proto files
set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/service.proto)

# Generate Protobuf and gRPC files
protobuf_generate(
    LANGUAGE cpp
    GENERATE_EXTENSIONS .pb.h .pb.cc
    OUT_VAR PROTO_SRCS
    PROTOS ${PROTO_FILES}
)
protobuf_generate(
    LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    OUT_VAR GRPC_SRCS
    PLUGIN "protoc-gen-grpc=C:/vcpkg/installed/x64-windows/tools/grpc/grpc_cpp_plugin.exe"
    PROTOS ${PROTO_FILES}
)

# Add the server executable
add_executable(grpc_http_server server.cpp ${PROTO_SRCS} ${GRPC_SRCS})
target_link_libraries(grpc_http_server
    gRPC::grpc++
    protobuf::libprotobuf
    nlohmann_json::nlohmann_json
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)

# Add the client executable
add_executable(client client.cpp ${PROTO_SRCS} ${GRPC_SRCS})
target_link_libraries(client
    gRPC::grpc++
    protobuf::libprotobuf
    nlohmann_json::nlohmann_json
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)