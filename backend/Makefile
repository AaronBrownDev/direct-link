.PHONY: proto run-signaling build-signaling test-unit test-integration test-all

# Directories
PROTO_DIR := ../shared/protocol
PROTO_OUT := gen/proto/signaling

proto:
	mkdir -p $(PROTO_OUT)
	protoc \
		--go_out=$(PROTO_OUT) \
		--go_opt=paths=source_relative \
		--go-grpc_out=$(PROTO_OUT) \
		--go-grpc_opt=paths=source_relative \
		-I $(PROTO_DIR) \
		$(PROTO_DIR)/signaling.proto

run-signaling:
	go run ./cmd/signaling

build-signaling:
	go build -o bin/signaling ./cmd/signaling

# Testing
test-unit:
	go test -race ./tests/unit/...

test-integration:
	go test -race -count=1 ./tests/integration/...

test-all:
	go test -race -cover ./...