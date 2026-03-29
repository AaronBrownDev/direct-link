/*
 * File: SessionOperatorView.qml
 * Author: Justin Williams
 * Date: 3/29/26
 * File Description: The qml file that contains a camera feed for the oeprator. The camera feed
 * displays the operator's camera output.
 */

import QtQuick
import QtQuick.Layouts
import QtMultimedia
import ui.controls

RowLayout {
        id: dl_operator_view

        spacing: 15

        SessionLog {
            id: dl_session_log
        }

        CameraFeed {
            id: dl_camera_preview

            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.preferredHeight: width / aspect_ratio
            Layout.minimumHeight: implicitWidth / aspect_ratio
            Layout.maximumWidth: dl_operator_view.height * aspect_ratio
            implicitWidth: 500
        }

        CaptureSession {
            id: dl_capture_session
            camera: Camera {
                id: dl_camera
                active: true
            }
            videoOutput: dl_camera_preview.video_sink
        }
    }