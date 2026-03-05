/*
 * File: DLTextField.qml
 * Author: Justin Williams
 * Date: 3/4/26
 * File Description: A component that provides a stylized text field with a label above it.
 */

import QtQuick
import QtQuick.Controls
import ui.theme

/*
    PROPERTIES

    label (string) - sets the text displayed above the text field
    emptyText (string) - sets the text displayed when there is no text input
    isCode (bool) - determines whether or not the input is evaluated as a
        room code (i.e. ROOM-1234)
    maxLength (int) - constrains the length of the entered text to a set amount of characters
    input (string) - an alias for the contents of the field's text input (readonly)
 */

Rectangle {
    id: dl_field

    property string label: ""
    property string emptyText: ""
    property bool isCode: false
    property int maxLength: Number.MAX_VALUE
    readonly property string input: dl_field_input.text

    color: Theme.fieldBackground
    radius: 15
    border {
        color: Theme.fieldHover
        width: 2
    }

    Text {
        id: dl_field_label

        text: dl_field.label
        anchors.bottom: parent.top
        anchors.left: parent.left
        anchors.bottomMargin: 10
        color: Theme.textMuted
        font.pointSize: 16
        font.bold: true
    }

    Text {
        id: dl_field_empty_text

        text: dl_field.emptyText
        visible: dl_field_input.length === 0
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 15
        color: Theme.textMuted
        font.pointSize: 16
    }

    TextInput {
        id: dl_field_input

        anchors.fill: parent
        anchors.margins: 15
        clip: true
        verticalAlignment: TextInput.AlignVCenter
        maximumLength: dl_field.isCode ? 9 : dl_field.maxLength
        color: Theme.textWhite
        font.pointSize: 16
        selectionColor: Theme.primary

        onTextEdited: {
            if (dl_field.isCode) {
                let clean = text.replace(/[^A-Za-z0-9]/g, "").toUpperCase()
                clean = clean.substring(0,8)
                if (clean.length > 4)
                    clean = clean.substring(0,4) + "-" + clean.substring(4)

                if (text !== clean)
                    text = clean
            }
        }
    }
}
