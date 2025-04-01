#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <sys/stat.h>
#include <grpcpp/grpcpp.h>
#include "simple_dfs.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

class DFSServiceImpl final : public simple_dfs::DFSService::Service
{
private:
    std::string server_dir;
    std::map<std::string, std::string> file_locks; // Map to store file locks (finename -> clientId)

public:
    DFSServiceImpl(const std::string &dir) : server_dir(dir)
    {
        // Create the server directory if it doesn't exist
        mkdir(server_dir.c_str(), 0755);
    }

    Status RequestWriteLock(ServerContext *context, const simple_dfs::LockRequest *request, simple_dfs::LockResponse *response) override
    {
        std::string filename = request->filename();
        std::string clientId = request->client_id();

        // Check if the file is already locked
        if (file_locks.find(filename) != file_locks.end())
        {
            std::string locked_clientId = file_locks[filename];
            // If the file is locked by the same client, allow the lock
            std::cout << "File " << filename << " is already locked by client " << locked_clientId << std::endl;
            response->set_status(simple_dfs::LockResponse::LOCKED);
            return Status::OK;
        }

        // Lock the file for the client
        file_locks[filename] = clientId;
        response->set_status(simple_dfs::LockResponse::LOCKED);
        std::cout << "File " << filename << " locked by client " << clientId << std::endl;
        return Status::OK;
    }

    Status StoreFile(ServerContext *context, const simple_dfs::FileData *request, simple_dfs::StatusResponse *response) override
    {
        std::string filename = request->filename();
        std::string clientId = request->client_id();

        std::cout << "Store request from client " << clientId << " for file " << filename << std::endl;

        // check if the client has the lock
        auto it = file_locks.find(filename);
        if (it == file_locks.end() || it->second != clientId)
        {
            std::cout << "Client " << clientId << " does not have the lock for file " << filename << std::endl;
            response->set_status(simple_dfs::StatusResponse::NOT_LOCKED);
            return Status::OK;
        }

        std::string file_path = server_dir + "/" + filename;
        std::ofstream file(file_path, std::ios::binary);
        if (!file)
        {
            std::cerr << "Failed to open file " << file_path << " for writing" << std::endl;
            response->set_status(simple_dfs::StatusResponse::FAILED);
            return Status::OK;
        }
        file.write(request->data().c_str(), request->data().size());
        file.close();

        file_locks.erase(filename); // Unlock the file after storing
        std::cout << "File " << filename << " stored by client " << clientId << std::endl;
        response->set_status(simple_dfs::StatusResponse::SUCCESS);
        response->set_message("File stored successfully");

        return Status::OK;
    }

    Status FetchFile(ServerContext *context, const simple_dfs::FileRequest *request, simple_dfs::FileData *response) override
    {
        std::string filename = request->filename();
        std::string clientId = request->client_id();

        std::cout << "Fetch request from client " << clientId << " for file " << filename << std::endl;

        std::string file_path = server_dir + "/" + filename;
        struct stat stat_buf;
        if (stat(file_path.c_str(), &stat_buf) != 0)
        {
            std::cerr << "File " << filename << " not found" << std::endl;
            return Status(grpc::StatusCode::NOT_FOUND, "File not found");
        }

        std::ifstream file(file_path, std::ios::binary);
        if (!file)
        {
            std::cerr << "Failed to open file " << file_path << " for reading" << std::endl;
            return Status(grpc::StatusCode::INTERNAL, "Failed to open file");
        }

        std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        response->set_filename(filename);
        response->set_data(data);
        response->set_status(simple_dfs::FileData::SUCCESS);
        std::cout << "File " << filename << " fetched by client " << clientId << std::endl;

        return Status::OK;
    }

    Status ListFiles(ServerContext *context,
                     const simple_dfs::Empty *request,
                     simple_dfs::FileList *response) override
    {
        std::cout << "List files request" << std::endl;

        // Open directory and read all files
        DIR *dir = opendir(server_dir.c_str());
        if (dir == nullptr)
        {
            return Status(grpc::StatusCode::INTERNAL, "Failed to open directory");
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            // Skip . and ..
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            // Get file info
            std::string filepath = server_dir + "/" + entry->d_name;
            struct stat stat_buf;

            if (stat(filepath.c_str(), &stat_buf) == 0)
            {
                // Add file to response
                simple_dfs::FileInfo *file_info = response->add_files();
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

int main()
{
    std::string server_address = "localhost:50051";
    std::string server_dir = "server_files";

    DFSServiceImpl service(server_dir);
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server Listening on " << server_address << std::endl;
    server->Wait();
    return 0;
}