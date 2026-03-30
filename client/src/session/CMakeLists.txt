qt_add_library(directlink_session STATIC)

set_target_properties(directlink_session PROPERTIES AUTOMOC ON)

target_include_directories(directlink_session PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GST_INCLUDE_DIRS}
)

target_link_libraries(directlink_session PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Concurrent
    Qt6::Multimedia
    video_core
    directlink_networking
)

qt_add_qml_module(directlink_session
    URI session
    VERSION 1.0
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/camera_session.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/camera_session.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/session_controller.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/session_controller.cpp
)