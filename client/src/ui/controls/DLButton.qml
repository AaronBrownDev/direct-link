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

        button_text:string - The text that will display inside of the button
        button_type:int - Determines the appearance of the button. It can be set
            using the primaryButton, neutralButton, or dangerButton values stored in
            the object
        active:bool - Controls whether or not the button can be clicked

    SIGNALS

        clicked() - Fires when the button has been clicked
 */
Rectangle {
    id: dl_button_bg

    enum ButtonType {
        Primary,
        Secondary,
        Danger,
        Default
    }

    property string button_text: ""
    property int button_type: DLButton.ButtonType.Default
    property bool active: true

    signal clicked

    color: switch (button_type) {
    case DLButton.ButtonType.Secondary:
        if (dl_button_area.pressed || !active)
            return Theme.fieldPressed;
        if (dl_button_area.containsMouse)
            return Theme.fieldHover;
        return Theme.fieldBackground;
    case DLButton.ButtonType.Danger:
        if (dl_button_area.pressed || !active)
            return Theme.dangerPressed;
        if (dl_button_area.containsMouse)
            return Theme.dangerHover;
        return Theme.danger;
    case DLButton.ButtonType.Primary:
        if (dl_button_area.pressed || !active)
            return Theme.primaryPressed;
        if (dl_button_area.containsMouse)
            return Theme.primaryHover;
        return Theme.primary;
    default:
        if (dl_button_area.pressed || !active)
            return Theme.neutralPressed;
        if (dl_button_area.containsMouse)
            return Theme.neutralHover;
        return Theme.neutral;
    }

    height: 65
    radius: 15

    Text {
        id: dl_button_label

        anchors.centerIn: parent
        font.pointSize: 14
        font.bold: true
        text: dl_button_bg.button_text
        color: dl_button_bg.active ? Theme.textWhite : Theme.textMuted
    }

    MouseArea {
        id: dl_button_area
        anchors.fill: parent
        enabled: dl_button_bg.active
        hoverEnabled: true
        onClicked: {
            dl_button_bg.clicked();
        }
    }
}
