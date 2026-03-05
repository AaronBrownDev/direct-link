import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ui.controls
import ui.theme

Rectangle {
    id: dl_bg_footer

    signal leavePage()

    Layout.fillWidth: true
    Layout.preferredHeight: 100

    color: Theme.surface

    RowLayout {
        id: dl_layout_footer

        anchors {
            fill: parent
            leftMargin: 25
            rightMargin: 25
        }

        spacing: 10

        // Text {
        //     id: dl_label_field_room_id
        //     text: "Room ID"
        //     color: Theme.textWhite
        //     font.pointSize: 18
        // }

        // Rectangle {
        //     id: dl_bg_field_room_id
        //     width: 400
        //     height: 50
        //     color: Theme.background
        //     radius: 5

        //     Layout.rightMargin: 20

        //     TextInput {
        //         id: dl_field_room_id
        //         anchors.left: parent.left
        //         anchors.verticalCenter: parent.verticalCenter
        //         anchors.leftMargin: 15
        //         width: parent.width
        //         color: Theme.textWhite
        //         maximumLength: 12
        //         font.pointSize: 18
        //     }
        // }

        // Button {
        //     id: dl_control_connect
        //     Layout.preferredWidth: 120
        //     Layout.preferredHeight: 50
        //     background: Rectangle {
        //         radius: 25
        //         color: dl_control_connect.down ? Theme.primaryPressed : Theme.primary
        //         Text {
        //             text: "Connect"
        //             font.pointSize: 15
        //             color: Theme.textBlack
        //             anchors.centerIn: parent
        //         }
        //     }
        // }

        Item { Layout.fillWidth: true }

        DLButton {
            id: dl_control_app_exit
            Layout.preferredWidth: 120
            Layout.preferredHeight: 50
            buttonType: DLButton.ButtonType.Danger
            buttonText: "Leave"
            onClicked: dl_bg_footer.leavePage()
        }

    }


}
