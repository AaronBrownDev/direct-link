import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ui.theme

Rectangle {
    id: dl_header

    property string user_type: "Director"

    Layout.fillWidth: true
    Layout.preferredHeight: 75

    color: Theme.surface

    signal settingsClicked()

    Text {
        id: dl_header_logo

        text: "DirectLink"
        anchors {
            left: parent.left
            verticalCenter: parent.verticalCenter
        }
        anchors.leftMargin: 20
        color: Theme.textWhite
        font.bold: true
        font.pointSize: 24
    }

    Text {
        id: dl_header_type

        text: " | " + user_type
        anchors {
            left: dl_header_logo.right
            verticalCenter: dl_header_logo.verticalCenter
        }
        color: Theme.textMuted
        font.bold: true
        font.pointSize: 20
    }

    RoundButton {
        id: dl_control_app_settings
        radius: 35
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 20
        icon {
            source: "qrc:/resources/icons/settings.png"
            width: radius
            height: radius
        }
        onClicked: dl_header.settingsClicked()
    }

}
