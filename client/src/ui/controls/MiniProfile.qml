/*
 * File: MiniProfile.qml
 * Author: Justin Williams
 * Date: 3/24/26
 * File Description: A component that provides a mini profile display. The display shows 
 * the user's id and role, and a logout button is also visible. Visibility can be controlled
 * by the state.
 */

import QtQuick
import QtQuick.Layouts
import types
import ui.theme

/*
    PROPERTIES

        user_id:string - Sets the text displayed for the user's ID
        user_type:int - Determines the text displayed for the user's role
        states - Control the visibility of the profile
            "visible" - The control can be seen, and the logout button is enabled
            "hidden" - The control cannot be seen, and the logout button is disabled

    SIGNALS

        logoutClicked() - Fires when the logout button has been clicked
        
 */
Rectangle {
        id: dl_profile

        property string user_id: ""
        property int user_type: 0

        color: Theme.innerSurface
        width: dl_profile_layout.implicitWidth + 50
        height: 80
        radius: 15

        states: [
            State {
                name: "visible"
                PropertyChanges {
                    dl_profile.opacity: 1
                    dl_logout_button.enabled: true
                }
            },
            State {
                name: "hidden"
                PropertyChanges {
                    dl_profile.opacity: 0
                    dl_logout_button.enabled: false
                }
            }
        ]

        signal logoutClicked()

        RowLayout {
            id: dl_profile_layout

            anchors.fill: parent
            anchors.margins: 15

            ColumnLayout {

                Layout.fillHeight: true

                Text {
                    id: dl_profile_id

                    text: "ID: " + dl_profile.user_id
                    color: Theme.textWhite
                    font.pointSize: 12
                }

                Text {
                    id: dl_profile_role

                    text: "Role: " + UserRole.toString(dl_profile.user_type, true)
                    color: Theme.textWhite
                    font.pointSize: 12
                }
            }

            Rectangle {
                id: dl_profile_separator

                Layout.preferredWidth: 2
                Layout.fillHeight: true

                color: Theme.surface
            }

            DLButton {
                id: dl_logout_button

                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 100
                Layout.preferredHeight: 30

                button_type: DLButton.ButtonType.Danger
                button_text: "Log Out"
                radius: 5

                onClicked: dl_profile.logoutClicked()
            }

        }

        Behavior on opacity { PropertyAnimation {
            duration: 250
            easing.type: Easing.InOutQuad
        } }
    }