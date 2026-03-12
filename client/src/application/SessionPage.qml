/*
 * File: SessionPage.qml
 * Author: Justin Williams
 * Date: 2/12/26
 * File Description: The qml file that contains the session page. There is a bar with
 * session details, an area for the active camera, a console for receiving backend
 * messages, and a side bar for previewing the available cameras. The 'Leave' button
 * will return the user to the previous page they were on.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import network
import ui
import ui.controls

ColumnLayout {
    id: dl_root_layout

    property string user_id: ""
    property string user_type: "Director"
    property real max_camera_count: 4
    property string room_code: "XXXX-XXXX"

    spacing: 15

    signal sessionCloseRequested(string roomCode)

    SessionInfo {
        id: dl_session_details

        Layout.topMargin: 10

        room_code: dl_root_layout.room_code
    }

    RowLayout {
        id: dl_layout_cameras
        spacing: 15
        Layout.margins: 15

        SessionLog { id: dl_session_log }

        CameraFeed {
            id: dl_active_camera

            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.preferredHeight: width / aspect_ratio
            Layout.minimumHeight: implicitWidth / aspect_ratio
            implicitWidth: 500

            Component.onCompleted: {
                FrameReader.videoSink = dl_active_camera.videoSink
            }
        }

        ThumbnailList {
            id: dl_camera_list
            Layout.fillHeight: true
            max_camera_count: dl_root_layout.max_camera_count
        }
    }

    Footer {
        id: dl_session_footer

        Layout.fillWidth: true
        Layout.preferredHeight: 100

        showCloseButton: user_type === "Director"

        onLeavePage: () => {
                         dl_root_layout.StackView.view.pop()
                     }

        onCloseClicked: () => {
            dl_root_layout.sessionCloseRequested(dl_root_layout.room_code)
        }
    }

    Connections {
        target: SessionClient

        function onSessionClosed(success) {
            if (success)
                dl_root_layout.StackView.view.pop()
        }
    }
}
