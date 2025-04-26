CXX = g++
CXXFLAGS = -std=c++17 -g -Wall -I${HOME}/.local/include -I/usr/local/include \
           -I${HOME}/.local/include/grpc -I${HOME}/.local/include/grpcpp
LDFLAGS = -L${HOME}/.local/lib -L/usr/local/lib -lgrpc++ -lgrpc -lprotobuf -lpthread

PROTO_PATH = .
CPP_OUT = .
GRPC_CPP_PLUGIN = `which grpc_cpp_plugin`

all: server client

# Generate C++ code from protocol buffers
proto:
	protoc -I$(PROTO_PATH) --cpp_out=$(CPP_OUT) \
	--grpc_out=generate_mock_code=true:$(CPP_OUT) \
	--plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $(PROTO_PATH)/simple_dfs.proto

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