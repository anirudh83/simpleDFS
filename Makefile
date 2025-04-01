CXX = g++
CXXFLAGS = -std=c++11 -g -Wall
LDFLAGS = -lgrpc++ -lgrpc -lprotobuf -lpthread

PROTO_PATH = .
CPP_OUT = .

all: server client

# Generate C++ code from protocol buffers
proto:
	protoc -I$(PROTO_PATH) --cpp_out=$(CPP_OUT) --grpc_out=$(CPP_OUT) \
	--plugin=protoc-gen-grpc=`which grpc_cpp_plugin` $(PROTO_PATH)/simple_dfs.proto

# Compile the server
server: proto
	$(CXX) $(CXXFLAGS) -o simple_dfs_server simple_dfs_server.cpp \
	simple_dfs.pb.cc simple_dfs.grpc.pb.cc $(LDFLAGS)

# Compile the client
client: proto
	$(CXX) $(CXXFLAGS) -o simple_dfs_client simple_dfs_client.cpp \
	simple_dfs.pb.cc simple_dfs.grpc.pb.cc $(LDFLAGS)

# Clean build files
clean:
	rm -f simple_dfs_server simple_dfs_client *.o *.pb.cc *.pb.h

# Create test directories
setup:
	mkdir -p server_files client_files1 client_files2

# Run the server
run_server:
	./simple_dfs_server

# Run a client with a specific directory
run_client1:
	./simple_dfs_client client_files1

run_client2:
	./simple_dfs_client client_files2

.PHONY: all proto clean setup run_server run_client1 run_client2