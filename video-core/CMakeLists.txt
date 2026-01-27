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
add_executable(encode_te1.0.0st
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