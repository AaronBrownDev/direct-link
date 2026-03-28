/*
 * File: CameraFeed.qml
 * Author: Justin Williams
 * Date: 2/25/26
 * File Description: A component representing a video output displayed over a thumbnail. The camera feed can
 * accept a track, which will have its video sink set to the video output's video sink so that track frames
 * can display in the application. The casting state and assigned_track value determine the state of the Thumbnail
 */

import QtQuick
import QtMultimedia

/*
    PROPERTIES

        assigned_track:var - The track that is currently assigned to the camera feed. When set,
            the track receives the video output's video sink
        aspect_ratio:real - The aspect ratio of the camera feed
        casting:bool - Sets the state of the thumbnail to "casting" if true
 */
Thumbnail {
    id: dl_camera
    property var assigned_track: null
    property real aspect_ratio: assigned_track ? assigned_track.aspectRatio : (16 / 9)
    property bool casting: false

    state: {
        if (casting) {
            return "casting";
        }
        if (assigned_track !== null) {
            return "active";
        }
        return "inactive";
    }

    onAssigned_trackChanged: {
        if (assigned_track !== null) {
            assigned_track.setVideoSink(dl_video_output.videoSink);
        }
    }

    VideoOutput {
        id: dl_video_output
        visible: true
        anchors.fill: parent
    }

    Connections {
        target: dl_camera.assigned_track

        function onAspectRatioChanged() {
            dl_camera.aspect_ratio = dl_camera.assigned_track.aspectRatio;
        }
    }
}
