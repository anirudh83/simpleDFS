#!/bin/bash
set -e

# Clean previous build
rm -f *.pb.cc *.pb.h simple_dfs_server simple_dfs_client

# Generate the protobuf files
echo "Generating proto files..."
protoc -I. --cpp_out=. --grpc_out=. \
  --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` ./simple_dfs.proto

# Compile the server
echo "Compiling server..."
g++ -std=c++17 -g -Wall -I/usr/local/include \
  -o simple_dfs_server simple_dfs_server.cpp \
  simple_dfs.pb.cc simple_dfs.grpc.pb.cc \
  -L/usr/local/lib -lgrpc++ -lgrpc -lprotobuf -lpthread \
  -labsl_synchronization -labsl_bad_optional_access -labsl_time

# Compile the client
echo "Compiling client..."
g++ -std=c++17 -g -Wall -I/usr/local/include \
  -o simple_dfs_client simple_dfs_client.cpp \
  simple_dfs.pb.cc simple_dfs.grpc.pb.cc \
  -L/usr/local/lib -lgrpc++ -lgrpc -lprotobuf -lpthread \
  -labsl_synchronization -labsl_bad_optional_access -labsl_time

# Create the necessary directories
mkdir -p server_files client_files1 client_files2

echo "Build complete!"
echo "To run the server: ./simple_dfs_server"
echo "To run the first client: ./simple_dfs_client client_files1"
echo "To run the second client: ./simple_dfs_client client_files2"