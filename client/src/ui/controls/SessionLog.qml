import QtQuick
import QtQuick.Layouts
import ui.theme

Rectangle {
    id: dl_bg_log
    Layout.fillHeight: true
    Layout.preferredWidth: 460

    function logMessage(type, message) {
        const msgColor = {
                    "debug": Theme.textWhite,
                    "info": Theme.logInfo,
                    "warning": Theme.logWarning,
                    "critical": Theme.logError,
                    "fatal": Theme.logError
                }[type] ?? Theme.textWhite;

                dl_session_console.appendLine(message, msgColor);
    }

    color: Theme.surface
    radius: 15

    DLConsole {
        id: dl_session_console

        anchors.fill: parent
        anchors {
            leftMargin: 25
            rightMargin: 25
            bottomMargin: 25
            topMargin: 50
        }

        label: "Session Log"
    }
}
