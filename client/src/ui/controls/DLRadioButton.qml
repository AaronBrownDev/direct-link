/*
 * File: DLRadioButton.qml
 * Author: Justin Williams
 * Date: 3/24/26
 * File Description: A component that provides a stylized radio button.
 */

import QtQuick
import QtQuick.Controls
import ui.theme

RadioButton {
    id: dl_radio
    
    indicator: Rectangle {
        id: dl_radio_indicator
        implicitWidth: 30
        implicitHeight: 30
        x: dl_radio.leftPadding
        y: parent.height / 2 - height / 2
        radius: 15
        color: {
            if (dl_radio.down)
                return Theme.fieldPressed;
            if (dl_radio.hovered)
                return Theme.fieldHover;
            return Theme.fieldBackground
        }

        Rectangle {
            width: dl_radio_indicator.radius + 1
            height: dl_radio_indicator.radius + 1
            x: dl_radio_indicator.radius - radius
            y: dl_radio_indicator.radius - radius
            radius: width / 2
            color: Theme.primary
            visible: dl_radio.checked
        }
    }

    contentItem: Text {
        text: dl_radio.text
        color: dl_radio.down ? Theme.textMuted : Theme.textWhite
        font.pointSize: 14
        verticalAlignment: Text.AlignVCenter
        leftPadding: dl_radio.indicator.width + dl_radio.spacing
    }
}