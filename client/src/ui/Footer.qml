import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ui.controls
import ui.theme

Rectangle {
    id: dl_bg_footer

    signal leavePage()

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
            buttonType: DLButton.ButtonType.Danger
            buttonText: "Leave"
            onClicked: dl_bg_footer.leavePage()
        }

    }


}
