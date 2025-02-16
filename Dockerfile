# Build stage
FROM ubuntu:22.04 AS builder

# Install build dependencies for gRPC and Protobuf
RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    autoconf \
    libtool \
    git \
    cmake \
    pkg-config \
    libssl-dev \
    libprotobuf-dev \
    protobuf-compiler \
    libgrpc++-dev \
    && rm -rf /var/lib/apt/lists/*

# Clone and build gRPC (with its bundled protobuf) as shared libraries.
RUN git clone --recurse-submodules -b v1.70.1 https://github.com/grpc/grpc && \
    cd grpc && \
    mkdir -p cmake/build && cd cmake/build && \
    cmake -DgRPC_INSTALL=ON \
          -DgRPC_BUILD_TESTS=OFF \
          -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_SHARED_LIBS=ON \
          ../.. && \
    make -j$(nproc) && make install

# (Optional) Remove the following line so that you don't reinstall conflicting packages.
# RUN apt install -y protobuf-compiler libprotobuf-dev libgrpc++-dev

# Set working directory
WORKDIR /app
ENV LD_LIBRARY_PATH=/usr/local/lib
# Copy the entire codebase into the container.
COPY . .

# Build the protobuf files, remove any previous build directory, then rebuild.

RUN protoc --proto_path=grpc/ --cpp_out=grpc/ --grpc_out=grpc/ --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) grpc/service.proto \
    && rm -rf grpc/build \
    && mkdir -p grpc/build \
    && cd grpc/build \
    && cmake .. \
    && make -j$(nproc)

# Runtime stage
FROM ubuntu:22.04

# (Optional) Install SSL libraries if needed
RUN apt-get update && apt-get install -y libssl-dev && rm -rf /var/lib/apt/lists/*

# Copy shared libraries from builder stage
COPY --from=builder /usr/local/lib /usr/local/lib
ENV LD_LIBRARY_PATH=/usr/local/lib

WORKDIR /app

# Copy the built executables from the builder stage (adjust paths if needed)
COPY --from=builder /app/grpc/build/server /app/server
COPY --from=builder /app/grpc/build/client /app/client

# Copy required SSL certificates and keys
COPY --from=builder /app/ca.crt /app/ca.crt
COPY --from=builder /app/client.crt /app/client.crt
COPY --from=builder /app/client.key /app/client.key
COPY --from=builder /app/server.crt /app/server.crt
COPY --from=builder /app/server.key /app/server.key

# Expose the gRPC server port
EXPOSE 50051

# Set default command to run the server.
CMD ["/app/server"]
