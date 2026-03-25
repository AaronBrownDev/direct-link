/*
 * File: DashboardPage.qml
 * Author: Justin Williams
 * Date: 3/3/26
 * File Description: The qml file that contains the application dashboard. There are two
 * different views: the director view and the operator view. The director view allows
 * the user to see session join and session create controls. The operator
 * view allows the user to see session join and equipment configuration controls.
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import types
import ui.controls
import ui.theme

/*
    PROPERTIES

        user_type:int - Reflects the user's role and updates text displays to match
        can_quick_join:bool - Determines if the 'Quick Join Last Session' button is enabled or not

    SIGNALS

        joinClicked(roomCode:string, cameraName:string) - Fires when the operator has input
            a room code and clicked on the 'Join Session' button. Passes the entered room
            code and entered camera name
        quickJoinClicked() - Fires when the operator has clicked on the 'Quick Join
            Last Session' button
 */
ColumnLayout {
    id: dl_root_layout

    property int user_type: UserRole.director
    property bool can_quick_join: false

    signal joinRequested(string roomCode)
    signal quickJoinRequested
    signal createRequested(int maxCameras)
    signal sessionFetchRequested

    ColumnLayout {
        id: dl_content_layout

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: 40
        spacing: 30

        Text {
            id: dl_dash_title

            Layout.alignment: Qt.AlignLeft

            text: UserRole.toString(dl_root_layout.user_type, true) + " Dashboard"
            color: Theme.textWhite
            font.pointSize: 36
            font.bold: true
        }

        Loader {
            id: dl_dash_view_loader
            Layout.fillWidth: true
            sourceComponent: dl_root_layout.user_type === UserRole.director ? dl_dash_director_view : dl_dash_operator_view
        }

        RecentSessionList {
            id: dl_dash_recent_sessions

            Layout.minimumHeight: 200

            visible: dl_root_layout.user_type === UserRole.director

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
            can_quick_join: dl_root_layout.can_quick_join

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
            can_quick_join: dl_root_layout.can_quick_join

            onJoinClicked: (roomCode, cameraName) => {
                dl_root_layout.joinRequested(roomCode);
            }

            onQuickJoinClicked: () => {
                dl_root_layout.quickJoinRequested();
            }
        }
    }
}
