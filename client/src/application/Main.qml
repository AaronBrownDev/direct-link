/*
 * File: Main.qml
 * Author: Justin Williams
 * Date: 2/10/26
 * File Description: The qml file that the application loads on startup. It
 * contains a StackView that manages application pages and specifies properties
 * for the Window.
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import network
import session
import ui
import ui.theme
import ui.controls

Window {
    id: root

    property string user_id: "director-1"
    property string user_type: "Director"

    property url channel: "http://localhost:50051"
    property bool connected: false

    property string last_room: ""
    property string pending_close_room: ""
    property string current_operation: ""

    visible: true
    minimumWidth: 1400
    minimumHeight: 1200
    width: minimumWidth
    height: minimumHeight
    color: Theme.background
    title: "DirectLink"

    Component.onCompleted: {
        SessionClient.connectToServer(channel);
        root.connected = true;
        if (root.user_type === "Director") {
            root.current_operation = "getSessions";
            SessionClient.getMySessions(root.user_id);
        }
    }

    // Header + Page Stack

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

            initialItem: dl_login_component
        }
    }

    // App Popups

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

    DLPopup {
        id: dl_error_popup

        inputType: DLPopup.InputType.Cancel
    }

    // Page Connections

    Connections {
        target: dl_page_stack.currentItem
        ignoreUnknownSignals: true

        function onJoinRequested(roomCode) {
            root.current_operation = "joinSession";
            root.last_room = roomCode;
            SessionClient.joinSession(roomCode, root.user_id, root.user_type.toLowerCase());
        }

        function onQuickJoinRequested() {
            if (root.last_room === "") {
                dl_error_popup.displayText = "Failed to join session. Please try again.";
                dl_error_popup.open();
                return;
            }

            root.current_operation = "joinSession";
            SessionClient.joinSession(root.last_room, root.user_id, root.user_type.toLowerCase());
        }

        function onCreateRequested(maxCameras) {
            root.current_operation = "createSession";
            SessionClient.createSession(root.user_id, maxCameras);
        }

        function onSessionFetchRequested() {
            root.current_operation = "getSessions";
            SessionClient.getMySessions(root.user_id);
        }

        function onSessionCloseRequested(roomCode) {
            root.pending_close_room = roomCode;
            dl_session_close_popup.open();
        }
    }

    // Signaling connections

    Connections {
        target: SessionClient

        function onDirectorJoined(token, livekitUrl) {
            dl_page_stack.push(dl_session_component, {
                user_type: "Director",
                room_code: root.last_room,
                livekit_token: token,
                livekit_url: livekitUrl
            });
        }

        function onCameraJoined(whipUrl, streamKey) {
            dl_page_stack.push(dl_session_component, {
                user_type: "Operator",
                room_code: root.last_room,
                whip_url: whipUrl,
                stream_key: streamKey
            });
        }

        function onSessionCreated(roomCode) {
            dl_info_popup.displayText = "Room Successfully Created: " + roomCode;
            dl_info_popup.open();
            root.current_operation = "joinSession";
            root.last_room = roomCode;
            SessionClient.joinSession(roomCode, root.user_id, root.user_type.toLowerCase());
        }

        function onSessionClosed(success) {
            if (success) {
                if (root.last_room === root.pending_close_room)
                    root.last_room = "";

                root.pending_close_room = "";
                DirectorTransport.disconnectFromRoom()
                dl_page_stack.pop();
            } else {
                dl_error_popup.displayText = "Failed to close session. Please try again.";
                dl_error_popup.open();
            }
        }

        function onError(msg) {
            switch (root.current_operation) {
            case "getSessions":
                dl_error_popup.displayText = "Failed to retrieve sessions. Please try again.";
                dl_error_popup.open();
                return;
            case "joinSession":
                if (msg === "session is closed")
                    dl_error_popup.displayText = "That session has ended.";
                else if (msg === "session not found")
                    dl_error_popup.displayText = "Session was not found. Please try again."
                else
                    dl_error_popup.displayText = "Failed to join session. Please try again.";

                dl_error_popup.open();
                root.last_room = "";
                return;
            case "createSession":
                dl_error_popup.displayText = "Failed to create session. Please try again.";
                dl_error_popup.open();
                return;
            default:
                dl_error_popup.displayText = "A room error has occurred: " + msg;
                dl_error_popup.open();
            }
        }
    }

    // Director Session Connections

    Connections {
        target: DirectorTransport

        function onConnected() {
            dl_dash_header.connection_status = "connected"
        }

        function onDisconnected() {
            dl_dash_header.connection_status = "disconnected"
        }

        function onConnectionStateChanged(newState) {
            dl_dash_header.connection_status = newState
        }
    }

    // Camera Session Connections

    Connections {
        target: CameraSessionController

        function onSessionStarted() {
            dl_info_popup.displayText = "Camera Session Started.";
            dl_info_popup.open();
            dl_dash_header.connection_status = "connected"
        }

        function onSessionStopped() {
            dl_dash_header.connection_status = "disconnected"
        }

        function onErrorOccurred(msg) {
            dl_error_popup.displayText = "A session error has occurred: " + msg;
            dl_error_popup.open();
        }
        
    }

    // Page Components

    Component {
        id: dl_login_component
        LoginPage {}
    }

    Component {
        id: dl_dashboard_component
        DashboardPage {
            user_type: root.user_type
            can_quick_join: root.last_room.length === 11

            StackView.onActivated: {
                if (!root.connected || user_type !== "Director")
                    return;
                root.current_operation = "getSessions";
                SessionClient.getMySessions(root.user_id);
            }
        }
    }

    Component {
        id: dl_session_component
        SessionPage {}
    }
}
