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
import ui.theme

Window {
        id: root

        property string user_type: "Director"
        property real max_camera_count: 4

        visible: true
        minimumWidth: dl_session_page.implicitWidth
        minimumHeight: dl_session_page.implicitHeight
        width: minimumWidth
        height: minimumHeight
        color: Theme.background
        title: "DirectLink"

        SessionPage {
            id: dl_session_page
            user_type: user_type
            max_camera_count: max_camera_count
        }
}
