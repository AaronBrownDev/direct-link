import QtQuick
import ui.theme

Rectangle {
    id: dl_bg_active_camera

    property real aspect_ratio: 16 / 9

    color: Theme.background
    state: "inactive"

    states: [
        State {
            name: "inactive"
            PropertyChanges {
                target: dl_bg_active_camera
                border.color: Theme.inactive
            }
        }
    ]
}
