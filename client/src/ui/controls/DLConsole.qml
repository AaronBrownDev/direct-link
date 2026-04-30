/*
 * File: DLConsole.qml
 * Author: Justin Williams
 * Date: 3/5/26
 * File Description: A component that provides a text box for printing messages.
 */
//ListV
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ui.theme

/*
    PROPERTIES

        contents:string - The text that will display inside of the console
        label:string - The text that will display above the console
 */
Rectangle {
    id: dl_console

    property string contents: ""
    property string label: ""
    property real maxLineWidth: 0

    // Append a line and scroll to the bottom
    function appendLine(text, textColor = Theme.textWhite) {
        dl_line_metrics.text = text;
        dl_console.maxLineWidth = Math.max(dl_console.maxLineWidth, dl_line_metrics.width);
        
        dl_console_model.append({ "line": text, "lineColor": textColor });
        Qt.callLater(() => dl_console_list.contentY = Math.max(0, dl_console_list.contentHeight - dl_console_list.height));
    }

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

    TextMetrics {
        id: dl_line_metrics
        font.pointSize: 10
        font.family: "Monospace"
    }

    ScrollView {
        id: dl_console_scroll

        anchors.fill: parent
        anchors.margins: 25
        clip: true

        ListView {
            id: dl_console_list

            contentWidth: dl_console.maxLineWidth
            spacing: 2

            model: ListModel { id: dl_console_model }

            delegate: Text {
                required property string line
                required property color lineColor
                width: implicitWidth
                text: line
                font.pointSize: 10
                font.family: "Monospace"
                color: lineColor
                wrapMode: Text.NoWrap
            }
        }
    }
}
