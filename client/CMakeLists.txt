cmake_minimum_required(VERSION 3.16)

project(direct-link VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS  Core Quick QuickControls2 Multimedia)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavutil
)

set(PROJECT_SOURCES
        src/main.cpp
)


qt_add_executable(direct-link
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        resources.qrc
    )

# QTP0001: Changes default RESOURCE_PREFIX for qt_add_qml_module from "/"
# to "qt/qml/". Removes the need to call addImportPath manually.
qt_policy(SET QTP0001 NEW)

set(QML_IMPORT_PATH
    ${CMAKE_BINARY_DIR}/src
    CACHE STRING "Qt Creator extra qml import paths" FORCE
)

add_subdirectory(src/application)
add_subdirectory(src/ui)
add_subdirectory(src/ui/controls)
add_subdirectory(src/ui/theme)

target_include_directories(direct-link PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(direct-link PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    directlink_appplugin
    directlink_uiplugin
    directlink_ui_controlsplugin
    directlink_ui_themeplugin
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
