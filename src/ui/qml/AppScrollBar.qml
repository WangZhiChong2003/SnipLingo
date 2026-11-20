import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ScrollBar {
    id: control

    required property var colors

    implicitWidth: 10
    implicitHeight: 10
    minimumSize: 0.12
    padding: 2
    hoverEnabled: true
    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: control.colors.scrollThumb
        opacity: control.size > 0 && control.size < 0.999
                 ? (control.pressed || control.hovered ? 0.95 : 0.55) : 0
    }
    background: Item { }
}
