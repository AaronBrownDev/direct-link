# Dev container image
FROM mcr.microsoft.com/devcontainers/go:2-1.26-bookworm

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
    python3-pip \
    protobuf-compiler \
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

# Install GStreamer dev libraries
RUN apt-get update && apt-get install -y \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-tools \
    && rm -rf /var/lib/apt/lists/*

# Install WebRTC plugin for GStreamer
COPY --from=livekit/gstreamer:1.22.8-prod-rs \
    /usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstwebrtchttp.so \
    /usr/lib/x86_64-linux-gnu/gstreamer-1.0/

# Install Qt
RUN apt-get update && apt-get install -y \
    libgl1-mesa-dev \
    libglib2.0-0 \
    libdbus-1-3 \
    libfontconfig1 \
    && rm -rf /var/lib/apt/lists/*

ENV QT_VERSION=6.10.2
ENV QT_INSTALL_DIR=/opt/Qt

RUN pip install aqtinstall --break-system-packages \
    && python3 -m aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
    --outputdir ${QT_INSTALL_DIR} \
    -m qtmultimedia qtshadertools qtgrpc

ENV PATH=${QT_INSTALL_DIR}/${QT_VERSION}/gcc_64/bin:${PATH}
ENV CMAKE_PREFIX_PATH=${QT_INSTALL_DIR}/${QT_VERSION}/gcc_64
ENV LD_LIBRARY_PATH=${QT_INSTALL_DIR}/${QT_VERSION}/gcc_64/lib:${LD_LIBRARY_PATH}

# Runs the following commands as vscode so /go/pkg/mod/ is not owned by root
USER vscode

# Install Go tools
RUN go install google.golang.org/protobuf/cmd/protoc-gen-go@latest \
    && go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest \
    && go install github.com/fullstorydev/grpcurl/cmd/grpcurl@latest
