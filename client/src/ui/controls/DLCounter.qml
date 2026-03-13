/*
 * File: DLCounter.qml
 * Author: Justin Williams
 * Date: 3/4/26
 * File Description: A component that provides a stylized counter.
 */

import QtQuick
import QtQuick.Controls
import ui.theme

/*
    PROPERTIES

    value (int) - The current value of the counter
    minimum (int) - The smallest possible value the counter can decrement to
    maximum (int) - The largest possible value the counter can increment to
    step (int) - The amount the counter value changes by when it increments or
        decrements
    label (string) - The display text to the right of the counter
 */
Rectangle {
    id: dl_counter_bg

    property int value: 1
    property int minimum: 1
    property int maximum: 10
    property int step: 1
    property string label: ""

    radius: 15
    color: Theme.fieldBackground

    Text {
        id: dl_counter_value_label

        anchors.centerIn: parent
        font.pointSize: 14
        color: Theme.textWhite
        text: dl_counter_bg.value
    }

    Text {
        id: dl_counter_label

        anchors.left: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 10
        font.pointSize: 14
        color: Theme.textMuted
        text: dl_counter_bg.label
    }

    Rectangle {
        id: dl_decrement_bg

        anchors.left: parent.left
        width: parent.width * 0.25
        height: parent.height
        topLeftRadius: parent.radius
        bottomLeftRadius: parent.radius
        color: {
            if (dl_decrement.pressed || dl_counter_bg.value <= dl_counter_bg.minimum) return Theme.fieldPressed
            if (dl_decrement.containsMouse) return Theme.fieldHover
            return Theme.fieldBackground
        }

        MouseArea {
            id: dl_decrement

            anchors.fill: parent
            hoverEnabled: true

            onClicked: {
                if (dl_counter_bg.value > dl_counter_bg.minimum)
                    dl_counter_bg.value -= dl_counter_bg.step
            }

            Text {
                id: dl_decrement_text

                anchors.centerIn: parent
                font.pointSize: 14
                font.bold: true
                color: Theme.textMuted
                text: "-"
            }
        }
    }

    Rectangle {
        id: dl_increment_bg

        anchors.right: parent.right
        width: parent.width * 0.25
        height: parent.height
        topRightRadius: parent.radius
        bottomRightRadius: parent.radius
        color: {
            if (dl_increment.pressed || dl_counter_bg.value >= dl_counter_bg.maximum) return Theme.fieldPressed
            if (dl_increment.containsMouse) return Theme.fieldHover
            return Theme.fieldBackground
        }

        MouseArea {
            id: dl_increment

            anchors.fill: parent
            hoverEnabled: true

            onClicked: {
                if (dl_counter_bg.value < dl_counter_bg.maximum)
                    dl_counter_bg.value += dl_counter_bg.step
            }

            Text {
                id: dl_increment_text

                anchors.centerIn: parent
                font.pointSize: 14
                font.bold: true
                color: Theme.textMuted
                text: "+"
            }
        }
    }


}
