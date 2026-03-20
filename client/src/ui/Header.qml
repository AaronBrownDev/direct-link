import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ui.theme
import ui.controls

Rectangle {
    id: dl_header

    property string user_type: "Director"
    property string connection_status: "disconnected"

    Layout.fillWidth: true
    Layout.preferredHeight: 75

    color: Theme.surface

    signal homeClicked()
    signal settingsClicked()

    RowLayout {
        id: dl_header_layout

        anchors.fill: parent
        anchors.rightMargin: 20
        anchors.leftMargin: 20

        Text {
            id: dl_header_logo

            text: "DirectLink"
            Layout.leftMargin: 20
            color: Theme.textWhite
            font.bold: true
            font.pointSize: 24

            MouseArea {
                id: dl_logo_area

                anchors.fill: parent

                onClicked: dl_header.homeClicked()
            }
        }

    Text {
        id: dl_header_type

        text: " | " + user_type
        color: Theme.textMuted
        font.bold: true
        font.pointSize: 20
    }

    Item {Layout.fillWidth: true}

    ConnectionStatusChip {
        id: dl_connection_status_chip

        Layout.preferredWidth: 200
        Layout.preferredHeight: 50
        Layout.rightMargin: 20

        state: dl_header.connection_status
    }

    RoundButton {
        id: dl_control_app_settings
        radius: 35
        icon {
            source: "qrc:/resources/icons/settings.png"
            width: radius
            height: radius
        }
        onClicked: dl_header.settingsClicked()
    }

    }

}
