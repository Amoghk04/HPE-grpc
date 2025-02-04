# HPE-grpc
HPE Career Preview Program Repository for gRPC configuration for VASA/vVols

# Files
- grpc/server.cpp -> The main grpc server file
- grpc/client.cpp -> The main grpc client file
- grpc/service.proto -> The proto file for grpc
- grpc/CMakeLists.txt -> CMake file for building the application


# Installation and Running Steps
It is made to run only on Linux systems, so make sure to have a Linux VM or Linux OS on your system.

### Run the following commands
Clone the repo
```console 
git clone https://github.com/Amoghk04/HPE-grpc.git
```

Make sure you have downloaded the protobuf and grpc modules beforehand, else run the below commands
```console 
sudo apt update
sudo apt install -y cmake g++ autoconf libtool pkg-config
git clone --recurse-submodules -b v1.70.1 https://github.com/grpc/grpc
cd grpc
mkdir -p cmake/build
cd cmake/build
cmake -DgRPC_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr/local ../..
make -j$(nproc)
sudo make install
```
After the modules are installed, confirm you have the latest version of protobuf
```console 
protoc --version
```
It must be 3.21.12(at the time of writing this readme file), if it is not then run the commands

```console 
sudo apt update
sudo apt install -y protobuf-compiler libprotobuf-dev libgrpc++-dev
```
Now build the proto file by going inside the grpc directory and typing the command
```console 
protoc --proto_path=. --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` service.proto
```
This generates
service.pb.h and service.pb.cc (Protobuf messages)
service.grpc.pb.h and service.grpc.pb.cc (gRPC stubs)

Now create a build directory inside /grpc and create the CMake files
```console
mkdir build
cd build
cmake ..
```

After the build is finished, run the below command
```console
make -j$(nproc)
```

Now your server and client are ready to talk via gRPC, start by opening 2 terminals inside the /gRPC/build directory and running
```console
./server 
```
and
```console
./client
```
