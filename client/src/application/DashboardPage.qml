/*
 * File: DashboardPage.qml
 * Author: Justin Williams
 * Date: 3/3/26
 * File Description: The qml file that contains the application dashboard. There are two
 * different views: the director view and the operator view. The director view allows
 * the user to see session join and session create controls. The operator
 * view allows the user to see session join and equipment configuration controls.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import network
import ui
import ui.controls
import ui.theme

ColumnLayout {
    id: dl_root_layout

    property string user_id: ""
    property string user_type: "Director"
    property string last_room: ""

    signal roomCodeReceived(string roomCode)

    ColumnLayout {
        id: dl_content_layout

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: 60
        spacing: 30

        Text {
            id: dl_dash_title

            Layout.alignment: Qt.AlignLeft

            text: user_type + " Dashboard"
            color: Theme.textWhite
            font.pointSize: 36
            font.bold: true
        }

        Loader {
            id: dl_dash_view_loader
            Layout.fillWidth: true
            sourceComponent: dl_root_layout.user_type === "Director" ? dl_dash_director_view : dl_dash_operator_view
        }



        RecentSessionList {
            id: dl_dash_recent_sessions

            Layout.minimumHeight: 200

            onSessionSelected: (roomCode, maxCameras) => {
                dl_root_layout.last_room = roomCode
                SessionClient.joinSession(roomCode, dl_root_layout.user_id, dl_root_layout.user_type.toLowerCase())
            }

            onRefreshClicked: () => {
                SessionClient.getMySessions(dl_root_layout.user_id)
            }
        }
    }

    Component {
        id: dl_dash_director_view
        DashboardDirectorView {
            canQuickJoin: last_room.length === 11 ? true : false

            onJoinClicked: (roomCode) => {
                dl_root_layout.last_room = roomCode
                SessionClient.joinSession(roomCode, dl_root_layout.user_id, "director")
            }

            onQuickJoinClicked: () => {
                SessionClient.joinSession(dl_root_layout.last_room, dl_root_layout.user_id, "director")
            }

            onCreateClicked: (projectName, sessionDesc, qualitySettings, cameraCount) => {
                SessionClient.createSession(dl_root_layout.user_id, cameraCount)
            }
        }
    }

    Component {
        id: dl_dash_operator_view
        DashboardOperatorView {
            canQuickJoin: last_room.length === 11 ? true : false
            
            onJoinClicked: (roomCode, cameraName) => {
                dl_root_layout.last_room = roomCode
                SessionClient.joinSession(roomCode, dl_root_layout.user_id, "camera")
            }

            onQuickJoinClicked: () => {
                SessionClient.joinSession(dl_root_layout.last_room, dl_root_layout.user_id, "camera")
            }
        }
    }

    Connections {
        target: SessionClient

        function onDirectorJoined(token, livekitUrl) {
            dl_root_layout.StackView.view.push(dl_session_page_component, {
                user_id: dl_root_layout.user_id,
                user_type: "Director",
                room_code: dl_root_layout.last_room
            })
        }

        function onCameraJoined(whipUrl, streamKey) {
            dl_root_layout.StackView.view.push(dl_session_page_component, {
                  user_id: dl_root_layout.user_id,
                  user_type: "Operator",
                  room_code: dl_root_layout.last_room
                })
        }

        function onSessionCreated(roomCode) {
            dl_root_layout.roomCodeReceived(roomCode)
        }
    }

    Component {
        id: dl_session_page_component
        SessionPage {}
    }

}
