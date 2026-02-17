import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: dl_bg_footer
    color: "#1E293B"
    Layout.fillWidth: true
    Layout.preferredHeight: 100

    RowLayout {
        id: dl_layout_footer

        anchors {
            fill: parent
            leftMargin: 25
            rightMargin: 25
        }

        spacing: 10

        Text {
            id: dl_label_field_room_id
            text: "Room ID"
            color: "white"
            font.pointSize: 18
        }

        Rectangle {
            id: dl_bg_field_room_id
            width: 400
            height: 50
            color: "#0F172A"
            radius: 5

            Layout.rightMargin: 20

            TextInput {
                id: dl_field_room_id
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 15
                width: parent.width
                color: "white"
                maximumLength: 12
                font.pointSize: 18
            }
        }

        Button {
            id: dl_control_connect
            Layout.preferredWidth: 120
            Layout.preferredHeight: 50
            background: Rectangle {
                radius: 25
                color: dl_control_connect.down ? "#6AE276" : "#77FF85"
                Text {
                    text: "Connect"
                    font.pointSize: 15
                    color: "black"
                    anchors.centerIn: parent
                }
            }
        }

        Item { Layout.fillWidth: true }

        Button {
            id: dl_control_app_exit
            Layout.preferredWidth: 120
            Layout.preferredHeight: 50
            background: Rectangle {
                radius: 25
                color: dl_control_app_exit.down ? "#B02120" : "#EC221F"
                Text {
                    text: "Leave"
                    font.pointSize: 15
                    color: "white"
                    anchors.centerIn: parent
                }
            }
            onClicked: Qt.quit()
        }

    }


}
