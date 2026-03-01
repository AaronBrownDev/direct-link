# DirectLink Video Core Makefile

CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic
LDFLAGS := $(shell pkg-config --libs libavcodec libavformat libavutil libswscale libavdevice)
INCLUDES := -Iinclude $(shell pkg-config --cflags libavcodec libavformat libavutil libswscale libavdevice)

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
.PHONY: all clean encode_test nvenc_test run_encode_test run_nvenc_test tests help capture_pipeline run_capture_pipeline

all: tests

tests: encode_test nvenc_test

LINT_DIRS := src include tests/unit tests/integration

# Lint code with clang-tidy
lint:
	@echo "Running clang-tidy on source files..."
	@find $(LINT_DIRS) -name "*.cpp" | \
        xargs -P$(shell nproc) -I{} clang-tidy -p $(BUILD_DIR) \
        --extra-arg=-std=c++20 {}
	@echo "Lint complete!"

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

# Unit Tests
UNIT_TEST_SRC := tests/unit/capture/test_types.cpp \
				tests/unit/capture/test_capture_config.cpp \
				tests/unit/capture/test_camera_capture.cpp \
				tests/unit/encode/test_encoder_config.cpp \
				tests/unit/pipeline/test_pipeline.cpp \
				src/capture/camera_capture.cpp \
				src/encode/encoder.cpp \
				src/encode/software_encoder.cpp \
                src/encode/nvenc_encoder.cpp \
				src/pipeline/video_pipeline.cpp
UNIT_TEST_BIN := $(BIN_DIR)/video_core_tests
GTEST_FLAGS := -lgtest -lgtest_main -pthread

test: $(UNIT_TEST_BIN)
	@echo "Running unit tests..."
	@$(UNIT_TEST_BIN)

$(UNIT_TEST_BIN): $(UNIT_TEST_SRC) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS) $(GTEST_FLAGS)
	@echo "Built unit tests -> $@"

PIPELINE_TEST_SRC := tests/integration/test_video_pipeline.cpp \
                     src/pipeline/video_pipeline.cpp \
                     src/capture/camera_capture.cpp \
					 src/encode/encoder.cpp \
                     src/encode/software_encoder.cpp \
                     src/encode/nvenc_encoder.cpp

PIPELINE_TEST_BIN := $(BIN_DIR)/test_pipeline

pipeline_test: $(PIPELINE_TEST_BIN)

$(PIPELINE_TEST_BIN): $(PIPELINE_TEST_SRC) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

run_pipeline_test: $(PIPELINE_TEST_BIN)
	@echo "Running pipeline integration test..."
	@$(PIPELINE_TEST_BIN)

NVENC_PIPELINE_TEST_SRC := tests/integration/test_nvenc_video_pipeline.cpp \
                     src/pipeline/video_pipeline.cpp \
                     src/capture/camera_capture.cpp \
					 src/encode/encoder.cpp \
                     src/encode/software_encoder.cpp \
                     src/encode/nvenc_encoder.cpp

NVENC_PIPELINE_TEST_BIN := $(BIN_DIR)/test_nvenc_pipeline

pipeline_test: $(NVENC_PIPELINE_TEST_BIN)

$(NVENC_PIPELINE_TEST_BIN): $(NVENC_PIPELINE_TEST_SRC) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

run_nvenc_pipeline_test: $(NVENC_PIPELINE_TEST_BIN)
	@echo "Running NVENC pipeline integration test..."
	@$(NVENC_PIPELINE_TEST_BIN)

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

# Capture pipeline test
CAPTURE_PIPELINE_SRC := tests/manual/capture_pipeline.cpp
CAPTURE_PIPELINE_OBJ := $(OBJ_DIR)/capture_pipeline.o
CAPTURE_PIPELINE_BIN := $(BIN_DIR)/capture_pipeline

capture_pipeline: $(CAPTURE_PIPELINE_BIN)

$(CAPTURE_PIPELINE_BIN): $(CAPTURE_PIPELINE_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) -pthread
	@echo "Built capture_pipeline -> $@"

$(OBJ_DIR)/capture_pipeline.o: $(CAPTURE_PIPELINE_SRC) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

run_capture_pipeline: $(CAPTURE_PIPELINE_BIN)
	@echo "Running capture pipeline..."
	@$(CAPTURE_PIPELINE_BIN)