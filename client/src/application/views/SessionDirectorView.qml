/*
 * File: DashboardDirectorView.qml
 * Author: Justin Williams
 * Date: 3/27/26
 * File Description: The qml file that contains the camera displays for the director. As operators
 * join a room, video tracks appear as a list camera feeds. The director can select an active camera
 * from the list to transfer it to a larger feed.
 */

import QtQuick
import QtQuick.Layouts
import application
import ui.controls

RowLayout {
        id: dl_director_view

        property int max_camera_count: 4
        property int activeCamera: -1

        Layout.margins: 15

        spacing: 15

        onActiveCameraChanged: {
            if (DirectorTransport.session === null) {
                return;
            }

            let tracks = DirectorTransport.session.tracks;
            if (activeCamera >= 0 && activeCamera < tracks.length) {
                dl_active_camera.assigned_track = tracks[activeCamera];
            } else {
                dl_active_camera.assigned_track = null;
            }
        }

        SessionLog {
            id: dl_session_log
        }

        CameraFeed {
            id: dl_active_camera

            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.preferredHeight: width / aspect_ratio
            Layout.minimumHeight: implicitWidth / aspect_ratio
            implicitWidth: 500
        }

        CameraList {
            id: dl_camera_list
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            max_camera_count: dl_director_view.max_camera_count
            activeCamera: dl_director_view.activeCamera

            onCameraSelected: (index) => {
                dl_director_view.activeCamera = index;
            }
        }

        Connections {
            target: DirectorTransport.session

            function onTrackAdded(index) {
                dl_camera_list.all_tracks = DirectorTransport.session.tracks;
                if (index <= dl_director_view.activeCamera) {
                    dl_director_view.activeCamera++;
                }
            }

            function onTrackRemoved(index) {
                dl_camera_list.all_tracks = DirectorTransport.session.tracks;
                if (index === dl_director_view.activeCamera) {
                    dl_director_view.activeCamera = -1;
                } else if (index < dl_director_view.activeCamera) {
                    dl_director_view.activeCamera--;
                }
            }
        }
    }