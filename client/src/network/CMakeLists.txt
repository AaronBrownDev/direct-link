qt_add_library(directlink_network STATIC)

set_target_properties(directlink_network PROPERTIES AUTOMOC ON)

target_include_directories(directlink_network PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(directlink_network PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Protobuf
    Qt6::Grpc
    signaling_proto
    signaling_grpc
)

qt_add_qml_module(directlink_network
    URI network
    VERSION 1.0
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/sessionclient.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/sessionclient.cpp
)
