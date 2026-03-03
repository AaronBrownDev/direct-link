import QtQuick
import QtQuick.Layouts
import ui.theme

Rectangle {
    id: dl_bg_camera_list

    property int max_camera_count: 4
    property real default_aspect_ratio: 16 / 9
    property int minHeight: dl_repeater_thumbnail.count * (Layout.preferredWidth / default_aspect_ratio)

    Layout.preferredWidth: 300
    Layout.minimumHeight: minHeight

    color: Theme.surface
    radius: 15

    ColumnLayout {
        id: dl_layout_camera_list

        property int index: 0

        spacing: 15
        anchors.fill: parent
        anchors.margins: 15

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
                    color: Theme.textWhite
                    text: "Camera " + (index + 1)
                }
            }
        }

        Item { Layout.fillHeight: true }

    }
}
