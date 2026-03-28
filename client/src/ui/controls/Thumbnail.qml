/*
 * File: Thumbnail.qml
 * Author: Justin Williams
 * Date: 2/25/26
 * File Description: A component representing a thumbnail. The thumbnail displays behind a camera feed and 
 * can its border and display an icon to reflect its state.
 */

import QtQuick
import ui.theme

/*
    PROPERTIES

        states - Control the appearance of the thumbnail
            "inactive" - The thumbnail is blank and borderless
            "active" - The thumbnail is blank with a green border
            "casting" - The thumbnail shows a cast icon and has a blue border
 */
Rectangle {
    id: dl_thumbnail

    color: Theme.innerSurface
    border.width: 2
    border.color: Theme.primary
    state: "inactive"

    states: [
        State {
            name: "inactive"
            PropertyChanges {
                dl_thumbnail.border.width: 0
                dl_cast_icon.visible: false
            }
        },
        State {
            name: "active"
            PropertyChanges {
                dl_thumbnail.border.width: 2
                dl_thumbnail.border.color: Theme.primary
                dl_cast_icon.visible: false
            }
        },
        State {
            name: "casting"
            PropertyChanges {
                dl_thumbnail.border.width: 2
                dl_thumbnail.border.color: Theme.cameraCasting
                dl_cast_icon.visible: true
            }
        }
    ]

    Image {
        id: dl_cast_icon

        anchors.centerIn: parent
        height: parent.height / 3
        width: height
        source: "qrc:/resources/icons/cast.png"
    }
}
