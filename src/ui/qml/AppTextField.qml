import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.TextField {
    id: control

    required property var colors

    implicitHeight: 40
    leftPadding: 12
    rightPadding: 12
    color: colors.textPrimary
    placeholderTextColor: colors.textSecondary
    selectionColor: colors.accentFill
    selectedTextColor: colors.accentText
    selectByMouse: true
    font.pixelSize: colors.fontOriginal
    verticalAlignment: TextInput.AlignVCenter
    background: Rectangle {
        radius: colors.radiusControl
        color: colors.surface
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? colors.accent : colors.border
        opacity: control.enabled ? 1 : 0.6
    }
}
