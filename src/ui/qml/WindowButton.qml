import QtQuick

// Frameless window-caption control shared by the main window and the floating result card.
Item {
    id: button
    required property var colors
    property int kind: 0  // 0 minimize, 1 maximize/restore, 2 close
    property bool maximized: false
    signal clicked()

    implicitWidth: 34
    implicitHeight: 30
    Accessible.role: Accessible.Button
    Accessible.name: kind === 0 ? qsTr("Minimize")
                   : kind === 1 ? qsTr("Maximize")
                   : qsTr("Close")

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: {
            if (button.kind === 2 && mouse.containsMouse)
                return button.colors.errorSoft;
            return mouse.containsMouse || mouse.pressed
                   ? button.colors.surfaceElevated : "transparent";
        }
    }
    Item {
        anchors.centerIn: parent
        width: 12; height: 12
        Rectangle {
            visible: button.kind === 0
            width: 10; height: 1.5; radius: 0.75
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
            color: button.colors.textSecondary
        }
        Item {
            visible: button.kind === 1
            anchors.fill: parent
            Rectangle {
                visible: !button.maximized
                anchors.centerIn: parent
                width: 9; height: 9; radius: 1
                color: "transparent"
                border.width: 1.5
                border.color: button.colors.textSecondary
            }
            Rectangle {
                visible: button.maximized
                x: 4; y: 1; width: 7; height: 7; radius: 1
                color: "transparent"
                border.width: 1.5
                border.color: button.colors.textSecondary
            }
            Rectangle {
                visible: button.maximized
                x: 1; y: 4; width: 7; height: 7; radius: 1
                color: "transparent"
                border.width: 1.5
                border.color: button.colors.textSecondary
            }
        }
        Item {
            visible: button.kind === 2
            anchors.fill: parent
            Rectangle {
                width: 11; height: 1.5; radius: 0.75
                anchors.centerIn: parent
                rotation: 45
                color: mouse.containsMouse ? button.colors.error : button.colors.textSecondary
            }
            Rectangle {
                width: 11; height: 1.5; radius: 0.75
                anchors.centerIn: parent
                rotation: -45
                color: mouse.containsMouse ? button.colors.error : button.colors.textSecondary
            }
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: button.clicked()
    }
}
