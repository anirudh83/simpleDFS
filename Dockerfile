FROM ubuntu:22.04

# Set noninteractive installation
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    autoconf \
    libtool \
    pkg-config \
    cmake \
    git \
    curl \
    unzip \
    wget \
    python3 \
    vim \
    && apt-get clean

# Install Protocol Buffers
RUN cd /tmp && \
    git clone https://github.com/protocolbuffers/protobuf.git && \
    cd protobuf && \
    git checkout v3.15.8 && \
    git submodule update --init --recursive && \
    ./autogen.sh && \
    ./configure && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    cd / && rm -rf /tmp/protobuf

# Install gRPC
RUN cd /tmp && \
    git clone --recurse-submodules -b v1.40.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc && \
    cd grpc && \
    mkdir -p cmake/build && \
    cd cmake/build && \
    cmake -DgRPC_INSTALL=ON \
          -DgRPC_BUILD_TESTS=OFF \
          -DgRPC_PROTOBUF_PROVIDER=package \
          -DCMAKE_BUILD_TYPE=Release \
          ../.. && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    cd / && rm -rf /tmp/grpc

# Create working directory
WORKDIR /app

# Copy the source files
COPY simple_dfs.proto /app/
COPY simple_dfs_server.cpp /app/
COPY simple_dfs_client.cpp /app/
COPY build.sh /app/

# Make build script executable
RUN chmod +x /app/build.sh

# Build the project
RUN ./build.sh

# Expose the gRPC port
EXPOSE 50051

# Create volumes for persistently storing files
VOLUME ["/app/server_files", "/app/client_files1", "/app/client_files2"]

# Default command
CMD ["/bin/bash"]