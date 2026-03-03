qt_add_library(directlink_app STATIC)

set_target_properties(directlink_app PROPERTIES AUTOMOC ON)

target_include_directories(directlink_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(directlink_app PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    ${FFMPEG_LIBRARIES}
)

qt_add_qml_module(directlink_app
    URI application
    VERSION 1.0
    QML_FILES
        Main.qml
        SessionPage.qml
        DashboardPage.qml
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../framereader.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../framereader.cpp
)
