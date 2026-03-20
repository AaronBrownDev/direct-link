cmake_minimum_required(VERSION 3.20)

project(direct-link VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS  Core Quick QuickControls2 Multimedia Concurrent Protobuf Grpc)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavutil
    libavdevice
)

pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
)

set(PROJECT_SOURCES
        src/main.cpp
)


qt_add_executable(direct-link
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        resources.qrc
    )

qt_add_executable(test_signaling_client
    tests/test_signaling_client.cpp
)

qt_add_executable(test_livekit_room
    tests/test_livekit_room.cpp
)

# QTP0001: Changes default RESOURCE_PREFIX for qt_add_qml_module from "/"
# to "qt/qml/". Removes the need to call addImportPath manually.
qt_policy(SET QTP0001 NEW)

set(QML_IMPORT_PATH
    ${CMAKE_BINARY_DIR}/src
    CACHE STRING "Qt Creator extra qml import paths" FORCE
)


# Protobuf and gRPC code generation from signaling.proto
set(SIGNALING_PROTO "${CMAKE_CURRENT_SOURCE_DIR}/../shared/protocol/signaling.proto")

qt_add_protobuf(signaling_proto
  PROTO_FILES ${SIGNALING_PROTO}
)

qt_add_grpc(signaling_grpc CLIENT
  PROTO_FILES ${SIGNALING_PROTO}
)

target_link_libraries(signaling_grpc PRIVATE signaling_proto)

# LiveKit SDK (pre-built in container)
set(LIVEKIT_DIR /home/vscode/livekit-cpp)

add_library(livekit SHARED IMPORTED)
set_target_properties(livekit PROPERTIES
    IMPORTED_LOCATION ${LIVEKIT_DIR}/build/lib/liblivekit.so
    INTERFACE_INCLUDE_DIRECTORIES ${LIVEKIT_DIR}/include
)

add_library(livekit_ffi SHARED IMPORTED)
set_target_properties(livekit_ffi PROPERTIES
    IMPORTED_LOCATION ${LIVEKIT_DIR}/build/lib/liblivekit_ffi.so
)

add_subdirectory(../video-core ${CMAKE_BINARY_DIR}/video-core)
add_subdirectory(../networking ${CMAKE_BINARY_DIR}/networking)
add_subdirectory(src/application)
add_subdirectory(src/ui)
add_subdirectory(src/ui/controls)
add_subdirectory(src/ui/theme)
add_subdirectory(src/network)
add_subdirectory(src/session)

target_include_directories(direct-link PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIVEKIT_DIR}/include
)

target_include_directories(test_signaling_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network
    ${CMAKE_BINARY_DIR}
)

target_include_directories(test_livekit_room PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(direct-link PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::Protobuf
    Qt6::Grpc
    ${FFMPEG_LIBRARIES}
    signaling_proto
    signaling_grpc
    livekit
    livekit_ffi
    directlink_appplugin
    directlink_uiplugin
    directlink_ui_controlsplugin
    directlink_ui_themeplugin
    directlink_networkplugin
    directlink_sessionplugin
)

target_link_libraries(test_signaling_client PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Protobuf
    Qt6::Grpc
    signaling_proto
    signaling_grpc
    directlink_networkplugin
)

target_link_libraries(test_livekit_room PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Multimedia
    Qt6::Concurrent
    Qt6::Protobuf
    Qt6::Grpc
    signaling_proto
    signaling_grpc
    livekit
    livekit_ffi
    directlink_networkplugin
    directlink_appplugin
    video_core
)

if(Qt6_VERSION VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.direct-link)
endif()
set_target_properties(direct-link PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS direct-link
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(direct-link)
endif()
