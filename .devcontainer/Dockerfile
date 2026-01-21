# Dev container image
FROM mcr.microsoft.com/devcontainers/go:2-1.25-bookworm

# Install C++ build tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    gcc \
    gdb \
    valgrind \
    linux-perf \
    clang-format \
    clang-tidy \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*


# Install FFmpeg dev libraries
RUN apt-get update && apt-get install -y \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    && rm -rf /var/lib/apt/lists/*

# Set g++ as default C++ compiler
ENV CXX=g++
ENV CC=gcc

# Install Qt

# Runs the following commands as vscode so /go/pkg/mod/ is not owned by root
USER vscode

# Install Go tools
RUN go install google.golang.org/protobuf/cmd/protoc-gen-go@latest \
    && go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest
