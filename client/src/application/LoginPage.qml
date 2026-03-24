/*
 * File: LoginPage.qml
 * Author: Justin Williams
 * Date: 3/24/26
 * File Description: The qml file that contains the application login page.
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import ui.controls
import ui.theme

Rectangle {
    id: dl_page_bg

    color: Theme.background

    ColumnLayout {
        id: dl_root_layout

        anchors.fill: parent
        spacing: 40

        Item {Layout.fillHeight: true}

        Text {
            id: dl_login_header

            Layout.alignment: Qt.AlignHCenter

            text: "Welcome"
            color: Theme.textWhite
            font.pointSize: 36
            font.bold: true
        }

        Text {
            id: dl_login_subtitle

            Layout.alignment: Qt.AlignHCenter

            text: "Please enter your credentials to continue"
            color: Theme.textMuted
            font.pointSize: 20
        }

        Rectangle {
            id: dl_login_bg

            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: parent.width / 2
            Layout.preferredHeight: dl_login_layout.implicitHeight + dl_login_layout.anchors.margins * 2

            color: Theme.surface
            radius: 15

            ColumnLayout {
                id: dl_login_layout

                anchors.fill: parent
                anchors.margins: 30

                spacing: 30

                DLTextField {
                    id: dl_name_field

                    Layout.fillWidth: true

                    label: "Username"
                    maxLength: 24
                }

                Text {
                    id: dl_role_label
                    text: "Select Role"
                    color: Theme.textMuted
                    font.pointSize: 14
                }

                ColumnLayout {
                    id: dl_radio_layout

                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight

                    spacing: 26

                    RadioButton {
                        id: dl_director_radio
                        checked: true
                        text: "Director"
                        
                        indicator: Rectangle {
                            implicitWidth: 26
                            implicitHeight: 26
                            x: dl_director_radio.leftPadding
                            y: parent.height / 2 - height / 2
                            radius: 13
                            color: {
                                if (dl_director_radio.down)
                                    return Theme.fieldPressed;
                                if (dl_director_radio.hovered)
                                    return Theme.fieldHover;
                                return Theme.fieldBackground
                            }

                            Rectangle {
                                width: 14
                                height: 14
                                x: 6
                                y: 6
                                radius: 7
                                color: Theme.primary
                                visible: dl_director_radio.checked
                            }
                        }

                        contentItem: Text {
                            text: dl_director_radio.text
                            color: dl_director_radio.down ? Theme.textMuted : Theme.textWhite
                            font.pointSize: 14
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: dl_director_radio.indicator.width + dl_director_radio.spacing
                        }
                    }

                    RadioButton {
                        id: dl_operator_radio
                        text: "Operator"

                        indicator: Rectangle {
                            implicitWidth: 26
                            implicitHeight: 26
                            x: dl_operator_radio.leftPadding
                            y: parent.height / 2 - height / 2
                            radius: 13
                            color: {
                                if (dl_operator_radio.down)
                                    return Theme.fieldPressed;
                                if (dl_operator_radio.hovered)
                                    return Theme.fieldHover;
                                return Theme.fieldBackground
                            }

                            Rectangle {
                                width: 14
                                height: 14
                                x: 6
                                y: 6
                                radius: 7
                                color: Theme.primary
                                visible: dl_operator_radio.checked
                            }
                        }

                        contentItem: Text {
                            text: dl_operator_radio.text
                            color: dl_operator_radio.down ? Theme.textMuted : Theme.textWhite
                            font.pointSize: 14
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: dl_operator_radio.indicator.width + dl_operator_radio.spacing
                        }
                    }
                }

                Item {Layout.fillHeight: true}

                DLButton {
                    id: dl_login_button

                    Layout.fillWidth: true

                    buttonText: "Login"
                    buttonType: DLButton.ButtonType.Primary
                }

            }

            
        }

        Item {Layout.fillHeight: true}

    }
}

