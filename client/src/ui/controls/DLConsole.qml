/*
 * File: DLConsole.qml
 * Author: Justin Williams
 * Date: 3/5/26
 * File Description: A component that provides a text box for printing messages
 */

import QtQuick
import ui.theme

/*
    PROPERTIES

    contents (string) - The text that will display inside of the console
    label (string) - The text that will display above the console
 */
Rectangle {
    id: dl_console

    property string contents: ""
    property string label: ""

    color: Theme.background
    radius: 15

    Text {
        id: dl_console_label

        text: dl_console.label
        anchors.bottom: parent.top
        anchors.left: parent.left
        anchors.bottomMargin: 10
        font.pointSize: 14
        font.bold: true
        color: Theme.textMuted
    }

    Item {
        id: dl_console_content_area

        anchors.fill: parent
        anchors.margins: 25
        clip: true

        Text {
            id: dl_console_contents

            anchors.fill: parent
            font.pointSize: 16
            color: Theme.textWhite
            text: dl_console.contents
        }
    }


}
