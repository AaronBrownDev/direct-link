import QtQuick
import QtQuick.Layouts
import application
import ui.controls
import ui.theme

Rectangle {
    id: dl_camera_list_bg

    property int max_camera_count: 4
    property real default_aspect_ratio: 16 / 9
    property int minHeight: dl_repeater_thumbnail.count * (Layout.preferredWidth / default_aspect_ratio)

    Layout.preferredWidth: 300
    Layout.minimumHeight: minHeight

    color: Theme.surface
    radius: 15

    ListView {
        id: dl_camera_list
    }
}
