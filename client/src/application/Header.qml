import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: dl_header

    color: "#1E293B"
    Layout.fillWidth: true
    Layout.preferredHeight: 75

    Text {
        id: dl_header_logo

        text: "DirectLink"
        anchors {
            left: parent.left
            verticalCenter: parent.verticalCenter
        }
        anchors.leftMargin: 20
        color: "white"
        font.bold: true
        font.pointSize: 24
    }

    Text {
        id: dl_header_type

        text: " | " + root.user_type
        anchors {
            left: dl_header_logo.right
            verticalCenter: dl_header_logo.verticalCenter
        }
        color: "#94A3B8"
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

    }


}
