cmake_minimum_required(VERSION 3.10)
project(grpc_example)

# Set C++17 as required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Protobuf CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(absl REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# Open ssl vars for macos only (since not running in my sys)
if(APPLE)
    set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl)
    set(OPENSSL_LIBRARIES ${OPENSSL_ROOT_DIR}/lib)
    set(OPENSSL_INCLUDE_DIR ${OPENSSL_ROOT_DIR}/include)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
    link_libraries(stdc++fs)
endif()

# Find or download nlohmann_json
include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Add include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${GRPC_INCLUDE_DIRS})
include_directories(${PROTOBUF_INCLUDE_DIRS})
include_directories(/usr/include)

# Download cpp-httplib if not exists
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/httplib.h)
    file(DOWNLOAD
        https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h
        ${CMAKE_CURRENT_SOURCE_DIR}/httplib.h
        SHOW_PROGRESS
    )
endif()

# Add the proto library
add_library(proto
    service.grpc.pb.cc
    service.pb.cc
)

target_link_libraries(proto
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    gRPC::grpc++
    gRPC::grpc
    gRPC::gpr
    protobuf::libprotobuf
    absl::synchronization
    absl::time
    absl::strings
    absl::str_format
    absl::base
    absl::log
    absl::log_internal_message
    absl::log_internal_check_op
    absl::log_internal_log_sink_set
    absl::log_internal_conditions
    absl::log_internal_format
    absl::log_internal_globals
    absl::log_internal_proto
    absl::log_severity
    absl::cord
    absl::hash
    absl::btree
    pthread
)

# Server executable
add_executable(server
    server.cpp
)

target_link_libraries(server
    proto
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    gRPC::grpc++
    gRPC::grpc
    gRPC::gpr
    protobuf::libprotobuf
    absl::synchronization
    absl::time
    absl::strings
    absl::str_format
    absl::base
    absl::log
    absl::log_internal_message
    absl::log_internal_check_op
    absl::log_internal_log_sink_set
    absl::log_internal_conditions
    absl::log_internal_format
    absl::log_internal_globals
    absl::log_internal_proto
    absl::log_severity
    absl::cord
    absl::hash
    absl::btree
    pthread
    nlohmann_json::nlohmann_json
)

# Client executable
add_executable(client
    client.cpp
)

target_link_libraries(client
    proto
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    gRPC::grpc++
    gRPC::grpc
    gRPC::gpr
    protobuf::libprotobuf
    absl::synchronization
    absl::time
    absl::strings
    absl::str_format
    absl::base
    absl::log
    absl::log_internal_message
    absl::log_internal_check_op
    absl::log_internal_log_sink_set
    absl::log_internal_conditions
    absl::log_internal_format
    absl::log_internal_globals
    absl::log_internal_proto
    absl::log_severity
    absl::cord
    absl::hash
    absl::btree
    pthread
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Add consistency test client executable
add_executable(consistency_test_client
    consistency_test_client.cpp
)

target_link_libraries(consistency_test_client
    proto
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    gRPC::grpc++
    gRPC::grpc
    gRPC::gpr
    protobuf::libprotobuf
    absl::synchronization
    absl::time
    absl::strings
    absl::str_format
    absl::base
    absl::log
    absl::log_internal_message
    absl::log_internal_check_op
    absl::log_internal_log_sink_set
    absl::log_internal_conditions
    absl::log_internal_format
    absl::log_internal_globals
    absl::log_internal_proto
    absl::log_severity
    absl::cord
    absl::hash
    absl::btree
    pthread
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_executable(benchmark_client
    benchmark_client.cpp
)

target_link_libraries(benchmark_client
    proto
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    gRPC::grpc++
    gRPC::grpc
    gRPC::gpr
    protobuf::libprotobuf
    absl::synchronization
    absl::time
    absl::strings
    absl::str_format
    absl::base
    absl::log
    absl::log_internal_message
    absl::log_internal_check_op
    absl::log_internal_log_sink_set
    absl::log_internal_conditions
    absl::log_internal_format
    absl::log_internal_globals
    absl::log_internal_proto
    absl::log_severity
    absl::cord
    absl::hash
    absl::btree
    pthread
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)