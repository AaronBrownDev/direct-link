import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ui.theme

ColumnLayout {
    id: dl_session_list_layout

    signal sessionSelected(string roomCode, int maxCameras)

    spacing: 15

    Text {
        id: dl_session_list_label

        Layout.alignment: Qt.AlignLeft

        text: "Recent Sessions"
        color: Theme.textWhite
        font.pointSize: 20
        font.bold: true
    }

    Rectangle {
        id: dl_session_list_bg

        Layout.fillWidth: true
        Layout.minimumHeight: 50
        Layout.fillHeight: true

        color: Theme.surface
        radius: 15

        ListView {
            id: dl_session_list

            anchors.fill: parent
            anchors.margins: 15
            clip: true
            spacing: 10

            // Placeholder
            model: ListModel {
                id: dl_session_model
                ListElement { roomCode: "ROOM-1234"; roomStatus: "Open"; timestamp: "12:37 PM"; maxCameras: 4 }
                ListElement { roomCode: "ROOM-5678"; roomStatus: "Closed"; timestamp: "08:00 AM"; maxCameras: 2 }
            }

            delegate: Rectangle {
                width: ListView.view.width
                height: 60
                color: dl_session_list_area.containsMouse ? Theme.innerSurface : Theme.surface
                radius: 10

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 5
                    Text {
                        Layout.alignment: Qt.AlignLeft
                        text: roomCode
                        color: Theme.textWhite
                        font.bold: true
                    }
                    Text {
                        Layout.alignment: Qt.AlignLeft
                        text: timestamp + " - " + roomStatus + " - " + maxCameras + " Cameras"
                        color: Theme.textMuted
                    }
                }

                MouseArea {
                    id: dl_session_list_area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: dl_session_list_layout.sessionSelected(roomCode, maxCameras)
                }

            }

        }

        Text {
            anchors.centerIn: parent
            visible: dl_session_model.count === 0
            text: "No sessions found"
            color: Theme.textMuted
            font.pointSize: 15
        }

    }


}
