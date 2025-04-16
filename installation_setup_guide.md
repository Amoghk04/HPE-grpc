# HPE-gRPC Installation and Setup Guide

##### Highly recommend using Stackoverflow and ChatGPT to debug if things north for debugging.

### Installing gRPC and Protobufs

```
sudo apt update
sudo apt install -y cmake g++ autoconf libtool pkg-config nlohmann-json3-dev
git clone --recurse-submodules -b v1.70.1 https://github.com/grpc/grpc
cd grpc
mkdir -p cmake/build
cd cmake/build
cmake -DgRPC_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr/local ../..
make -j$(nproc)
sudo make install
```

```
protoc --version
```
- It must be 3.21.12(at the time of writing this readme file), if it is not then run the commands

```
sudo apt update
sudo apt install -y protobuf-compiler libprotobuf-dev libgrpc++-dev
```


### Cloning the repository

```
git clone https://github.com/Amoghk04/HPE-grpc.git
```

```
cd grpc/
protoc --proto_path=. --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` service.proto
```

Run from the main root of the project
```
cd ..
source certs.sh
```

```
mkdir build
cd build
cmake ..

make -j$(nproc)

./server 
./client
```

### Certificates setup

run this from the root of the project directory if theres a certifcates fatal error
```
source certs.sh


### Envoy Proxy Installation and Setup

----------------------------------------------------------------------------------------------------------------------------------------
3. Download and install envoy - https://www.envoyproxy.io/docs/envoy/latest/start/start

----------------------------------------------------------------------------------------------------------------------------------------
4. Generate the proto files (for both server and webUI)
``` npm install -g protoc-gen-grpc-web
```

for the grpc server side follow the original protoc command in the readme file
for the webUI run the below command

```
npm install google-protobuf
npm install protoc-gen-grpc-web

protoc -I=. \
    --js_out=import_style=commonjs:../web/src \
    --grpc-web_out=import_style=commonjs,mode=grpcwebtext:../web/src \
    service.proto
```
----------------------------------------------------------------------------------------------------------------------------------------
5. run these commands inside the web directory
```
npm install --save google-protobuf grpc-web webpack webpack-cli
npm install --save-dev @grpc/grpc-js @grpc/proto-loader
```
----------------------------------------------------------------------------------------------------------------------------------------
6. open 3 terminals
```
1st terminal(inside grpc/build) -
mkdir -p uploads downloads 
```

then run the server - 
```
./server
```

2nd terminal(root directory) - 
```
envoy -c envoy.yaml --mode validate
```

it should print OK, at the end
run
``` 
envoy -c envoy.yaml 
```

3rd terminal(web directory) - run 

```
npm install
npm start
```
The web app must be running if all the steps are followed properly.
----------------------------------------------------------------------------------------------------------------------------------------

### Input instructions in the Client

Name: anything u wish
Ex: Tom 

Hello again: Greets again
Ex: Hello Tom again

Status: 200 OK

Upload: any file you wish to
Ex: test.txt

Download: enter the name of the file uploaded
Ex: test.txt


##### Highly recommend using Stackoverflow and ChatGPT to debug if things north for debugging.
