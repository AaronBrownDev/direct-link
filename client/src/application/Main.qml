/*
 * File: main.qml
 * Author: Justin Williams
 * Date: 2/10/26
 * File Description: The qml file that the application loads on startup.
   Currently, it contains a nonfunctional window for the Director session page.
   The Leave button closes the application.
 */

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

Window {
        id: root

        property string user_type: "Director"
        property real max_camera_count: 4

        visible: true
        minimumWidth: dl_root_layout.implicitWidth
        minimumHeight: dl_root_layout.implicitHeight
        width: minimumWidth
        height: minimumHeight
        color: "#0F172A"
        title: "Direct Link Session"

        ColumnLayout {
            id: dl_root_layout

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

                    Text {
                        id: dl_label_main_camera
                        text: "Main Camera (16:9)"
                        anchors.centerIn: parent
                        color: "white"
                        font.pointSize: 12
                    }
                }

                ThumbnailList {
                    id: dl_camera_list
                    Layout.fillHeight: true

                }
            }

            Footer { id: dl_session_footer }
        }
}
