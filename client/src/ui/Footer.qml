import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ui.controls
import ui.theme

Rectangle {
    id: dl_bg_footer

    property bool showCloseButton: false

    signal leavePage()
    signal closeClicked()

    color: Theme.surface

    RowLayout {
        id: dl_layout_footer

        anchors {
            fill: parent
            leftMargin: 25
            rightMargin: 25
        }

        Item { Layout.fillWidth: true }

        DLButton {
            id: dl_control_app_exit
            Layout.preferredWidth: 120
            Layout.preferredHeight: 50
            button_type: DLButton.ButtonType.Secondary
            button_text: "Leave"
            onClicked: dl_bg_footer.leavePage()
        }

        DLButton {
            id: dl_control_session_close

            Layout.preferredWidth: 200
            Layout.preferredHeight: 50

            visible: dl_bg_footer.showCloseButton
            button_type: DLButton.ButtonType.Danger
            button_text: "Close Session"
            onClicked: dl_bg_footer.closeClicked()
        }

    }


}
