/*
 * File: CameraList.qml
 * Author: Justin Williams
 * Date: 3/27/26
 * File Description: A component that manages a list of camera feeds. Each camera feed will
 * be assigned a track from a list of tracks if it has one. Clicking a camera feed in the
 * list will emit a signal with the index selected.
 */

import QtQuick
import QtQuick.Layouts
import application
import ui.controls
import ui.theme

/*
    PROPERTIES

        max_camera_count:int - Determines the maximum number of cameras in the list
        activeCamera:int - Determines which camera is selected as the active camera. That
            camera will not be assigned a track, and it will be considered to be casting.
            A value of -1 means that no camera is selected
        all_tracks:var - A list of tracks. Each track will be attached to a camera unless
            the max_camera_count value is exceeded or the current index matches the 
            activeCamera value

    SIGNALS

        cameraSelected() - Fires when a camera from the list that has a track has been selected
 */
Rectangle {
    id: dl_camera_list_bg

    property int max_camera_count: 4
    property int activeCamera: -1

    property var all_tracks: []

    color: Theme.surface
    radius: 15

    signal cameraSelected(int index)

    ListView {
        id: dl_camera_list

        anchors.fill: parent
        anchors.margins: 15
        clip: true
        spacing: 15

        model: {
            let slots = [];
            for (let i = 0; i < max_camera_count; i++) {
                slots.push(i < all_tracks.length ? all_tracks[i] : null)
            }
            return slots;
        }

        delegate: CameraFeed {
            required property var modelData
            required property int index
            width: ListView.view.width
            height: width / aspect_ratio
            assigned_track: (index === dl_camera_list_bg.activeCamera) ? null : modelData
            casting: (index === dl_camera_list_bg.activeCamera) && (modelData !== null)

            MouseArea {
                anchors.fill: parent
                enabled: modelData !== null
                onClicked: cameraSelected(index)
            }
        }
    }
}
