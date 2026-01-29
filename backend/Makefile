.PHONY: proto run-sfu run-signaling build-sfu build-signaling

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

run-sfu:
	go run ./cmd/sfu

run-signaling:
	go run ./cmd/signaling

build-sfu:
	go build -o bin/sfu ./cmd/sfu

build-signaling:
	go build -o bin/signaling ./cmd/signaling
