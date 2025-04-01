## Simple Distributed File System Implementation

This is a very simple distributed file system implementation to help you understand the core concepts. This basic example demonstrates:

1. A server that maintains files
2. Clients that maintain local copies
3. File watching using inotify
4. Basic synchronization between clients and server
5. Write locks to prevent conflicts

### Components

1. Proto Definition (simple_dfs.proto)
    - Defines the RPC service methods
    - Defines message types for requests and responses

2. Server Implementation (simple_dfs_server.cpp)
    - Handles client requests (fetch, store, list, lock)
    - Manages file storage in a central directory
    - Provides write locks for files

3. Client Implementation (simple_dfs_client.cpp)
    - Monitors a local directory using inotify
    - Synchronizes with the server when files change
    - Provides a simple command interface

4. Makefile
    - Compiles the code and generates proto files
    - Provides convenience targets for running server and clients

### How to Use This Example

1. Set up the environment
```
make setup
```

2. Compile the code
```
make all
```

3. Run the server in one terminal
```
make run_server
```

4. Run clients in separate terminals
```
make run_client1
make run_client2
```

5. Use client commands

| Command | Description |
|---------|-------------|
| `start` | Start the file watcher |
| `list` | List files on the server |
| `fetch <filename>` | Download a file from the server |
| `store <filename>` | Upload a file to the server |
| `sync` | Synchronize all files from the server |
| `stop` | Stop the file watcher |
| `quit` | Exit the client |

### Key Concepts Demonstrated

#### File Watching
The client uses inotify to watch for file changes in the mounted directory. When a file is created or modified, it automatically uploads the file to the server.

#### Write Locks
Before a client can upload a file, it must obtain a write lock from the server. This prevents conflicts when multiple clients try to modify the same file.

#### Synchronization
Clients can synchronize with the server to ensure they have the latest versions of all files.

### Directory Structure
- `server_files/` - Where the server stores the master copies
- `client_files1/` - Mount directory for the first client
- `client_files2/` - Mount directory for the second client

### Testing Scenarios

#### Basic file creation:
1. Start the watcher in Client 1
2. Create a file in Client 1's directory
3. Observe it being uploaded to the server
4. Run sync in Client 2
5. Observe the file appearing in Client 2's directory

#### Lock contention:
1. Try to modify the same file in both clients at nearly the same time
2. Observe that only one client succeeds in uploading changes

#### Manual operations:
- Use fetch and store commands to manually transfer files

This simple example should help you understand the core concepts of the more complex distributed file system you're working on for your assignment.
