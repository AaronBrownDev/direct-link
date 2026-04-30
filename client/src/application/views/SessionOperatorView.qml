/*
 * File: SessionOperatorView.qml
 * Author: Justin Williams
 * Date: 3/29/26
 * File Description: The qml file that contains a camera feed for the oeprator. The camera feed
 * displays the operator's camera output.
 */

import QtQuick
import QtQuick.Layouts
import application
import session
import ui.controls

RowLayout {
        id: dl_operator_view

        spacing: 15

        Component.onCompleted: {
            CameraSessionController.previewSink = dl_camera_preview.video_sink;
        }

        Component.onDestruction: {
            CameraSessionController.previewSink = null;
        }

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

        Connections {
            target: AppLogger.logger

            function onMessageReceived(type, message) {
                dl_session_log.logMessage(type, message);
            }
        }
    }