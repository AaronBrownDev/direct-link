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
import types
import network
import session
import ui
import ui.theme
import ui.controls

Window {
    id: root

    property string user_id: ""
    property int user_type: UserRole.director

    property url channel: "http://localhost:50051"
    property bool connected: false

    property string last_room: ""
    property string pending_close_room: ""
    property string current_operation: ""

    visible: true
    minimumWidth: 1400
    minimumHeight: 1100
    width: minimumWidth
    height: minimumHeight
    color: Theme.background
    title: "DirectLink"

    Component.onCompleted: {
        SessionClient.connectToServer(channel);
        root.connected = true;
    }

    // Header + Page Stack

    ColumnLayout {
        id: dl_main_layout

        anchors.fill: parent

        Header {
            id: dl_header
            state: "login"

            onProfileClicked: dl_profile.state = "visible"
            onHomeClicked: {
                if (dl_page_stack.depth > 2) {
                    dl_page_stack.popToIndex(1, StackView.Immediate);
                }
            }
        }

        StackView {
            id: dl_page_stack

            Layout.fillWidth: true
            Layout.fillHeight: true

            initialItem: dl_login_component
        }
    }

    MouseArea {
        id: dl_profile_dismiss_area
        anchors.fill: parent
        visible: dl_profile.state === "visible"
        onClicked: dl_profile.state = "hidden"
    }

    MiniProfile {
        id: dl_profile

        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: dl_header.height + 10
        anchors.rightMargin: 10

        user_id: root.user_id
        user_type: root.user_type

        state: "hidden"

        onLogoutClicked: {
            dl_logout_popup.open();
            dl_profile.state = "hidden";
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

    DLPopup {
        id: dl_logout_popup

        inputType: DLPopup.InputType.ConfirmCancel
        displayText: "Are you sure you want to log out?"
        onConfirmed: {
            dl_page_stack.popToIndex(0, StackView.Immediate);
            root.user_id = "";
            root.user_type = UserRole.director;
            root.last_room = "";
            root.pending_close_room = "";
            root.current_operation = "";
            dl_header.connection_status = "disconnected"
            dl_header.state = "login";
            dl_profile.state = "hidden";
        }
    }

    // Page Connections

    Connections {
        target: dl_page_stack.currentItem
        ignoreUnknownSignals: true

        function onLogin(userName, userRole) {
            root.user_id = userName;
            root.user_type = userRole;
            dl_header.user_type = userRole;
            dl_header.state = "default";
            dl_page_stack.pushItem(dl_dashboard_component, {
                user_type: userRole
            }, StackView.Immediate);

            if (!root.connected || userRole !== UserRole.director)
                    return;
            
            root.current_operation = "getSessions";
            SessionClient.getMySessions(root.user_id);
        }

        function onClosePage() {
            dl_page_stack.popCurrentItem(StackView.Immediate);
        }

        function onJoinRequested(roomCode) {
            root.current_operation = "joinSession";
            root.last_room = roomCode;
            SessionClient.joinSession(roomCode, root.user_id, UserRole.toString(root.user_type));
        }

        function onQuickJoinRequested() {
            if (root.last_room === "") {
                dl_error_popup.displayText = "Failed to join session. Please try again.";
                dl_error_popup.open();
                return;
            }

            root.current_operation = "joinSession";
            SessionClient.joinSession(root.last_room, root.user_id, UserRole.toString(root.user_type));
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
            dl_page_stack.pushItem(dl_session_component, {
                user_type: UserRole.director,
                room_code: root.last_room,
                livekit_token: token,
                livekit_url: livekitUrl
            }, StackView.Immediate);
        }

        function onCameraJoined(whipUrl, streamKey) {
            dl_page_stack.pushItem(dl_session_component, {
                user_type: UserRole.camera,
                room_code: root.last_room,
                whip_url: whipUrl,
                stream_key: streamKey
            }, StackView.Immediate);
        }

        function onSessionCreated(roomCode) {
            dl_info_popup.displayText = "Room Successfully Created: " + roomCode;
            dl_info_popup.open();
            root.current_operation = "joinSession";
            root.last_room = roomCode;
            SessionClient.joinSession(roomCode, root.user_id, UserRole.toString(root.user_type));
        }

        function onSessionClosed(success) {
            if (success) {
                if (root.last_room === root.pending_close_room)
                    root.last_room = "";

                root.pending_close_room = "";
                dl_page_stack.popCurrentItem(StackView.Immediate);
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
            dl_header.connection_status = "connected"
        }

        function onDisconnected() {
            dl_header.connection_status = "disconnected"
        }

        function onConnectionStateChanged(newState) {
            dl_header.connection_status = newState
        }
    }

    // Camera Session Connections

    Connections {
        target: CameraSessionController

        function onSessionStarted() {
            dl_info_popup.displayText = "Camera Session Started.";
            dl_info_popup.open();
            dl_header.connection_status = "connected"
        }

        function onSessionStopped() {
            dl_header.connection_status = "disconnected"
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
            can_quick_join: root.last_room.length === 11

            StackView.onStatusChanged: {
                if (StackView.status === StackView.Inactive)
                    clearFields();
            }
        }
    }

    Component {
        id: dl_session_component
        SessionPage {}
    }
}
