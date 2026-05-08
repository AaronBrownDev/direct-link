# Dev container image
#
# Base: Debian 13 (trixie) — ships GStreamer 1.24, which is required for the
# WHIP send pipeline.  GStreamer 1.22 (the Debian 12 default) has webrtcbin
# bugs that cause the send chain to stall after the first encoded buffer
# even though ICE/DTLS complete successfully.
FROM mcr.microsoft.com/devcontainers/go:2-1.26-trixie

RUN rm -f /etc/apt/sources.list.d/yarn.list

# Install C++ build tools.  Trixie ships LLVM 18 in main, so we install it
# directly via apt rather than running the apt.llvm.org bootstrap script.
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    gcc \
    gdb \
    valgrind \
    linux-perf \
    clang-18 \
    clang-format-18 \
    clang-tidy-18 \
    llvm-18-dev \
    libclang-18-dev \
    pkg-config \
    libgtest-dev \
    python3-pip \
    protobuf-compiler \
    patchelf \
    file \
    desktop-file-utils \
    wget \
    curl \
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

# Install GStreamer dev libraries.  Trixie ships GStreamer 1.26.x, which
# fixes the webrtcbin send-pipeline stalls present in 1.22.  gstreamer1.0-nice
# provides nicesrc/nicesink (required by webrtcbin for ICE).  Debian doesn't
# package gst-plugins-rs in trixie main, so whipsink is built from source in
# the next layer.
RUN apt-get update && apt-get install -y \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-bad1.0-dev \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-nice \
    gstreamer1.0-tools \
    && rm -rf /var/lib/apt/lists/*

# Install LiveKit dependencies.  clang-18 / llvm-18-dev / libclang-18-dev
# are installed in the C++ tools layer above; the LiveKit build pins to
# clang-18 explicitly via CC/CXX/LIBCLANG_PATH.
RUN apt-get update && apt-get install -y \
    ninja-build \
    libprotobuf-dev \
    libabsl-dev \
    libssl-dev \
    libva-dev \
    libdrm-dev \
    libgbm-dev \
    libx11-dev \
    libasound2-dev \
    && rm -rf /var/lib/apt/lists/*

# Install Qt
RUN apt-get update && apt-get install -y \
    libgl1-mesa-dev \
    libglib2.0-0 \
    libdbus-1-3 \
    libfontconfig1 \
    libmsgsl-dev \
    libfmt-dev \
    libxcb-cursor0 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-randr0 \
    libxcb-render-util0 \
    libxcb-shape0 \
    libxcb-sync1 \
    libxcb-xfixes0 \
    libxcb-xkb1 \
    libxkbcommon-x11-0 \
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

# Runs the following commands as vscode so /go/pkg/mod/ and /.cargo/bin are not owned by root
USER vscode

# Install Rust Toolchain
RUN curl https://sh.rustup.rs -sSf | sh -s -- -y --default-toolchain stable
ENV PATH="/home/vscode/.cargo/bin:${PATH}"

# Clone LiveKit repo and initialize submodules.
# The build is retried once because livekit-ffi/build.rs has a race with
# webrtc-sys/build.rs over the shared WebRTC download cache: if livekit-ffi
# observes the cache directory mid-extraction it skips the (locked) download
# and panics on a missing LICENSE.md. The retry succeeds because the cache is
# fully populated by then and cargo only rebuilds the failed crate.
RUN git clone --recurse-submodules https://github.com/livekit/client-sdk-cpp.git /home/vscode/livekit-cpp \
    && cd /home/vscode/livekit-cpp \
    && export CC=clang-18 \
    && export CXX=clang++-18 \
    && export LIBCLANG_PATH=/usr/lib/llvm-18/lib \
    && export CXXFLAGS="-Wno-deprecated-declarations" \
    && export CFLAGS="-Wno-deprecated-declarations" \
    && cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
    && (cmake --build build || cmake --build build)

# Build the WHIP/WHEP GStreamer plugin from gst-plugins-rs against the system's
# GStreamer (1.26 on trixie).  Debian's main archive doesn't ship a binary
# package, and pre-built plugins from upstream Docker images target slightly
# different gstreamer-rs binding patch levels and panic during type
# registration at runtime.  Building locally guarantees ABI alignment.
# Tag 0.14.5 is the latest 0.14.x, which targets GStreamer 1.26.
RUN git clone --depth 1 --branch 0.14.5 \
       https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs.git \
       /tmp/gst-plugins-rs \
    && cd /tmp/gst-plugins-rs \
    && cargo build --release -p gst-plugin-webrtchttp \
    && sudo install -m 644 target/release/libgstwebrtchttp.so \
       /usr/lib/x86_64-linux-gnu/gstreamer-1.0/ \
    && cd / && rm -rf /tmp/gst-plugins-rs ~/.cargo/registry ~/.cargo/git

# Install Go tools
RUN go install google.golang.org/protobuf/cmd/protoc-gen-go@latest \
    && go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest \
    && go install github.com/fullstorydev/grpcurl/cmd/grpcurl@latest
