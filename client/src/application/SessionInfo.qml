import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15


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
