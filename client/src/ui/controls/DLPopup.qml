/*
 * File: DLPopup.qml
 * Author: Justin Williams
 * Date: 3/12/26
 * File Description: A component that provides a stylized popup menu.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ui.theme

/*
    PROPERTIES

        displayText:string - The message that displays in the popup
        inputType:InputType - Determines the options available in the popup

    SIGNALS

        confirmed() - Fires when the confirm option is selected, if available
 */
Popup {
    id: dl_popup

    enum InputType {
        Cancel,
        ConfirmCancel
    }

    property string displayText: ""
    property int inputType: DLPopup.InputType.ConfirmCancel

    property string confirmText: "Confirm"
    property int confirmType: DLButton.ButtonType.Danger
    property string closeText: "Close"
    property int closeType: DLButton.ButtonType.Secondary

    anchors.centerIn: parent
    width: 500
    height: 200

    modal: true
    dim: true

    background: Rectangle {
        color: Theme.surface
        radius: 15
    }

    signal confirmed

    ColumnLayout {
        id: dl_popup_layout

        anchors.fill: parent
        anchors.margins: 15
        spacing: 20

        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true

            text: dl_popup.displayText
            color: Theme.textWhite
            font.pointSize: 14
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true

            spacing: 20

            Item {
                Layout.fillWidth: true
            }

            DLButton {
                Layout.preferredWidth: 200
                Layout.preferredHeight: 55

                visible: dl_popup.inputType === DLPopup.InputType.ConfirmCancel
                button_text: dl_popup.confirmText
                button_type: dl_popup.confirmType
                onClicked: {
                    dl_popup.confirmed();
                    dl_popup.close();
                }
            }

            DLButton {
                Layout.preferredWidth: 200
                Layout.preferredHeight: 55

                button_text: dl_popup.closeText
                button_type: dl_popup.closeType
                onClicked: {
                    dl_popup.close();
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }
}
