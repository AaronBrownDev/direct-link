/*
 * File: Main.qml
 * Author: Justin Williams
 * Date: 2/10/26
 * File Description: The qml file that the application loads on startup. It
 * contains a StackView that manages application pages and specifies properties
 * for the Window.
 */

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import network
import ui
import ui.theme
import ui.controls

Window {
        id: root

        property string user_id: "director-1"
        property string user_type: "Director"
        // property string user_id: "operator-1"
        // property string user_type: "Operator"
        property real max_camera_count: 4
        property url channel: "http://localhost:50051"

        property string pending_close_room: ""

        visible: true
        minimumWidth: 1400
        minimumHeight: 1100
        width: minimumWidth
        height: minimumHeight
        color: Theme.background
        title: "DirectLink"

        Component.onCompleted: {
            SessionClient.connectToServer(channel)
        }

        ColumnLayout {
            id: dl_main_layout

            anchors.fill: parent

            Header {
                id: dl_dash_header
                user_type: root.user_type
            }

            StackView {
                id: dl_page_stack

                Layout.fillWidth: true
                Layout.fillHeight: true

                initialItem: dl_dashboard_component
            }
        }

        DLPopup {
            id: dl_session_close_popup

            inputType: DLPopup.InputType.ConfirmCancel
            displayText: "Are you sure you want to close this session?"
            onConfirmed: SessionClient.closeSession(root.pending_close_room, root.user_id)
        }

        DLPopup {
            id: dl_info_popup

            inputType: DLPopup.InputType.Cancel
        }

        Component {
            id: dl_dashboard_component
            DashboardPage {
                user_id: root.user_id
                user_type: root.user_type
            }
        }

        Connections {
            target: dl_page_stack.currentItem
            ignoreUnknownSignals: true

            function onSessionCloseRequested(roomCode) {
                root.pending_close_room = roomCode
                dl_session_close_popup.open()
            }

            function onRoomCodeReceived(roomCode) {
                dl_info_popup.displayText = "Room Successfully Created: " + roomCode
                dl_info_popup.open()
            }
        }
}
