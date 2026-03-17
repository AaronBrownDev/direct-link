cmake_minimum_required(VERSION 3.15)
project(DirectLinkVideoCore VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find FFmpeg libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavformat
    libavutil
    libswscale
)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# =================================================================
# video_core library
# =================================================================
add_library(video_core STATIC
    src/capture/camera_capture.cpp
    src/encode/encoder.cpp
    src/encode/software_encoder.cpp
    src/encode/nvenc_encoder.cpp
    src/decode/decoder.cpp
    src/decode/software_decoder.cpp
    src/decode/nvdec_decoder.cpp
    src/pipeline/video_pipeline.cpp
)

target_include_directories(video_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(video_core PUBLIC
    ${FFMPEG_LIBRARIES}
)

# =================================================================
# Manual test executables
# =================================================================
add_executable(encode_test
    tests/manual/encode_test.cpp
)

target_link_libraries(encode_test PRIVATE
    video_core
)

add_executable(nvenc_test
    tests/manual/nvenc_test.cpp
)

target_link_libraries(nvenc_test PRIVATE
    video_core
)

set_target_properties(encode_test nvenc_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

install(TARGETS encode_test nvenc_test
    RUNTIME DESTINATION bin
)

# =================================================================
# Google Test
# =================================================================
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/v1.17.0.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(video_core_tests
    tests/unit/common/test_types.cpp
    tests/unit/capture/test_capture_config.cpp
    tests/unit/capture/test_camera_capture.cpp
    tests/unit/encode/test_encoder_config.cpp
    tests/unit/decode/test_decoder.cpp
    tests/unit/pipeline/test_pipeline.cpp
)

target_link_libraries(video_core_tests PRIVATE
    GTest::gtest_main
    video_core
)

include(GoogleTest)
gtest_discover_tests(video_core_tests)