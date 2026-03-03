qt_add_library(directlink_ui STATIC)

set_target_properties(directlink_ui PROPERTIES AUTOMOC ON)
target_link_libraries(directlink_ui PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
)

qt_add_qml_module(directlink_ui
    URI ui
    VERSION 1.0
    QML_FILES
        Header.qml
        Footer.qml
)
