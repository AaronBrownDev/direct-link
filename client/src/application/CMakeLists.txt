qt_add_library(directlink_app STATIC)

set_target_properties(directlink_app PROPERTIES AUTOMOC ON)

target_include_directories(directlink_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${FFMPEG_INCLUDE_DIRS}
    ${LIVEKIT_DIR}/include
)

target_link_libraries(directlink_app PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::Concurrent
    Microsoft.GSL::GSL
    ${FFMPEG_LIBRARIES}
    livekit
    livekit_ffi
    video_core
)

qt_add_qml_module(directlink_app
    URI application
    VERSION 1.0
    QML_FILES
        Main.qml
        LoginPage.qml
        DashboardPage.qml
        SessionPage.qml
        views/DashboardDirectorView.qml
        views/DashboardOperatorView.qml
        views/JoinSessionView.qml
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include/framereader.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include/directortransport.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include/directorsession.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include/videotrack.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../framereader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../directortransport.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../directorsession.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../videotrack.cpp
)
