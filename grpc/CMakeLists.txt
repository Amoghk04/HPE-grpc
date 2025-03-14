cmake_minimum_required(VERSION 3.10)
project(grpc_example)

# Set C++17 as required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(absl REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

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
    absl::synchronization
    absl::time
    absl::strings
    absl::base
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
    absl::synchronization
    absl::time
    absl::strings
    absl::base
    pthread
)

# Client executable
add_executable(client
    client.cpp
)

target_link_libraries(client
    proto
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    absl::synchronization
    absl::time
    absl::strings
    absl::base
    pthread
)