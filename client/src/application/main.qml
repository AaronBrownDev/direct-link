/*
 * File: main.qml
 * Author: Justin Williams
 * Date: 2/10/26
 * File Description: The qml file that the application loads on startup.
   Currently, it contains a nonfunctional window for the Director session page.
   The Leave button closes the application.
 */

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
        id: root
        property string user_type: "Director"
        property real main_aspect_ratio: 16 / 9
        property real max_camera_count: 4

        visible: true
        width: 1500
        height: 1000
        color: "#0F172A"
        title: "Direct Link Session"

        ColumnLayout {
            id: dl_root_layout
            spacing: 15
            anchors.fill: parent

            Rectangle {
                id: dl_header
                color: "#1E293B"
                Layout.fillWidth: true
                Layout.preferredHeight: 75

                Text {
                    id: dl_header_logo
                    text: "DirectLink"
                    anchors {
                        left: parent.left
                        verticalCenter: parent.verticalCenter
                    }
                    anchors.leftMargin: 20
                    color: "white"
                    font.bold: true
                    font.pointSize: 24
                }

                Text {
                    id: dl_header_type
                    text: " | " + root.user_type
                    anchors {
                        left: dl_header_logo.right
                        verticalCenter: dl_header_logo.verticalCenter
                    }
                    color: "#94A3B8"
                    font.bold: true
                    font.pointSize: 20
                }

                RoundButton {
                    id: dl_control_app_settings
                    radius: 35
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 20
                    icon {
                        source: "qrc:/resources/icons/settings.png"
                        width: radius
                        height: radius
                    }

                }


            }

            RowLayout {
                id: dl_layout_session_info
                spacing: 50
                Layout.alignment: Qt.AlignHCenter

                Text {
                    id: dl_label_live
                    text: "LIVE"
                    color: "white"
                    font.pointSize: 18
                }

                Text {
                    id: dl_label_duration
                    text: "xx:xx:xx"
                    color: "white"
                    font.pointSize: 18
                }

                Text {
                    id: dl_label_room_id
                    text: "0000-0000-0000"
                    color: "white"
                    font.pointSize: 18
                }

                Rectangle {
                    id: dl_bg_label_latency
                    color: "#4AEE80"
                    Layout.preferredHeight: 70
                    Layout.preferredWidth: 180
                    radius: Layout.preferredHeight / 2

                    Text {
                        id: dl_label_latency
                        text: "0 ms"
                        color: "black"
                        font.pointSize: 18
                        anchors.centerIn: parent
                    }
                }

                Rectangle {
                    id: dl_bg_label_quality
                    color: "#D9D9D9"
                    Layout.preferredHeight: 70
                    Layout.preferredWidth: 200
                    radius: Layout.preferredHeight / 2

                    Text {
                        id: dl_label_quality
                        text: "4K60"
                        color: "black"
                        font.pointSize: 18
                        anchors.centerIn: parent
                    }

                    RoundButton {
                        id: dl_control_video_settings
                        radius: parent.radius
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        icon {
                            source: "qrc:/resources/icons/settings.png"
                            width: radius
                            height: radius
                        }
                    }
                }
            }

            RowLayout {
                id: dl_layout_cameras
                spacing: 15
                Layout.margins: 15

                Item { Layout.fillWidth: true }

                Rectangle {
                    id: dl_bg_active_camera
                    color: "black"
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: true
                    Layout.preferredHeight: width / main_aspect_ratio
                    Layout.maximumHeight: dl_layout_cameras.height
                    Layout.maximumWidth: Layout.maximumHeight * main_aspect_ratio
                    Layout.preferredWidth: Layout.maximumWidth
                    radius: 15

                    Text {
                        text: "Main Camera (16:9)"
                        anchors.centerIn: parent
                        color: "white"
                    }
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    id: dl_bg_camera_list
                    color: "#1E293B"
                    Layout.fillHeight: true
                    Layout.preferredWidth: 300
                    radius: 15


                    ColumnLayout {
                        id: dl_layout_camera_list
                        spacing: 15
                        anchors.fill: parent
                        anchors.margins: parent.radius + 5

                        property int index: 0

                        Repeater {
                            model: max_camera_count
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: width / main_aspect_ratio
                                color: "black"
                                radius: 15

                                Text {
                                    anchors.centerIn: parent
                                    color: "white"
                                    text: "Camera " + (index + 1)
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: dl_bg_camera_info
                    color: "#1E293B"
                    Layout.fillHeight: true
                    Layout.preferredWidth: 300
                    radius: 15

                    ColumnLayout {
                        id: dl_layout_checklist
                        spacing: 25
                        anchors.fill: parent
                        anchors.margins: parent.radius + 5

                        Text {
                            id: dl_label_checklist
                            text: "Status Checklist"
                            color: "white"
                            font.pointSize: 16
                            font.bold: true
                        }

                        Text {
                            id: dl_label_checklist_connection
                            text: "Camera Connected"
                            color: "white"
                            font.pointSize: 14
                        }

                        Text {
                            id: dl_label_checklist_gpu
                            text: "GPU encoding ready"
                            color: "white"
                            font.pointSize: 14
                        }

                        Text {
                            id: dl_label_checklist_network
                            text: "Network Stable"
                            color: "white"
                            font.pointSize: 14
                        }

                        Text {
                            id: dl_label_checklist_director
                            text: "Waiting for director"
                            color: "white"
                            font.pointSize: 14
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }

            Rectangle {
                id: dl_bg_footer
                color: "#1E293B"
                Layout.fillWidth: true
                Layout.preferredHeight: 100

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    Text {
                        id: dl_label_field_address
                        text: "Address"
                        color: "white"
                        font.pointSize: 18
                    }

                    Rectangle {
                        id: dl_bg_field_address
                        width: 400
                        height: 50
                        color: "#0F172A"
                        radius: 5

                        Layout.rightMargin: 20

                        TextInput {
                            id: dl_field_address
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 15
                            width: parent.width
                            color: "white"
                            maximumLength: 24
                            font.pointSize: 18
                        }
                    }

                    Text {
                        id: dl_label_field_room_id
                        text: "Room ID"
                        color: "white"
                        font.pointSize: 18
                    }

                    Rectangle {
                        id: dl_bg_field_room_id
                        width: 400
                        height: 50
                        color: "#0F172A"
                        radius: 5

                        Layout.rightMargin: 20

                        TextInput {
                            id: dl_field_room_id
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 15
                            width: parent.width
                            color: "white"
                            maximumLength: 12
                            font.pointSize: 18
                        }
                    }

                    Button {
                        id: dl_control_connect
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 50
                        background: Rectangle {
                            radius: 25
                            color: dl_control_connect.down ? "#6AE276" : "#77FF85"
                            Text {
                                text: "Connect"
                                font.pointSize: 15
                                color: "black"
                                anchors.centerIn: parent
                            }
                        }
                    }

                    // Spacer
                    Item { Layout.fillWidth: true }

                    Button {
                        id: dl_control_app_exit
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 50
                        background: Rectangle {
                            radius: 25
                            color: dl_control_app_exit.down ? "#B02120" : "#EC221F"
                            Text {
                                text: "Leave"
                                font.pointSize: 15
                                color: "white"
                                anchors.centerIn: parent
                            }
                        }
                        onClicked: Qt.quit()
                    }

                }


            }
        }
}
