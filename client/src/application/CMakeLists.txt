qt_add_library(directlink_app STATIC)

set_target_properties(directlink_app PROPERTIES AUTOMOC ON)
target_link_libraries(directlink_app PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
)

qt_add_qml_module(directlink_app
    URI application
    VERSION 1.0
    RESOURCE_PREFIX /src
    QML_FILES
        Main.qml
        SessionPage.qml
)
