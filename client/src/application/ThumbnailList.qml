import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: dl_bg_camera_list
    color: "#1E293B"
    Layout.preferredHeight: dl_layout_camera_list.implicitHeight + radius
    Layout.preferredWidth: 300
    radius: 15


    ColumnLayout {
        id: dl_layout_camera_list
        spacing: 0
        anchors.fill: parent

        property int index: 0

        Item { Layout.fillHeight: true }

        Repeater {
            id: dl_repeater_thumbnail
            model: max_camera_count
            Camera {
                id: dl_thumbnail
                Layout.margins: 15
                Layout.fillWidth: true
                Layout.preferredHeight: width / main_aspect_ratio

                Text {
                    anchors.centerIn: parent
                    color: "white"
                    text: "Camera " + (index + 1)
                }
            }
        }

        Item { Layout.fillHeight: true }

    }
}
