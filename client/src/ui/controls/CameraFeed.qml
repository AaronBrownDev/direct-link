import QtQuick
import QtMultimedia

Thumbnail {
    id: dl_active_camera
    readonly property alias videoSink: dl_video_output.videoSink

    CaptureSession {
        id: dl_capture_session
        camera: Camera {
            id: dl_camera
            cameraDevice: MediaDevices.defaultVideoInput
            active: true
        }
        videoOutput: dl_video_output
    }

    VideoOutput {
        id: dl_video_output
        visible: true
        anchors.fill: parent
    }
}
