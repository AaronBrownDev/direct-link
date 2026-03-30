import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import types
import ui.theme
import ui.controls

Rectangle {
    id: dl_header

    property int user_type: UserRole.director
    property string connection_status: "disconnected"

    Layout.fillWidth: true
    Layout.preferredHeight: 75

    color: Theme.surface

    signal homeClicked()
    signal settingsClicked()
    signal profileClicked()

    states: [
        State {
            name: "login"
            PropertyChanges {
                dl_header_type_bg.opacity: 0
                dl_connection_status_chip.opacity: 0
                dl_settings_button.opacity: 0
                dl_settings_button.enabled: false
                dl_profile_button.opacity: 0
                dl_profile_button.enabled: false
                dl_logo_area.enabled: false
            }
        },
        State {
            name: "default"
            PropertyChanges {
                dl_header_type_bg.opacity: 1
                dl_connection_status_chip.opacity: 1
                dl_settings_button.opacity: 1
                dl_settings_button.enabled: true
                dl_profile_button.opacity: 1
                dl_profile_button.enabled: true
                dl_logo_area.enabled: true
            }
        }
    ]

    RowLayout {
        id: dl_header_layout

        anchors.fill: parent
        anchors.rightMargin: 20
        anchors.leftMargin: 20
        spacing: 20

        Text {
            id: dl_header_logo

            text: "DirectLink"
            Layout.leftMargin: 20
            color: Theme.textWhite
            font.bold: true
            font.pointSize: 24

            MouseArea {
                id: dl_logo_area

                anchors.fill: parent
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor

                onClicked: dl_header.homeClicked()
            }
        }

        Rectangle {
            id: dl_header_type_bg

            Layout.preferredWidth: dl_header_type.implicitWidth + radius * 2
            Layout.preferredHeight: 30

            color: Theme.fieldBackground
            radius: height / 2

            Text {
                id: dl_header_type

                anchors.centerIn: parent
                text: UserRole.toString(dl_header.user_type, true)
                color: Theme.textMuted
                font.bold: true
                font.pointSize: 14
            }

            Behavior on opacity { PropertyAnimation {
                    duration: 250
                    easing.type: Easing.InOutQuad
                } }
        }

        

        Item {Layout.fillWidth: true}

        ConnectionStatusChip {
            id: dl_connection_status_chip

            Layout.preferredWidth: 200
            Layout.preferredHeight: 50

            state: dl_header.connection_status

            Behavior on opacity { PropertyAnimation {
                duration: 250
                easing.type: Easing.InOutQuad
            } }
        }

        DLButton {
            id: dl_settings_button

            Layout.preferredWidth: 50
            Layout.preferredHeight: 50

            button_type: DLButton.ButtonType.Default
            radius: 25

            onClicked: dl_header.settingsClicked()

            Image {
                anchors.fill: parent
                anchors.margins: 10
                source: "qrc:/resources/icons/settings.png"
            }

            Behavior on opacity { PropertyAnimation {
                duration: 250
                easing.type: Easing.InOutQuad
            } }
        }

        DLButton {
            id: dl_profile_button

            Layout.preferredWidth: 50
            Layout.preferredHeight: 50

            button_type: DLButton.ButtonType.Secondary
            radius: 25

            onClicked: dl_header.profileClicked()

            Image {
                anchors.fill: parent
                anchors.margins: 10
                source: "qrc:/resources/icons/profile.png"
            }

            Behavior on opacity { PropertyAnimation {
                duration: 250
                easing.type: Easing.InOutQuad
            } }
        }
    }
}
