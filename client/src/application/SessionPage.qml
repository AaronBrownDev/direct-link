import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import ui
import ui.controls

ColumnLayout {
    id: dl_root_layout

    property string user_type: "Director"
    property real max_camera_count: 4

    spacing: 15
    anchors.fill: parent

    Header { id: dl_session_header }

    SessionInfo { id: dl_session_details }

    RowLayout {
        id: dl_layout_cameras
        spacing: 15
        Layout.margins: 15

        SessionLog { id: dl_session_log }

        CameraFeed {
            id: dl_active_camera

            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.preferredHeight: width / aspect_ratio
            Layout.minimumHeight: implicitWidth / aspect_ratio
            implicitWidth: 500

            Component.onCompleted: {
                FrameReader.videoSink = dl_active_camera.videoSink
            }
        }

        ThumbnailList {
            id: dl_camera_list
            Layout.fillHeight: true

        }
    }

    Footer { id: dl_session_footer }
}
