# Dev container image
FROM mcr.microsoft.com/devcontainers/go:2-1.25-bookworm

RUN rm -f /etc/apt/sources.list.d/yarn.list

# Install C++ build tools
RUN apt-get update && apt-get install -y \
    wget \
    gnupg \
    lsb-release \
    software-properties-common \
    && wget https://apt.llvm.org/llvm.sh \
    && chmod +x llvm.sh \
    && ./llvm.sh 18 \
    && rm llvm.sh
    
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    gcc \
    gdb \
    valgrind \
    linux-perf \
    clang-format-18 \
    clang-tidy-18 \
    pkg-config \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

RUN ln -sf /usr/bin/clang-tidy-18 /usr/bin/clang-tidy \
    && ln -sf /usr/bin/clang-format-18 /usr/bin/clang-format

# Install FFmpeg dev libraries
RUN apt-get update && apt-get install -y \
    ffmpeg \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libavdevice-dev \
    && rm -rf /var/lib/apt/lists/*

# Set g++ as default C++ compiler
ENV CXX=g++
ENV CC=gcc

# Install Qt

# Runs the following commands as vscode so /go/pkg/mod/ is not owned by root
USER vscode

# Install Go tools
RUN go install google.golang.org/protobuf/cmd/protoc-gen-go@latest \
    && go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest \
    && go install github.com/fullstorydev/grpcurl/cmd/grpcurl@latest