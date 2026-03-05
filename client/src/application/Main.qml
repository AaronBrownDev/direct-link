/*
 * File: Main.qml
 * Author: Justin Williams
 * Date: 2/10/26
 * File Description: The qml file that the application loads on startup. It
 * contains a StackView that manages application pages and specifies properties
 * for the Window.
 */

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import ui
import ui.theme

Window {
        id: root

        property string user_type: "Director"
        property real max_camera_count: 4

        visible: true
        minimumWidth: 1400
        minimumHeight: 1100
        width: minimumWidth
        height: minimumHeight
        color: Theme.background
        title: "DirectLink"

        ColumnLayout {
            id: dl_main_layout

            anchors.fill: parent

            Header {
                id: dl_dash_header
                user_type: root.user_type
            }

            StackView {
                id: dl_page_stack

                Layout.fillWidth: true
                Layout.fillHeight: true

                initialItem: dl_dashboard_component
            }
        }

        Component {
            id: dl_dashboard_component
            DashboardPage {
                user_type: root.user_type
            }
        }
}
