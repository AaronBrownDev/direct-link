.PHONY: proto run-signaling build-signaling

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
