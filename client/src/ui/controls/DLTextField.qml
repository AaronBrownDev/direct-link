/*
 * File: DLTextField.qml
 * Author: Justin Williams
 * Date: 3/4/26
 * File Description: A component that provides a stylized text field with a label above it.
 */

import QtQuick
import QtQuick.Layouts
import ui.theme

/*
    PROPERTIES

        label:string - Sets the text displayed above the text field
        emptyText:string - Sets the text displayed when there is no text input
        isCode:bool - Determines whether or not the input is evaluated as a
            room code (i.e. ROOM-123456)
        maxLength:int - Constrains the length of the entered text to a set amount of characters
        input:string - An alias for the contents of the field's text input (readonly)

    FUNCTIONS

        clear() - Sets the field input to an empty string

    SIGNALS

        accepted() - Fires when enter/return is pressed on the field
        
 */
ColumnLayout {
    id: dl_field

    property string label: ""
    property string emptyText: ""
    property bool isCode: false
    property int maxLength: Number.MAX_VALUE

    readonly property string input: dl_field_input.text

    function clear() {
        dl_field_input.text = ""
        state: ""
    }

    implicitHeight: dl_field_label.implicitHeight + spacing + 65
    spacing: 10

    states: [
            State {
                name: "invalid"
                PropertyChanges {
                    dl_field_bg.border.color: Theme.danger
                    dl_field_bg.border.width: 2
                }
            }
        ]

    signal accepted()

    Text {
            id: dl_field_label

            text: dl_field.label
            font.pointSize: 14
            color: Theme.textMuted
        }

    Rectangle {
        id: dl_field_bg
        
        Layout.fillWidth: true
        Layout.preferredHeight: 65

        color: Theme.fieldBackground
        radius: 15
        border {
            width: 0
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
            maximumLength: dl_field.isCode ? 11 : dl_field.maxLength
            color: Theme.textWhite
            font.pointSize: 16
            selectionColor: Theme.primary

            onTextEdited: {
                dl_field.state = "";
                if (dl_field.isCode) {
                    let clean = text.replace(/[^A-Za-z0-9]/g, "").toUpperCase();
                    clean = clean.substring(0, 10);
                    if (clean.length > 4)
                        clean = clean.substring(0, 4) + "-" + clean.substring(4);

                    if (text !== clean)
                        text = clean;
                }
            }

            onAccepted: dl_field.accepted()

            // Changes cursor when hovering over field
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.IBeamCursor
                acceptedButtons: Qt.NoButton
            }
        }
    }
}