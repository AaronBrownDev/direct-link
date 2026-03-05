pragma Singleton
import QtQuick 2.15

QtObject {
    // Backgrounds
    readonly property color background: "#0F172A"
    readonly property color surface: "#1E293B"
    readonly property color innerSurface: "#172032"

    readonly property color neutral: "#D9D9D9"

    // Interactive
    readonly property color primary: "#10B981"
    readonly property color primaryHover: "#12CC8E"
    readonly property color primaryPressed: "#0EA371"

    readonly property color danger: "#DD3E3C"
    readonly property color dangerHover: "#EF4343"
    readonly property color dangerPressed: "#C63737"

    readonly property color inactive: "#404244"

    readonly property color fieldBackground: "#334155"
    readonly property color fieldHover: "#475569"
    readonly property color fieldPressed: "#283548"

    // Text
    readonly property color textWhite: "#ffffff"
    readonly property color textBlack: "#000000"
    readonly property color textMuted: "#94A3B8"

}
