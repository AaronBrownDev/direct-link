qt_add_library(directlink_ui_theme STATIC)

set_target_properties(directlink_ui_theme  PROPERTIES AUTOMOC ON)
target_link_libraries(directlink_ui_theme  PRIVATE
    Qt6::Quick
)

set_source_files_properties(Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(directlink_ui_theme
    URI ui.theme
    VERSION 1.0
    QML_FILES
        Theme.qml
)
