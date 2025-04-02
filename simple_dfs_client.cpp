#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#include <utime.h>
#include <grpcpp/grpcpp.h>
#include "simple_dfs.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

// Generate a random client ID
std::string generate_client_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    const char* hex_chars = "0123456789abcdef";
    std::string uuid = "client-";
    
    for (int i = 0; i < 8; ++i) {
        uuid += hex_chars[dis(gen)];
    }
    
    return uuid;
}

// Client implementation
class DFSClient {
private:
    std::unique_ptr<simple_dfs::DFSService::Stub> stub_;
    std::string client_id;
    std::string client_dir;
    int inotify_fd;
    int watch_descriptor;
    bool running;
    std::thread watcher_thread;

public:
    DFSClient(std::shared_ptr<Channel> channel, const std::string& dir) 
        : stub_(simple_dfs::DFSService::NewStub(channel)), 
          client_id(generate_client_id()),
          client_dir(dir),
          running(false) {
        
        // Create client directory if it doesn't exist
        mkdir(client_dir.c_str(), 0755);
        
        std::cout << "Client initialized with ID: " << client_id << std::endl;
        std::cout << "Using directory: " << client_dir << std::endl;
    }
    
    ~DFSClient() {
        StopWatcher();
    }
    
    // Start the file watcher thread
    void StartWatcher() {
        if (running) return;
        
        running = true;
        watcher_thread = std::thread(&DFSClient::WatcherThread, this);
        
        std::cout << "File watcher started" << std::endl;
    }
    
    // Stop the file watcher thread
    void StopWatcher() {
        if (!running) return;
        
        running = false;
        if (watcher_thread.joinable()) {
            watcher_thread.join();
        }
        
        std::cout << "File watcher stopped" << std::endl;
    }
    
    // Watcher thread function
    void WatcherThread() {
        inotify_fd = inotify_init();
        if (inotify_fd < 0) {
            std::cerr << "Failed to initialize inotify" << std::endl;
            return;
        }
        
        watch_descriptor = inotify_add_watch(inotify_fd, client_dir.c_str(), 
                                            IN_CREATE | IN_MODIFY | IN_DELETE);
        
        if (watch_descriptor < 0) {
            std::cerr << "Failed to add inotify watch" << std::endl;
            close(inotify_fd);
            return;
        }
        
        const int BUF_LEN = 1024 * (sizeof(struct inotify_event) + 16);
        char buffer[BUF_LEN];
        
        while (running) {
            // Use select to wait with timeout so we can check running flag
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(inotify_fd, &fds);
            
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            
            int ret = select(inotify_fd + 1, &fds, NULL, NULL, &tv);
            
            if (ret < 0) {
                std::cerr << "Select error" << std::endl;
                break;
            }
            
            if (!FD_ISSET(inotify_fd, &fds)) {
                // Timeout - check running flag and continue
                continue;
            }
            
            int length = read(inotify_fd, buffer, BUF_LEN);
            if (length < 0) {
                std::cerr << "Read error" << std::endl;
                break;
            }
            
            int i = 0;
            while (i < length) {
                struct inotify_event* event = (struct inotify_event*) &buffer[i];
                
                if (event->len) {
                    // Skip . and .. and hidden files
                    if (event->name[0] != '.') {
                        std::string filename = event->name;
                        
                        if (event->mask & IN_CREATE) {
                            std::cout << "File created: " << filename << std::endl;
                            // Upload the new file to server
                            StoreFile(filename);
                        }
                        else if (event->mask & IN_MODIFY) {
                            std::cout << "File modified: " << filename << std::endl;
                            // Upload the modified file to server
                            StoreFile(filename);
                        }
                        else if (event->mask & IN_DELETE) {
                            std::cout << "File deleted: " << filename << std::endl;
                            // We would delete the file on server in a full implementation
                        }
                    }
                }
                
                i += sizeof(struct inotify_event) + event->len;
            }
        }
        
        inotify_rm_watch(inotify_fd, watch_descriptor);
        close(inotify_fd);
    }
    
    // Request a write lock
    bool RequestWriteLock(const std::string& filename) {
        ClientContext context;
        simple_dfs::LockRequest request;
        simple_dfs::LockResponse response;
        
        request.set_filename(filename);
        request.set_client_id(client_id);
        
        Status status = stub_->RequestWriteLock(&context, request, &response);
        
        if (status.ok()) {
            return response.granted();
        } else {
            std::cerr << "Lock request failed: " << status.error_message() << std::endl;
            return false;
        }
    }
    
    // Store a file to the server
    bool StoreFile(const std::string& filename) {
        // First, get a write lock
        if (!RequestWriteLock(filename)) {
            std::cerr << "Failed to acquire write lock for " << filename << std::endl;
            return false;
        }
        
        std::string filepath = client_dir + "/" + filename;
        struct stat stat_buf;
        
        if (stat(filepath.c_str(), &stat_buf) != 0) {
            std::cerr << "File not found: " << filepath << std::endl;
            return false;
        }
        
        // Read the file
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open file: " << filepath << std::endl;
            return false;
        }
        
        std::string content((std::istreambuf_iterator<char>(file)), 
                           std::istreambuf_iterator<char>());
        
        // Prepare request
        ClientContext context;
        simple_dfs::FileData request;
        simple_dfs::StatusResponse response;
        
        request.set_filename(filename);
        request.set_client_id(client_id);
        request.set_data(content);
        request.set_modified_time(stat_buf.st_mtime);
        
        // Send request
        Status status = stub_->StoreFile(&context, request, &response);
        
        if (status.ok() && response.success()) {
            std::cout << "Successfully stored " << filename << std::endl;
            return true;
        } else {
            std::cerr << "Store failed: " << response.message() << std::endl;
            return false;
        }
    }
    
    // Fetch a file from the server
    bool FetchFile(const std::string& filename) {
        ClientContext context;
        simple_dfs::FileRequest request;
        simple_dfs::FileData response;
        
        request.set_filename(filename);
        
        Status status = stub_->FetchFile(&context, request, &response);
        
        if (!status.ok()) {
            std::cerr << "Fetch failed: " << status.error_message() << std::endl;
            return false;
        }
        
        // Write the file locally
        std::string filepath = client_dir + "/" + filename;
        std::ofstream file(filepath, std::ios::binary);
        
        if (!file) {
            std::cerr << "Failed to create file: " << filepath << std::endl;
            return false;
        }
        
        file.write(response.data().c_str(), response.data().size());
        file.close();
        
        // Set the modification time to match the server
        struct utimbuf new_times;
        new_times.actime = time(NULL);
        new_times.modtime = response.modified_time();
        utime(filepath.c_str(), &new_times);
        
        std::cout << "Successfully fetched " << filename << std::endl;
        return true;
    }
    
    // List all files on the server
    void ListFiles() {
        ClientContext context;
        simple_dfs::Empty request;
        simple_dfs::FileList response;
        
        Status status = stub_->ListFiles(&context, request, &response);
        
        if (!status.ok()) {
            std::cerr << "List failed: " << status.error_message() << std::endl;
            return;
        }
        
        std::cout << "Files on server:" << std::endl;
        for (int i = 0; i < response.files_size(); i++) {
            const simple_dfs::FileInfo& file_info = response.files(i);
            
            // Convert timestamp to readable format
            char time_str[80];
            time_t mod_time = file_info.modified_time();
            struct tm* timeinfo = localtime(&mod_time);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);
            
            std::cout << file_info.filename() 
                      << " (" << file_info.size() << " bytes, modified: " 
                      << time_str << ")" << std::endl;
        }
    }
    
    // Sync all files from server
    void SyncFromServer() {
        ClientContext context;
        simple_dfs::Empty request;
        simple_dfs::FileList response;
        
        Status status = stub_->ListFiles(&context, request, &response);
        
        if (!status.ok()) {
            std::cerr << "Sync failed: " << status.error_message() << std::endl;
            return;
        }
        
        std::cout << "Syncing files from server..." << std::endl;
        for (int i = 0; i < response.files_size(); i++) {
            const simple_dfs::FileInfo& file_info = response.files(i);
            std::string filename = file_info.filename();
            
            // Check if file exists locally
            std::string filepath = client_dir + "/" + filename;
            struct stat stat_buf;
            bool file_exists = (stat(filepath.c_str(), &stat_buf) == 0);
            
            // If file doesn't exist locally or is older than server version, fetch it
            if (!file_exists || stat_buf.st_mtime < file_info.modified_time()) {
                std::cout << "Fetching " << filename << " from server..." << std::endl;
                FetchFile(filename);
            }
        }
        
        std::cout << "Sync complete" << std::endl;
    }
};

void PrintUsage() {
    std::cout << "Commands:" << std::endl;
    std::cout << "  start         - Start the file watcher" << std::endl;
    std::cout << "  stop          - Stop the file watcher" << std::endl;
    std::cout << "  list          - List files on the server" << std::endl;
    std::cout << "  fetch <file>  - Fetch a file from the server" << std::endl;
    std::cout << "  store <file>  - Store a file to the server" << std::endl;
    std::cout << "  sync          - Sync all files from server" << std::endl;
    std::cout << "  quit          - Exit the program" << std::endl;
}

int main(int argc, char** argv) {
    std::string server_address = "localhost:50051";
    std::string client_dir = "client_files";
    
    // Allow client directory to be specified as command line arg
    if (argc > 1) {
        client_dir = argv[1];
    }
    
    // Create a channel and client
    DFSClient client(
        grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()),
        client_dir
    );
    
    bool quit = false;
    std::string line, command, arg;
    
    PrintUsage();
    
    while (!quit) {
        std::cout << "> ";
        std::getline(std::cin, line);
        
        // Parse command and argument
        size_t space_pos = line.find(' ');
        if (space_pos != std::string::npos) {
            command = line.substr(0, space_pos);
            arg = line.substr(space_pos + 1);
        } else {
            command = line;
            arg = "";
        }
        
        if (command == "start") {
            client.StartWatcher();
        }
        else if (command == "stop") {
            client.StopWatcher();
        }
        else if (command == "list") {
            client.ListFiles();
        }
        else if (command == "fetch" && !arg.empty()) {
            client.FetchFile(arg);
        }
        else if (command == "store" && !arg.empty()) {
            client.StoreFile(arg);
        }
        else if (command == "sync") {
            client.SyncFromServer();
        }
        else if (command == "quit" || command == "exit") {
            quit = true;
        }
        else {
            PrintUsage();
        }
    }
    
    return 0;
}