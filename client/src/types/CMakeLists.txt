qt_add_library(directlink_types STATIC)

set_target_properties(directlink_types  PROPERTIES AUTOMOC ON)
target_link_libraries(directlink_types  PRIVATE
    Qt6::Quick
)

set_source_files_properties(UserRole.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(directlink_types
    URI types
    VERSION 1.0
    QML_FILES
        UserRole.qml
)
