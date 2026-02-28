import QtQuick
import QtQuick.Layouts
import ui.theme

Rectangle {
    id: dl_bg_log
    color: Theme.surface
    Layout.fillHeight: true
    Layout.preferredWidth: 500
    radius: 15

    ColumnLayout {
        id: dl_layout_log
        spacing: 25
        anchors.fill: parent
        anchors.margins: parent.radius + 5

        Text {
            id: dl_label_log
            text: "Session Log"
            color: Theme.textWhite
            font.pointSize: 16
            font.bold: true
        }

        Rectangle {
            id: dl_bg_log_contents
            Layout.fillHeight: true
            Layout.fillWidth: true
            radius: 15
            color: Theme.background

            Text {
                id: dl_log_contents
                text: ""
                anchors.fill: parent
                anchors.margins: 15
                font.pointSize: 12
                color: Theme.textWhite
                wrapMode: Text.Wrap
            }
        }
    }
}
