/*
 * File: main.qml
 * Author: Justin Williams
 * Date: 2/10/26
 * File Description: The qml file that the application loads on startup.
   Currently, it contains a nonfunctional window for the Director session page.
   The Leave button closes the application.
 */

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
        id: root

        property string user_type: "Director"
        property real main_aspect_ratio: 16 / 9
        property real max_camera_count: 4

        visible: true
        minimumWidth: dl_root_layout.implicitWidth
        minimumHeight: dl_root_layout.implicitHeight
        width: 2400
        height: 1250
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

                Item { Layout.fillWidth: true }

                Camera {
                    id: dl_main_camera

                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: true
                    Layout.preferredHeight: width / main_aspect_ratio
                    Layout.maximumHeight: dl_layout_cameras.height
                    Layout.maximumWidth: Layout.maximumHeight * main_aspect_ratio
                    Layout.preferredWidth: Layout.maximumWidth

                    Text {
                        id: dl_label_main_camera
                        text: "Main Camera (16:9)"
                        anchors.centerIn: parent
                        color: "white"
                        font.pointSize: 12
                    }
                }

                Item { Layout.fillWidth: true }

                ThumbnailList { id: dl_camera_list }
            }

            Footer { id: dl_session_footer }
        }
}
