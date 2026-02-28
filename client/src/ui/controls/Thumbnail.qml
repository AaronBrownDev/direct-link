import QtQuick
import ui.theme

Rectangle {
    id: dl_bg_active_camera
    state: "inactive"

    property real aspect_ratio: 16 / 9

    color: "black"

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
