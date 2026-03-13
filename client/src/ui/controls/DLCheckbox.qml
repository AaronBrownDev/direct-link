/*
 * File: DLCheckbox.qml
 * Author: Justin Williams
 * Date: 3/12/26
 * File Description: A component that provides a stylized checkbox.
 */

import QtQuick
import QtQuick.Layouts
import ui.theme

Item {
    id: dl_checkbox

    property bool checked: false
    property bool active: true
    property string label: ""

    implicitWidth: dl_checkbox_layout.implicitWidth
    implicitHeight: dl_checkbox_layout.implicitHeight

    RowLayout {
        id: dl_checkbox_layout

        spacing: 10

        Rectangle {
            id: dl_checkbox_bg

            opacity: dl_checkbox.active ? 1 : 0.5
            color: {
                if (dl_checkbox.checked) {
                    if (dl_checkbox_area.pressed)
                        return Theme.primaryPressed;
                    if (dl_checkbox_area.containsMouse)
                        return Theme.primaryHover;
                    return Theme.primary;
                } else {
                    if (dl_checkbox_area.pressed)
                        return Theme.fieldPressed;
                    if (dl_checkbox_area.containsMouse)
                        return Theme.fieldHover;
                    return Theme.fieldBackground;
                }
            }
            radius: 5
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30

            Image {
                id: dl_check_icon

                anchors.fill: parent
                anchors.margins: 5
                visible: dl_checkbox.checked
                source: "qrc:/resources/icons/checkIcon.png"
            }
        }

        Text {
            id: dl_checkbox_label

            font.pointSize: 14
            color: Theme.textMuted
            text: dl_checkbox.label
        }
    }

    MouseArea {
        id: dl_checkbox_area

        anchors.fill: parent
        hoverEnabled: true
        enabled: dl_checkbox.active

        onClicked: dl_checkbox.checked = !dl_checkbox.checked
    }
}
