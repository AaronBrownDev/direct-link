import QtQuick
import QtQuick.Layouts
import ui.controls
import ui.theme

RowLayout {
    id: dl_dash_view_layout

    signal joinClicked(string roomCode, string cameraName)
    signal quickJoinClicked()

    spacing: 20

    Rectangle {
        id: dl_join_session_bg

        Layout.fillWidth: true
        Layout.preferredHeight: 550

        color: Theme.surface
        radius: 15

        ColumnLayout {
            id: dl_join_session_layout

            anchors.fill: parent
            anchors.margins: 20

            Text {
                id: dl_join_session_title

                text: "Join Session"
                color: Theme.textWhite
                font.pointSize: 25
                font.bold: true
            }

            DLTextField {
                id: dl_session_code_field

                Layout.topMargin: 50
                Layout.preferredHeight: 65
                Layout.fillWidth: true

                label: "Session Code"
                emptyText: "Enter 8-digit code"
                maxLength: 9
                isCode: true
            }

            DLTextField {
                id: dl_camera_name_field

                Layout.topMargin: 40
                Layout.preferredHeight: 65
                Layout.fillWidth: true

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
                    if (dl_session_code_field.input.length === 9)
                        joinClicked(dl_session_code_field.input, dl_camera_name_field.input)
                }
            }

            DLButton {
                id: dl_quick_join_button

                Layout.fillWidth: true
                Layout.preferredHeight: 55
                Layout.topMargin: 10

                buttonType: DLButton.ButtonType.Neutral
                buttonText: "Quick Join Last Session"

                onClicked: quickJoinClicked()
            }
        }
    }

    Rectangle {
        id: dl_equipment_setup_bg

        Layout.fillWidth: true
        Layout.preferredHeight: 550

        color: Theme.surface
        radius: 15

        ColumnLayout {
            id: dl_equipment_setup_layout

            anchors.fill: parent
            anchors.margins: 20

            Text {
                id: dl_equipment_setup_title

                text: "Equipment Setup"
                color: Theme.textWhite
                font.pointSize: 25
                font.bold: true
            }

            DLButton {
                id: dl_camera_config_button

                Layout.topMargin: 50
                Layout.fillWidth: true
                Layout.preferredHeight: 65

                buttonType: DLButton.ButtonType.Neutral
                buttonText: "Configure Cameras & Hardware"
            }

            DLConsole {
                id: dl_last_config_console

                Layout.topMargin: 40
                Layout.fillWidth: true
                Layout.preferredHeight: 130

                label: "Last Used Configuration"
                contents: "Sony A7S III (HDMI)\n4K @ 60fps - H.625"
            }

            Text {
                id: dl_status_label

                Layout.topMargin: 10

                font.pointSize: 14
                font.bold: true
                color: Theme.textMuted
                text: "Status Indicators"
            }

            Text {
                id: dl_camera_status

                font.pointSize: 14
                color: Theme.textMuted
                text: "- Camera detected"
            }

            Text {
                id: dl_gpu_status

                font.pointSize: 14
                color: Theme.textMuted
                text: "- GPU Available"
            }

            Item { Layout.fillHeight: true }
        }
    }
}
