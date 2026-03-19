/*
 * File: ConnectionStatusChip.qml
 * Author: Justin Williams
 * Date: 3/19/26
 * File Description: A component that provides a connection indicator. It can be
 * used to reflect the connection status of a module.
 */

import QtQuick
import QtQuick.Layouts
import ui.theme

Rectangle {
    id: dl_chip

    color: Theme.fieldBackground
    radius: dl_chip.height / 2

    states: [
        State {
            name: "disconnected"
            PropertyChanges {
                dl_status_icon.color: Theme.statusDisconnected
                dl_status_label.text: "Disconnected"
            }
        },
        State {
            name: "connecting"
            PropertyChanges {
                dl_status_icon.color: Theme.statusConnecting
                dl_status_label.color: Theme.textWhite
                dl_status_label.text: "Connecting"
            }
        },
        State {
            name: "connected"
            PropertyChanges {
                dl_status_icon.color: Theme.statusConnected
                dl_status_label.color: Theme.textWhite
                dl_status_label.text: "Connected"
            }
        }
    ]

    RowLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 10

        Rectangle {
            id: dl_status_icon

            Layout.preferredWidth: 20
            Layout.preferredHeight: 20

            radius: width / 2

            color: Theme.statusDisconnected
        }

        Item {
            Layout.fillWidth: true
        }

        Text {
            id: dl_status_label

            text: "Disconnected"
            color: Theme.textMuted
            font.pointSize: 14
        }

        Item {
            Layout.fillWidth: true
        }

    }

}
