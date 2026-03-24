/*
 * File: RecentSessionList.qml
 * Author: Justin Williams
 * Date: 2/10/26
 * File Description: A qml file containing a component that lists sessions. List items
 * display the room code, room status, creation timestamp, and max cameras of that
 * session.
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import network
import ui.controls
import ui.theme

/*
    PROPERTIES

        fetchPending:bool - Set to true when a refresh was performed, set to false when sessions
            have been received or when an error signal was emitted from SessionClient after a refresh
        allSessions:var - A container for sessions retrieved from SessionClient

    FUNCTIONS

        populateModel():void - Clear and add all sessions currently in the allSessions property to the
            ListView model. If the active session filter is checked, only active sessions will populate
            the model

    SIGNALS

        sessionSelected(roomCode:string, maxCameras:int) - Fires when a session has been selected from
            the list
        refreshClicked() - Fires when the refresh button has been clicked
 */
ColumnLayout {
    id: dl_session_list_layout

    property bool fetchPending: false
    property var allSessions: []

    function populateModel() {
        dl_session_model.clear();
        for (let i = 0; i < allSessions.length; i++) {
            if (!dl_list_filter.checked || allSessions[i].roomStatus === "active")
                dl_session_model.append(allSessions[i]);
        }
    }

    signal sessionSelected(string roomCode, int maxCameras)
    signal refreshClicked

    spacing: 15

    RowLayout {
        id: dl_session_list_header

        spacing: 20

        Text {
            id: dl_session_list_label

            Layout.alignment: Qt.AlignLeft

            text: "Recent Sessions"
            color: Theme.textWhite
            font.pointSize: 20
            font.bold: true
        }

        DLButton {
            id: dl_list_refresh

            Layout.preferredHeight: 50
            Layout.preferredWidth: 50

            buttonType: DLButton.ButtonType.Neutral

            onClicked: {
                dl_session_list_layout.fetchPending = true;
                dl_session_list_layout.refreshClicked();
            }

            Image {
                id: dl_list_refresh_icon

                anchors.fill: parent
                anchors.margins: 10
                source: "qrc:/resources/icons/refreshIcon.png"
            }
        }

        DLCheckbox {
            id: dl_list_filter

            label: "Show Active Sessions Only"
            onCheckedChanged: dl_session_list_layout.populateModel()
        }
    }

    Rectangle {
        id: dl_session_list_bg

        Layout.fillWidth: true
        Layout.minimumHeight: 50
        Layout.fillHeight: true

        color: Theme.surface
        radius: 15

        ListView {
            id: dl_session_list

            anchors.fill: parent
            anchors.margins: 15
            clip: true
            spacing: 10

            model: ListModel {
                id: dl_session_model
            }

            delegate: Rectangle {
                width: ListView.view.width
                height: 60
                color: dl_session_list_area.containsMouse ? Theme.innerSurface : Theme.surface
                radius: 10

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 5
                    Text {
                        Layout.alignment: Qt.AlignLeft
                        text: roomCode
                        color: roomStatus === "active" ? Theme.textWhite : Theme.textMuted
                        font.pointSize: 12
                    }
                    Text {
                        Layout.alignment: Qt.AlignLeft
                        text: "Created " + createdAt + " - " + roomStatus.charAt(0).toUpperCase() + roomStatus.slice(1) + " - " + maxCameras + " Cameras"
                        color: Theme.textMuted
                        font.pointSize: 10
                    }
                }

                MouseArea {
                    id: dl_session_list_area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: dl_session_list_layout.sessionSelected(roomCode, maxCameras)
                }
            }
        }

        Connections {
            target: SessionClient

            function onSessionsReceived(sessions) {
                dl_session_list_layout.allSessions = sessions;
                dl_session_list_layout.populateModel();
                dl_session_list_layout.fetchPending = false;
            }

            function onError(msg) {
                if (dl_session_list_layout.fetchPending && msg !== "session is closed") {
                    dl_session_model.clear();
                    dl_session_list_layout.fetchPending = false;
                }
            }
        }

        Text {
            anchors.centerIn: parent
            font.pointSize: 16
            visible: dl_session_model.count === 0
            text: "No sessions found"
            color: Theme.textMuted
        }
    }
}
