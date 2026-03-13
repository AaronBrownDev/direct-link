# Client gRPC Setup

## Required Qt Modules

Qt 6.8 or later is required. The following modules must be installed:

- `Qt Protobuf` — generates C++ message classes from `.proto` files
- `Qt GRPC` — generates Qt-native gRPC client stubs

In the devcontainer these are installed automatically via `aqtinstall`. On a local machine, install them via Qt Maintenance Tool or verify they are present before building.

No external gRPC library (`libgrpc++-dev`, `grpc_cpp_plugin`) is needed. Qt's `QGrpcHttp2Channel` provides its own HTTP/2 implementation.

## Proto File and Code Generation

The gRPC API is defined in `shared/protocol/signaling.proto`. The client consumes it via CMake at configure time — no generated files are checked into version control.

`client/CMakeLists.txt` contains:

```cmake
qt_add_protobuf(signaling_proto
    PROTO_FILES ${SIGNALING_PROTO}
)

qt_add_grpc(signaling_grpc CLIENT
    PROTO_FILES ${SIGNALING_PROTO}
)
```

When you run `cmake -B build`, Qt invokes `protoc` with its own plugins (`qtprotobufgen`, `qtgrpcgen`) and writes the generated headers and sources into the build directory. The generated files follow the naming pattern `*.qpb.h` and `*.grpc.qpb.h`.

`protoc` must be on your PATH. In the devcontainer it is installed via `protobuf-compiler`. On a local machine:

```bash
sudo apt install protobuf-compiler
```

## Testing the gRPC Connection Locally

### Prerequisites

The full Docker Compose stack must be running (Redis, LiveKit, LiveKit Ingress, signaling server). From the devcontainer:

```bash
cd /workspace/backend && make run-signaling
```

### Run the Qt roundtrip test

```bash
cd /workspace/client
cmake -B build .
cmake --build build
./build/test_signaling_client
```

**Note:** The camera `JoinSession` call requires LiveKit Ingress to be running. Without it the server returns a gRPC `Internal` error — this is an infra dependency, not a client issue.

## JoinReply Dual-Response Handling

`JoinSession` returns different fields depending on the role passed in the request. The server always leaves the irrelevant pair empty.

| Role | Returned fields |
|------|-----------------|
| `director` | `token`, `livekit_url` |
| `camera` | `whip_url`, `stream_key` |

The `SessionClient` wrapper handles this branching internally and emits the appropriate signal:

- `directorJoined(token, livekitUrl)` — use these to connect via the LiveKit C++ SDK
- `cameraJoined(whipUrl, streamKey)` — pass these to the `WHIPPublisher` GStreamer pipeline
