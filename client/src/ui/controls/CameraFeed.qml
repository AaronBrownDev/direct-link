import QtQuick
import QtMultimedia

Thumbnail {
    id: dl_active_camera
    readonly property alias videoSink: dl_video_output.videoSink

    VideoOutput {
        id: dl_video_output
        visible: true
        anchors.fill: parent
    }
}
