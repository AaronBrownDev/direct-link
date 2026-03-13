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

/*
    PROPERTIES

        canQuickJoin:bool - Determines if the 'Quick Join Last Session' button is enabled or not

    SIGNALS

        joinClicked(roomCode:string, cameraName:string) - Fires when the operator has input
            a room code and clicked on the 'Join Session' button. Passes the entered room
            code and entered camera name
        quickJoinClicked() - Fires when the operator has clicked on the 'Quick Join
            Last Session' button
 */
ColumnLayout {
    id: dl_root_layout

    property string user_id: ""
    property string user_type: "Director"
    property bool canQuickJoin: false

    signal joinRequested(string roomCode)
    signal quickJoinRequested
    signal createRequested(int maxCameras)
    signal sessionFetchRequested

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

            visible: user_type === "Director"

            onSessionSelected: (roomCode, maxCameras) => {
                dl_root_layout.joinRequested(roomCode);
            }

            onRefreshClicked: () => {
                dl_root_layout.sessionFetchRequested();
            }
        }
    }

    Component {
        id: dl_dash_director_view
        DashboardDirectorView {
            canQuickJoin: dl_root_layout.canQuickJoin

            onJoinClicked: roomCode => {
                dl_root_layout.joinRequested(roomCode);
            }

            onQuickJoinClicked: () => {
                dl_root_layout.quickJoinRequested();
            }

            onCreateClicked: (projectName, sessionDesc, qualitySettings, maxCameras) => {
                dl_root_layout.createRequested(maxCameras);
            }
        }
    }

    Component {
        id: dl_dash_operator_view
        DashboardOperatorView {
            canQuickJoin: dl_root_layout.canQuickJoin

            onJoinClicked: (roomCode, cameraName) => {
                dl_root_layout.joinRequested(roomCode);
            }

            onQuickJoinClicked: () => {
                dl_root_layout.quickJoinRequested();
            }
        }
    }
}
