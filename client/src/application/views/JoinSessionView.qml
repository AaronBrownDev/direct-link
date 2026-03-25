/*
 * File: JoinSessionView.qml
 * Author: Justin Williams
 * Date: 3/12/26
 * File Description: A view that holds the surface for session join controls.
 */

import QtQuick
import QtQuick.Layouts
import network
import ui.controls
import ui.theme

/*
    PROPERTIES

        show_camera_field:bool - Determines if the 'Camera Name' text field is visible
        can_quick_join:bool - Determines if the 'Quick Join Last Session' button is enabled

    SIGNALS

        joinClicked(roomCode:string, cameraName:string) - Fires when a room code has been
            entered and the 'Join Session' button has been clicked. Passes the entered room
            code and entered camera name
        quickJoinClicked() - Fires when the 'Quick Join Last Session' button has been pressed
 */
Rectangle {
    id: dl_join_session_bg

    property bool show_camera_field: true
    property bool can_quick_join: false

    Layout.fillWidth: true
    Layout.preferredHeight: 550

    color: Theme.surface
    radius: 15

    signal joinClicked(string roomCode, string cameraName)
    signal quickJoinClicked

    ColumnLayout {
        id: dl_join_session_layout

        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        Text {
            id: dl_join_session_title

            text: "Join Session"
            color: Theme.textWhite
            font.pointSize: 25
            font.bold: true
        }

        DLTextField {
            id: dl_session_code_field

            Layout.fillWidth: true

            label: "Session Code"
            emptyText: "Enter code (XXXX-XXXXXX)"
            maxLength: 11
            isCode: true

            onInputChanged: {
                dl_session_join_status.visible = false;
            }
        }

        Text {
            id: dl_session_join_status

            visible: false
            text: "Please enter a valid code"
            color: Theme.danger
            font.pointSize: 14
        }

        DLTextField {
            id: dl_camera_name_field

            Layout.fillWidth: true

            visible: dl_join_session_bg.show_camera_field
            label: "Camera Name"
            emptyText: "e.g. Camera A - Wide Angle"
            maxLength: 32
        }

        Item {
            Layout.fillHeight: true
        }

        DLButton {
            id: dl_join_button

            Layout.fillWidth: true

            button_type: DLButton.ButtonType.Primary
            button_text: "Join Session"

            onClicked: {
                if (dl_session_code_field.input.length === 11)
                    dl_join_session_bg.joinClicked(dl_session_code_field.input, dl_camera_name_field.input);
                else {
                    dl_session_code_field.state = "invalid";
                    dl_session_join_status.visible = true;
                }
            }
        }

        DLButton {
            id: dl_quick_join_button

            Layout.fillWidth: true
            Layout.preferredHeight: 55
            Layout.topMargin: 10

            button_type: DLButton.ButtonType.Secondary
            button_text: "Quick Join Last Session"
            active: dl_join_session_bg.can_quick_join

            onClicked: dl_join_session_bg.quickJoinClicked()
        }
    }

    Connections {
        target: SessionClient

        function onError(msg) {
            switch (msg) {
            case "session not found":
                dl_session_code_field.state = "invalid";
                dl_session_join_status.color = Theme.danger;
                dl_session_join_status.visible = true;
                return;
            default:
                return;
            }
        }
    }
}
