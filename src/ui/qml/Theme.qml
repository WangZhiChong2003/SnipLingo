import QtQuick

// Shared semantic colours and spacing for the application windows.
QtObject {
    property SystemPalette systemPalette: SystemPalette { colorGroup: SystemPalette.Active }
    property bool dark: systemPalette.window.hslLightness < 0.5

    readonly property color background: dark ? "#101725" : "#F2F4FA"
    readonly property color surface: dark ? "#1A2435" : "#FFFFFF"
    readonly property color surfaceElevated: dark ? "#242F43" : "#F6F8FD"
    readonly property color border: dark ? "#35435A" : "#E0E6F1"
    readonly property color textPrimary: dark ? "#F1F4FC" : "#202B42"
    readonly property color textSecondary: dark ? "#A9B8CE" : "#6B7890"
    readonly property color accent: dark ? "#91ABFF" : "#5263E0"
    readonly property color accentSoft: dark ? "#293A65" : "#EDF0FF"
    readonly property color toggleOff: dark ? "#536078" : "#CAD2E0"
    readonly property color error: dark ? "#FF9A91" : "#BA3D37"
    readonly property color errorSoft: dark ? "#492B33" : "#FFF1EF"

    readonly property int radiusWindow: 16
    readonly property int radiusControl: 10
    readonly property int spacing: 12
    readonly property int padding: 18
    readonly property int fontBody: 15
    readonly property int fontOriginal: 13
    readonly property int fontMeta: 12
}
