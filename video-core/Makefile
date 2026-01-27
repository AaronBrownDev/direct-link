# DirectLink Video Core Makefile

CXX := g++
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic
LDFLAGS := $(shell pkg-config --libs libavcodec libavformat libavutil libswscale)
INCLUDES := -Iinclude $(shell pkg-config --cflags libavcodec libavformat libavutil libswscale)

# Directories
BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin
OBJ_DIR := $(BUILD_DIR)/obj

# CPU Encode Test
ENCODE_TEST_SRC := tests/manual/encode_test.cpp
ENCODE_TEST_OBJ := $(OBJ_DIR)/encode_test.o
ENCODE_TEST_BIN := $(BIN_DIR)/encode_test

# NVENC Test
NVENC_TEST_SRC := tests/manual/nvenc_test.cpp
NVENC_TEST_OBJ := $(OBJ_DIR)/nvenc_test.o
NVENC_TEST_BIN := $(BIN_DIR)/nvenc_test

# Build modes
DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g -O0 -DDEBUG
else
    CXXFLAGS += -O3 -DNDEBUG
endif

# Targets
.PHONY: all clean encode_test nvenc_test run_encode_test run_nvenc_test tests help

all: tests

tests: encode_test nvenc_test

# Create directories
$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

# CPU Encode test
encode_test: $(ENCODE_TEST_BIN)

$(ENCODE_TEST_BIN): $(ENCODE_TEST_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built encode_test -> $@"

$(OBJ_DIR)/encode_test.o: $(ENCODE_TEST_SRC) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# NVENC test
nvenc_test: $(NVENC_TEST_BIN)

$(NVENC_TEST_BIN): $(NVENC_TEST_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built nvenc_test -> $@"

$(OBJ_DIR)/nvenc_test.o: $(NVENC_TEST_SRC) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Run tests
run_encode_test: $(ENCODE_TEST_BIN)
	@echo "Running CPU encode test..."
	@cd $(BIN_DIR) && ./encode_test

run_nvenc_test: $(NVENC_TEST_BIN)
	@echo "Running NVENC test..."
	@cd $(BIN_DIR) && ./nvenc_test

# Clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f output.h264 output_nvenc.h264
	@echo "Cleaned build artifacts"

# Help
help:
	@echo "DirectLink Video Core Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build all (default)"
	@echo "  tests            - Build all tests"
	@echo "  encode_test      - Build CPU encode test"
	@echo "  nvenc_test       - Build NVENC test"
	@echo "  run_encode_test  - Build and run CPU encode test"
	@echo "  run_nvenc_test   - Build and run NVENC test"
	@echo "  clean            - Remove build artifacts"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1          - Build with debug symbols (default: 0)"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build all tests (release)"
	@echo "  make DEBUG=1            # Build all tests (debug)"
	@echo "  make nvenc_test         # Build only NVENC test"
	@echo "  make run_nvenc_test     # Build and run NVENC test"