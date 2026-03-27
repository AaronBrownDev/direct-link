qt_add_library(directlink_ui_controls STATIC)

set_target_properties(directlink_ui_controls  PROPERTIES AUTOMOC ON)
target_link_libraries(directlink_ui_controls  PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
)

qt_add_qml_module(directlink_ui_controls
    URI ui.controls
    VERSION 1.0
    QML_FILES
        CameraFeed.qml
        SessionInfo.qml
        SessionLog.qml
        Thumbnail.qml
        CameraList.qml
        RecentSessionList.qml
        ConnectionStatusChip.qml
        MiniProfile.qml
        DLTextField.qml
        DLButton.qml
        DLCounter.qml
        DLConsole.qml
        DLPopup.qml
        DLCheckbox.qml
        DLRadioButton.qml
)
