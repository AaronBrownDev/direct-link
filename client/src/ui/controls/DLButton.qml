/*
 * File: DLButton.qml
 * Author: Justin Williams
 * Date: 3/4/26
 * File Description: A component that provides a stylized, rectangular button.
 */

import QtQuick
import ui.theme

/*
    PROPERTIES

    buttonText (string) - The text that will display inside of the button
    buttonType (int) - Determines the appearance of the button. It can be set
        using the primaryButton, neutralButton, or dangerButton values stored in
        the object
 */
Rectangle {
    id: dl_button_bg

    // // Button Types
    // readonly property int primaryButton: 0
    // readonly property int neutralButton: 1
    // readonly property int dangerButton: 2
    enum ButtonType {
        Primary,
        Neutral,
        Danger
    }

    property string buttonText: ""
    property int buttonType: DLButton.ButtonType.Primary
    property bool active: true

    signal clicked()

    color: switch (buttonType) {
           case DLButton.ButtonType.Neutral:
               if (dl_button_area.pressed || !active) return Theme.fieldPressed
               if (dl_button_area.containsMouse) return Theme.fieldHover
               return Theme.fieldBackground
           case DLButton.ButtonType.Danger:
               if (dl_button_area.pressed || !active) return Theme.dangerPressed
               if (dl_button_area.containsMouse) return Theme.dangerHover
               return Theme.danger
           default:
               if (dl_button_area.pressed || !active) return Theme.primaryPressed
               if (dl_button_area.containsMouse) return Theme.primaryHover
               return Theme.primary
    }
    radius: 15

    Text {
        id: dl_button_label

        anchors.centerIn: parent
        font.pointSize: 14
        font.bold: true
        text: buttonText
        color: active ? Theme.textWhite : Theme.textMuted
    }

    MouseArea {
        id: dl_button_area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            if (active)
                dl_button_bg.clicked()
        }
    }

}


