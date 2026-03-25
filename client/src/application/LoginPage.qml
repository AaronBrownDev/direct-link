/*
 * File: LoginPage.qml
 * Author: Justin Williams
 * Date: 3/24/26
 * File Description: The qml file that contains the application login page. The user
 * can enter their username and select their role before accessing the rest of the app.
 */

import QtQuick
import QtQuick.Layouts
import types
import ui.controls
import ui.theme

Rectangle {
    id: dl_page_bg

    color: Theme.background

    signal login(string userName, int userRole)

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

                spacing: 15

                DLTextField {
                    id: dl_name_field

                    Layout.fillWidth: true

                    label: "Username"
                    maxLength: 24

                    onInputChanged: {
                        dl_name_status.visible = false;
                    }
                }

                Text {
                    id: dl_name_status

                    visible: false
                    text: "Please enter a username"
                    color: Theme.danger
                    font.pointSize: 14
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

                    DLRadioButton {
                        id: dl_director_radio
                        checked: true
                        text: "Director"
                    }

                    DLRadioButton {
                        id: dl_operator_radio
                        text: "Operator"
                    }
                }

                Item {Layout.fillHeight: true}

                DLButton {
                    id: dl_login_button

                    Layout.fillWidth: true

                    button_text: "Log In"
                    button_type: DLButton.ButtonType.Primary

                    onClicked: {
                        if (dl_name_field.input.length > 0) {
                            dl_page_bg.login(dl_name_field.input, dl_director_radio.checked ? UserRole.director : UserRole.camera);
                        } else {
                            dl_name_field.state = "invalid";
                            dl_name_status.visible = true;
                        }
                    }
                }

            }

            
        }

        Item {Layout.fillHeight: true}

    }
}

