import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ui
import ui.controls
import ui.theme

ColumnLayout {
    id: dl_root_layout

    property string user_type: "Director"

    ColumnLayout {
        id: dl_content_layout

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: 60
        spacing: 30

        Text {
            id: dl_dash_title

            Layout.alignment: Qt.AlignLeft

            text: user_type + " Dashboard"
            color: Theme.textWhite
            font.pointSize: 36
            font.bold: true
        }

        Loader {
            id: dl_dash_view_laoder
            Layout.fillWidth: true
            sourceComponent: dl_root_layout.user_type === "Director" ? dl_dash_director_view : dl_dash_operator_view
        }

        Component {
            id: dl_dash_director_view
            DashboardDirectorView {
                onJoinClicked: (roomCode) => {
                   dl_root_layout.StackView.view.push(dl_session_page_component, {
                      user_type: dl_root_layout.user_type,
                      room_code: roomCode
                    })
                }

                onQuickJoinClicked: () => {
                    dl_root_layout.StackView.view.push(dl_session_page_component, {
                       user_type: dl_root_layout.user_type
                    })
                }

                onCreateClicked: (projectName, sessionDesc, qualitySettings, cameraCount) => {
                    dl_root_layout.StackView.view.push(dl_session_page_component, {
                        user_type: dl_root_layout.user_type,
                        max_camera_count: cameraCount
                    })
                }
            }
        }

        Component {
            id: dl_dash_operator_view
            DashboardOperatorView {
                onJoinClicked: (roomCode, cameraName) => {
                   dl_root_layout.StackView.view.push(dl_session_page_component, {
                      user_type: dl_root_layout.user_type,
                      room_code: roomCode
                    })
                }

                onQuickJoinClicked: () => {
                    dl_root_layout.StackView.view.push(dl_session_page_component, {
                       user_type: dl_root_layout.user_type
                    })
                }
            }
        }

        RecentSessionList {
            id: dl_dash_recent_sessions

            Layout.minimumHeight: 200

            onSessionSelected: (roomCode, maxCameras) => {
               dl_root_layout.StackView.view.push(dl_session_page_component, {
                    user_type: dl_root_layout.user_type,
                    max_camera_count: maxCameras,
                    room_code: roomCode
               })
           }
        }
    }

    Component {
        id: dl_session_page_component
        SessionPage {}
    }

}
