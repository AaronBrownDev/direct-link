cmake_minimum_required(VERSION 3.15)
project(DirectLinkNetworking VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
)

add_library(directlink_networking STATIC)

target_include_directories(directlink_networking
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${GST_INCLUDE_DIRS}
)

target_link_libraries(directlink_networking
    PUBLIC
        ${GST_LIBRARIES}
)

target_compile_options(directlink_networking
    PRIVATE
        ${GST_CFLAGS_OTHER}
)