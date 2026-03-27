import QtQuick
import QtQuick.Layouts
import ui.controls

RowLayout {
        id: dl_director_view

        property int max_camera_count: 4
        property int active_camera: -1
        property DirectorSession session

        Layout.margins: 15

        spacing: 15

        SessionLog {
            id: dl_session_log
        }

        CameraFeed {
            id: dl_active_camera

            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.preferredHeight: width / aspect_ratio
            Layout.minimumHeight: implicitWidth / aspect_ratio
            implicitWidth: 500
        }

        CameraList {
            id: dl_camera_list
            Layout.fillHeight: true
            max_camera_count: dl_director_view.max_camera_count
        }
    }