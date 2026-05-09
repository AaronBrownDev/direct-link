import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ui.theme

RowLayout {
    id: dl_layout_session_info

    property string room_code: "XXXX-XXXX"
    property double latency_ms: 0.0
    property double dc_one_way_ms: 0.0
    property double video_lag_ms: 0.0
    property double display_gap_ms: 0.0
    // Sub-components of video_lag (sampled from libwebrtc getStats() at 1 Hz).
    // upstream_video_ms = video_lag - jitter_buffer - decode (computed by parent).
    property double upstream_video_ms: 0.0
    property double jitter_buffer_ms: 0.0
    property double decode_ms: 0.0
    property bool show_latency: true
    property int video_width: 0
    property int video_height: 0

    Layout.alignment: Qt.AlignHCenter

    spacing: 50

    Text {
        id: dl_label_live
        text: "LIVE"
        color: Theme.textWhite
        font.pointSize: 18
    }

    Text {
        id: dl_label_duration
        text: "xx:xx:xx"
        color: Theme.textWhite
        font.pointSize: 18
    }

    Text {
        id: dl_label_room_code
        text: room_code
        color: Theme.textWhite
        font.pointSize: 18
    }

    ColumnLayout {
        spacing: 4
        visible: dl_layout_session_info.show_latency

        Rectangle {
            id: dl_bg_label_latency
            color: Theme.primary
            Layout.preferredHeight: 70
            Layout.preferredWidth: 180
            radius: Layout.preferredHeight / 2

            Text {
                id: dl_label_latency
                text: Math.round(dl_layout_session_info.latency_ms) + " ms"
                color: Theme.textBlack
                font.pointSize: 18
                anchors.centerIn: parent
            }
        }

        RowLayout {
            spacing: 10
            Layout.alignment: Qt.AlignHCenter

            Text {
                text: "DC " + Math.round(dl_layout_session_info.dc_one_way_ms) + "ms"
                color: Theme.textWhite
                font.pointSize: 9
            }
            Text {
                text: "Up " + Math.round(dl_layout_session_info.upstream_video_ms) + "ms"
                color: Theme.textWhite
                font.pointSize: 9
            }
            Text {
                text: "JB " + Math.round(dl_layout_session_info.jitter_buffer_ms) + "ms"
                color: Theme.textWhite
                font.pointSize: 9
            }
            Text {
                text: "Dec " + Math.round(dl_layout_session_info.decode_ms) + "ms"
                color: Theme.textWhite
                font.pointSize: 9
            }
            Text {
                text: "Disp " + Math.round(dl_layout_session_info.display_gap_ms) + "ms"
                color: Theme.textWhite
                font.pointSize: 9
            }
        }
    }

    Rectangle {
        id: dl_bg_label_quality
        color: Theme.neutral
        Layout.preferredHeight: 70
        Layout.preferredWidth: 200
        radius: Layout.preferredHeight / 2

        Text {
            id: dl_label_quality
            text: (dl_layout_session_info.video_width > 0 && dl_layout_session_info.video_height > 0)
                  ? (dl_layout_session_info.video_width + "×" + dl_layout_session_info.video_height)
                  : "—"
            color: Theme.textBlack
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
