pragma Singleton
import QtQuick

QtObject {
    // Backgrounds
    readonly property color background: "#0F172A"
    readonly property color surface: "#1E293B"
    readonly property color innerSurface: "#172032"

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

    readonly property color neutral: "#D9D9D9"
    readonly property color neutralHover: "#F7F7F7"
    readonly property color neutralPressed: "#C5C5C5"

    readonly property color cameraCasting: "#1089b9"

    // Connection Status

    readonly property color statusConnected: "#10B981"
    readonly property color statusConnecting: "#FFCC26"
    readonly property color statusDisconnected: "#475569"

    // Text
    readonly property color textWhite: "#FFFFFF"
    readonly property color textBlack: "#000000"
    readonly property color textMuted: "#94A3B8"

    // Logging
    readonly property color logInfo: "#12CC8E"
    readonly property color logWarning: "#FFB84D"
    readonly property color logError: '#EF4343'
}
