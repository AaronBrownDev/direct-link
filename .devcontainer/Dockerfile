# Dev container image
FROM mcr.microsoft.com/devcontainers/go:2-1.25-bookworm

# Install C++ build tools, CMAKE, Qt

# Runs the following commands as vscode so /go/pkg/mod/ is not owned by root
USER vscode

# Install Go tools
RUN go install google.golang.org/protobuf/cmd/protoc-gen-go@latest \
    && go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest
