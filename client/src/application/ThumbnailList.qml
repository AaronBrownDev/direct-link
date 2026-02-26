import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: dl_bg_camera_list

    property real default_aspect_ratio: 16 / 9
    property int minHeight: dl_repeater_thumbnail.count * (Layout.preferredWidth / default_aspect_ratio)

    color: "#1E293B"
    radius: 15

    Layout.preferredWidth: 300
    Layout.minimumHeight: minHeight

    ColumnLayout {
        id: dl_layout_camera_list

        property int index: 0

        spacing: 15
        anchors.fill: parent
        anchors.margins: 15

        // Item { Layout.fillHeight: true }

        Repeater {
            id: dl_repeater_thumbnail
            model: max_camera_count
            Thumbnail {
                id: dl_thumbnail

                aspect_ratio: default_aspect_ratio

                Layout.fillWidth: true
                Layout.preferredHeight: width / aspect_ratio


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
