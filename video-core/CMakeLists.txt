cmake_minimum_required(VERSION 3.15)
project(DirectLinkVideoCore VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 23)
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

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)

# CPU encoding test executable
add_executable(encode_test
    tests/manual/encode_test.cpp
)

target_link_libraries(encode_test
    ${FFMPEG_LIBRARIES}
)

# NVENC encoding test executable
add_executable(nvenc_test
    tests/manual/nvenc_test.cpp
)

target_link_libraries(nvenc_test
    ${FFMPEG_LIBRARIES}
)

# Set output directory for binaries
set_target_properties(encode_test nvenc_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install targets
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
    tests/unit/capture/test_types.cpp
    tests/unit/capture/test_capture_config.cpp
    tests/unit/capture/test_camera_capture.cpp
)

target_link_libraries(video_core_tests
    GTest::gtest_main
    ${FFMPEG_LIBRARIES}
)

target_include_directories(video_core_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)

include(GoogleTest)
gtest_discover_tests(video_core_tests)