import QtQuick
import QtQuick.Layouts
import network
import ui.controls
import ui.theme

Rectangle {
        id: dl_join_session_bg

        property bool showCameraField: true
        property bool canQuickJoin: false

        Layout.fillWidth: true
        Layout.preferredHeight: 550

        color: Theme.surface
        radius: 15

        signal joinClicked(string roomCode, string cameraName)
        signal quickJoinClicked()

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

                Layout.topMargin: 40
                Layout.preferredHeight: 65
                Layout.fillWidth: true

                label: "Session Code"
                emptyText: "Enter code (XXXX-XXXXXX)"
                maxLength: 11
                isCode: true

                onFieldChanged: {
                    dl_session_join_status.visible = false
                }
            }
            
            Text {
                id: dl_session_join_status

                visible: false
                text: "Please enter a valid code"
                font.pointSize: 14
            }

            DLTextField {
                id: dl_camera_name_field

                Layout.topMargin: 30
                Layout.preferredHeight: 65
                Layout.fillWidth: true

                visible: showCameraField
                label: "Camera Name"
                emptyText: "e.g. Camera A - Wide Angle"
                maxLength: 32
            }

            Item { Layout.fillHeight: true }

            DLButton {
                id: dl_join_button

                Layout.fillWidth: true
                Layout.preferredHeight: 65

                buttonType: DLButton.ButtonType.Primary
                buttonText: "Join Session"

                onClicked: {
                    if (dl_session_code_field.input.length === 11)
                        joinClicked(dl_session_code_field.input, dl_camera_name_field.input)
                    else {
                        dl_session_code_field.state = "invalid"
                        dl_session_join_status.color = Theme.danger
                        dl_session_join_status.visible = true
                    }
                }
            }

            DLButton {
                id: dl_quick_join_button

                Layout.fillWidth: true
                Layout.preferredHeight: 55
                Layout.topMargin: 10

                buttonType: DLButton.ButtonType.Neutral
                buttonText: "Quick Join Last Session"
                active: dl_join_session_bg.canQuickJoin

                onClicked: quickJoinClicked()
            }
        }

        Connections {
            target: SessionClient

            function onError(msg) {
                switch (msg) {
                    case "session not found":
                        dl_session_code_field.state = "invalid"
                        dl_session_join_status.color = Theme.danger
                        dl_session_join_status.visible = true
                        return
                    default:
                        return
                }
            }
        }
    }