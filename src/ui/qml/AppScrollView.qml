import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic

Basic.ScrollView {
    id: control

    required property var colors

    ScrollBar.vertical: AppScrollBar {
        objectName: "verticalScrollBar"
        parent: control
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        colors: control.colors
    }
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
}
