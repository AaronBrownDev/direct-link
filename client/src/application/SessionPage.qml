/*
 * File: SessionPage.qml
 * Author: Justin Williams
 * Date: 2/12/26
 * File Description: The qml file that contains the session page. There is a bar with
 * session details, an area for the active camera, a console for receiving backend
 * messages, and a side bar for previewing the available cameras. The 'Leave' button
 * will return the user to the previous page they were on. The 'Close Session' button
 * is visible only to directors and will prompt the user to confirm a session close.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import session
import ui
import ui.controls

ColumnLayout {
    id: dl_root_layout

    property string user_type: "Director"
    property real max_camera_count: 4
    property string room_code: "XXXX-XXXX"

    property string livekit_token: ""
    property string livekit_url: ""

    property string whip_url: ""
    property string stream_key: ""

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

        SessionLog {
            id: dl_session_log
        }

        CameraFeed {
            id: dl_active_camera

            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.preferredHeight: width / aspect_ratio
            Layout.minimumHeight: implicitWidth / aspect_ratio
            implicitWidth: 500

            Component.onCompleted: {
                // TODO: Restore when FrameReader pushes actual frames
                // FrameReader.videoSink = dl_active_camera.videoSink
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

        showCloseButton: dl_root_layout.user_type === "Director"

        onLeavePage: () => {
            if (dl_root_layout.user_type === "Director") {
            DirectorTransport.disconnectFromRoom()
            } else {
                CameraSessionController.stop()
            }
            
            dl_root_layout.StackView.view.pop();
        }

        onCloseClicked: () => {
            dl_root_layout.sessionCloseRequested(dl_root_layout.room_code);
        }
    }

    Component.onCompleted: {
        if (dl_root_layout.user_type === "Director") {
            DirectorTransport.connectToRoom(dl_root_layout.livekit_token, dl_root_layout.livekit_url)
        } else {
            CameraSessionController.start(dl_root_layout.whip_url, dl_root_layout.stream_key)
        }
    }

    Connections {
        target: DirectorTransport

        function onConnected() {
            let session = DirectorTransport.session
            session.videoSink = dl_active_camera.videoSink
        }
    }
}
