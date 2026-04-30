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
import QtQuick.Layouts
import types
import session
import ui
import ui.controls

/*
    PROPERTIES

        user_type:int - Reflects the user's role and manages either DirectorTransport for directors or CameraSessionController 
            for operators based on the value
        max_camera_count:int - Determines how many thumbnails the thumbnail list contains
        room_code:string - Determines what room code is displayed
        livekit_token:string - Determines the LiveKit token that will be passed when DirectorTransport is creating a room
        livekit_url:string - Determines the LiveKit URL that will be passed when DirectorTransport is creating a room
        whip_url:string - Determines the WHIP URL that will be passed when the CameraSessionController starts publishing
        stream_key:string - Determines the stream key that will be passed when the CameraSessionController starts publishing

    SIGNALS

        sessionCloseRequested(roomCode:string) - Fires when the director selectes the 'Close Session' button in the footer
        closePage() - Fires when the user selects the 'Leave' button in the footer

 */
ColumnLayout {
    id: dl_root_layout

    property int user_type: UserRole.director
    property int max_camera_count: 0
    property string room_code: "XXXX-XXXX"

    property string livekit_token: ""
    property string livekit_url: ""

    property string whip_url: ""
    property string stream_key: ""

    property string data_token: ""
    property string data_livekit_url: ""

    property double dc_one_way_ms: 0.0
    property double video_lag_ms: 0.0
    property double display_gap_ms: 0.0
    property int video_width: 0
    property int video_height: 0

    property bool isClosing: false

    spacing: 15

    signal sessionCloseRequested(string roomCode)
    signal closePage()

    Component.onCompleted: {
        if (dl_root_layout.user_type === UserRole.director) {
            DirectorTransport.connectToRoom(dl_root_layout.livekit_token, dl_root_layout.livekit_url);
        } else {
            CameraSessionController.start(dl_root_layout.whip_url, dl_root_layout.stream_key);
            if (dl_root_layout.data_token.length > 0)
                CameraLatencySender.start(dl_root_layout.data_token, dl_root_layout.data_livekit_url);
        }
    }

    Component.onDestruction: {
        if (dl_root_layout.user_type === UserRole.director) {
            DirectorTransport.disconnectFromRoom();
        } else {
            CameraSessionController.stop();
            CameraLatencySender.stop();
        }
    }

    SessionInfo {
        id: dl_session_details

        Layout.topMargin: 10

        room_code: dl_root_layout.room_code
        latency_ms: dl_root_layout.dc_one_way_ms + dl_root_layout.video_lag_ms + dl_root_layout.display_gap_ms
        dc_one_way_ms: dl_root_layout.dc_one_way_ms
        video_lag_ms: dl_root_layout.video_lag_ms
        display_gap_ms: dl_root_layout.display_gap_ms
        show_latency: dl_root_layout.user_type === UserRole.director
        video_width: dl_root_layout.video_width
        video_height: dl_root_layout.video_height
    }

    Loader {
        id: dl_session_view_loader

        Layout.margins: 15
        Layout.fillWidth: true
        Layout.fillHeight: true

        sourceComponent: (dl_root_layout.user_type === UserRole.director) ? dl_session_director_view : dl_session_operator_view
    }

    Footer {
        id: dl_session_footer

        Layout.fillWidth: true
        Layout.preferredHeight: 100

        showCloseButton: dl_root_layout.user_type === UserRole.director

        onLeavePage: () => {
            dl_root_layout.isClosing = true;
            dl_root_layout.closePage();
        }

        onCloseClicked: () => {
            dl_root_layout.sessionCloseRequested(dl_root_layout.room_code);
        }
    }

    Connections {
        target: DirectorTransport

        function onDisconnected() {
            if (dl_root_layout.user_type === UserRole.director) {
                if (dl_root_layout.isClosing) {
                    return;
                }
                dl_root_layout.closePage();
            }
        }

        function onLatencyBreakdown(dcMs, videoMs, gapMs) {
            dl_root_layout.dc_one_way_ms = dcMs;
            dl_root_layout.video_lag_ms = videoMs;
            dl_root_layout.display_gap_ms = gapMs;
        }

        function onVideoResolutionChanged(w, h) {
            dl_root_layout.video_width = w;
            dl_root_layout.video_height = h;
        }
    }

    // Forward per-frame capture timestamps to CameraLatencySender so it uses
    // actual capture times instead of the 33 ms fallback timer.
    Connections {
        target: CameraSessionController

        function onFrameCaptured(captureNs) {
            CameraLatencySender.onFrameCaptured(captureNs);
        }
    }

    Component {
        id: dl_session_director_view
        SessionDirectorView {
            max_camera_count: dl_root_layout.max_camera_count
        }
    }

    Component {
        id: dl_session_operator_view
        SessionOperatorView {

        }
    }
}
