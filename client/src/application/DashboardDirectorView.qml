/*
 * File: DashboardDirectorView.qml
 * Author: Justin Williams
 * Date: 3/5/26
 * File Description: The qml file that contains the session controls for the director.
 * A director can join and create sessions.
 */

import QtQuick
import QtQuick.Layouts
import ui.controls
import ui.theme

/*
    SIGNALS

    joinClicked(string roomCode) - Fires when the director has input a room code
        and clicked on the 'Join Session' button. Passes the entered room code.
    quickJoinClicked () - Fires when the director has clicked on the 'Quick Join
        Last Session' button.
    createClicked(string projectName, string sessionDesc, string qualitySettings,
        int cameraCount) - Fires when the director has clicked on the 'Create Session'
        button. Passes the entered project name, session description, quality settings,
        and camera count
 */
RowLayout {
    id: dl_dash_view_layout

    property bool canQuickJoin: false

    signal joinClicked(string roomCode)
    signal quickJoinClicked()
    signal createClicked(string projectName, string sessionDesc, string qualitySettings, int cameraCount)

    spacing: 20    

    JoinSessionView {
        id: dl_join_session_view

        Layout.fillWidth: true
        Layout.preferredHeight: 550

        showCameraField: false
        canQuickJoin: dl_dash_view_layout.canQuickJoin

        onJoinClicked: (roomCode, cameraName) => dl_dash_view_layout.joinClicked(roomCode)
        onQuickJoinClicked: () => dl_dash_view_layout.quickJoinClicked()
    }

    Rectangle {
        id: dl_create_session_bg

        Layout.fillWidth: true
        Layout.preferredHeight: 550

        color: Theme.surface
        radius: 15

        ColumnLayout {
            id: dl_create_session_layout

            anchors.fill: parent
            anchors.margins: 20

            Text {
                id: dl_create_session_title

                text: "Create Session"
                color: Theme.textWhite
                font.pointSize: 25
                font.bold: true
            }

            DLTextField {
                id: dl_project_name

                Layout.topMargin: 50
                Layout.preferredHeight: 65
                Layout.fillWidth: true

                label: "Project Name"
                emptyText: "Project Name"
                maxLength: 24
            }

            DLTextField {
                id: dl_session_desc

                Layout.topMargin: 40
                Layout.preferredHeight: 65
                Layout.fillWidth: true

                label: "Session Description"
                maxLength: 100
            }

            DLTextField {
                id: dl_quality_settings

                Layout.topMargin: 40
                Layout.preferredHeight: 65
                Layout.fillWidth: true

                label: "Quality Settings"
                maxLength: 100
            }

            DLCounter {
                id: dl_camera_counter

                Layout.topMargin: 10
                Layout.preferredHeight: 40
                Layout.preferredWidth: 150
                Layout.alignment: Qt.AlignLeft

                label: "Add Cameras"
                maximum: 4
            }

            Item { Layout.fillHeight: true }

            DLButton {
                id: dl_create_button

                Layout.fillWidth: true
                Layout.preferredHeight: 65

                buttonType: DLButton.ButtonType.Primary
                buttonText: "Create Session"

                onClicked: createClicked(
                               dl_project_name.input,
                               dl_session_desc.input,
                               dl_quality_settings.input,
                               dl_camera_counter.value
                            )
            }
        }
    }
}
