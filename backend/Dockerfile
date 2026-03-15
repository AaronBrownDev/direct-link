# --------------- Stage 1: Build ------------------------------
FROM golang:1.26-bookworm AS builder

# Pin versions for reproducibility
ARG PROTOC_VERSION=34.0
ARG TARGETOS=linux
ARG TARGETARCH=amd64

# Install protoc from official release
RUN apt-get update && apt-get install -y --no-install-recommends unzip ca-certificates \
    && curl -fsSL \
       "https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOC_VERSION}/protoc-${PROTOC_VERSION}-linux-x86_64.zip" \
       -o /tmp/protoc.zip \
    && unzip -q /tmp/protoc.zip -d /usr/local \
    && rm /tmp/protoc.zip \
    && rm -rf /var/lib/apt/lists/*

# Install Go protoc plugins
RUN go install google.golang.org/protobuf/cmd/protoc-gen-go@latest \
    && go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest

WORKDIR /src

# --- Layer cache: dependencies first ---
COPY backend/go.mod backend/go.sum ./backend/
RUN cd backend && go mod download && go mod verify

# --- Generate protobuf code ---
COPY shared/protocol/ ./shared/protocol/
RUN mkdir -p backend/gen/proto/signaling \
    && protoc \
       --go_out=backend/gen/proto/signaling \
       --go_opt=paths=source_relative \
       --go-grpc_out=backend/gen/proto/signaling \
       --go-grpc_opt=paths=source_relative \
       -I shared/protocol \
       shared/protocol/signaling.proto

# --- Copy source and build ---
COPY backend/ ./backend/

# Build args for version injection (optional, set at build time)
ARG VERSION=dev
ARG COMMIT=unknown

RUN cd backend && CGO_ENABLED=0 GOOS=${TARGETOS} GOARCH=${TARGETARCH} \
    go build \
      -ldflags="-s -w -X main.version=${VERSION} -X main.commit=${COMMIT}" \
      -trimpath \
      -o /bin/signaling \
      ./cmd/signaling

# --------------- Stage 2: Runtime ----------------------------
FROM gcr.io/distroless/static-debian12:nonroot

# OCI image labels
LABEL org.opencontainers.image.title="DirectLink Signaling Server" \
      org.opencontainers.image.description="gRPC + HTTP control plane for DirectLink remote production platform" \
      org.opencontainers.image.source="https://github.com/AaronBrownDev/direct-link" \
      org.opencontainers.image.licenses="MIT"

# Copy the static binary from builder
COPY --from=builder /bin/signaling /signaling

# Distroless nonroot user (UID 65532)
USER nonroot:nonroot

# HTTP (health + metrics + webhooks) and gRPC
EXPOSE 8081 50051

ENTRYPOINT ["/signaling"]