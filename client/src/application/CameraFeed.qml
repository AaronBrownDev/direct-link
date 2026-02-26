import QtQuick
import QtQuick.Window
import QtMultimedia

Thumbnail {
    id: dl_active_camera
    property alias videoSink: dl_video_output.videoSink

    VideoOutput {
        id: dl_video_output
        visible: true
        anchors.fill: parent
    }
}
