#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>
#include <grpcpp/grpcpp.h>
#include "simple_dfs.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

// Server implementation
class DFSServiceImpl final : public simple_dfs::DFSService::Service {
private:
    std::string server_dir;
    std::map<std::string, std::string> file_locks; // filename -> client_id

public:
    DFSServiceImpl(const std::string& dir) : server_dir(dir) {
        // Create server directory if it doesn't exist
        mkdir(server_dir.c_str(), 0755);
    }

    // Get a write lock for a file
    grpc::Status RequestWriteLock(grpc::ServerContext* context, 
                          const simple_dfs::LockRequest* request,
                          simple_dfs::LockResponse* response) override {
        std::string filename = request->filename();
        std::string client_id = request->client_id();
        
        std::cout << "Lock request from client " << client_id 
                  << " for file " << filename << std::endl;
        
        // Check if file is locked by another client
        auto it = file_locks.find(filename);
        if (it != file_locks.end() && it->second != client_id) {
            response->set_granted(false);
            response->set_message("File is locked by another client");
            return Status::OK;
        }
        
        // Grant the lock
        file_locks[filename] = client_id;
        response->set_granted(true);
        response->set_message("Lock granted");
        
        std::cout << "Lock granted to client " << client_id 
                  << " for file " << filename << std::endl;
        
        return Status::OK;
    }

    // Store a file
    grpc::Status StoreFile(grpc::ServerContext* context, 
                   const simple_dfs::FileData* request,
                   simple_dfs::StatusResponse* response) override {
        std::string filename = request->filename();
        std::string client_id = request->client_id();
        
        std::cout << "Store request from client " << client_id 
                  << " for file " << filename << std::endl;
        
        // Check if client has lock
        auto it = file_locks.find(filename);
        if (it == file_locks.end() || it->second != client_id) {
            response->set_success(false);
            response->set_message("No write lock for this file");
            return Status::OK;
        }
        
        // Write file data to disk
        std::string filepath = server_dir + "/" + filename;
        std::ofstream file(filepath, std::ios::binary);
        
        if (!file) {
            response->set_success(false);
            response->set_message("Failed to create file");
            return Status::OK;
        }
        
        file.write(request->data().c_str(), request->data().size());
        file.close();
        
        // Release the lock
        file_locks.erase(filename);
        
        response->set_success(true);
        response->set_message("File stored successfully");
        
        std::cout << "File " << filename << " stored successfully" << std::endl;
        
        return Status::OK;
    }

    // Fetch a file
    grpc::Status FetchFile(grpc::ServerContext* context, 
                   const simple_dfs::FileRequest* request,
                   simple_dfs::FileData* response) override {
        std::string filename = request->filename();
        
        std::cout << "Fetch request for file " << filename << std::endl;
        
        // Check if file exists
        std::string filepath = server_dir + "/" + filename;
        struct stat stat_buf;
        
        if (stat(filepath.c_str(), &stat_buf) != 0) {
            return Status(grpc::StatusCode::NOT_FOUND, "File not found");
        }
        
        // Read file data
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            return Status(grpc::StatusCode::INTERNAL, "Failed to open file");
        }
        
        std::string content((std::istreambuf_iterator<char>(file)), 
                            std::istreambuf_iterator<char>());
        
        // Set response
        response->set_filename(filename);
        response->set_data(content);
        response->set_modified_time(stat_buf.st_mtime);
        
        std::cout << "File " << filename << " fetched successfully" << std::endl;
        
        return Status::OK;
    }

    // List all files
    grpc::Status ListFiles(grpc::ServerContext* context, 
                   const simple_dfs::Empty* request,
                   simple_dfs::FileList* response) override {
        std::cout << "List files request" << std::endl;
        
        // Open directory and read all files
        DIR* dir = opendir(server_dir.c_str());
        if (dir == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Failed to open directory");
        }
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // Skip . and ..
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            // Get file info
            std::string filepath = server_dir + "/" + entry->d_name;
            struct stat stat_buf;
            
            if (stat(filepath.c_str(), &stat_buf) == 0) {
                // Add file to response
                simple_dfs::FileInfo* file_info = response->add_files();
                file_info->set_filename(entry->d_name);
                file_info->set_size(stat_buf.st_size);
                file_info->set_modified_time(stat_buf.st_mtime);
            }
        }
        
        closedir(dir);
        std::cout << "Listed " << response->files_size() << " files" << std::endl;
        
        return Status::OK;
    }
};

int main(int argc, char** argv) {
    std::string server_address = "0.0.0.0:50051";
    std::string server_dir = "server_files";
    
    DFSServiceImpl service(server_dir);
    
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;
    
    server->Wait();
    
    return 0;
}