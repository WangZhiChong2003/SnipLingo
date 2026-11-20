import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

Basic.ComboBox {
    id: control

    required property var colors
    property var formatOption: function(value) { return value; }
    // The control itself scales with the window content layer that contains it; its popup lives
    // on the overlay, so it needs the same factor applied explicitly.
    property real zoom: 1.0

    implicitHeight: 40
    leftPadding: 12
    rightPadding: 34
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    contentItem: Text {
        text: control.displayText
        textFormat: Text.PlainText
        color: control.colors.textPrimary
        opacity: control.enabled ? 1 : 0.55
        font.pixelSize: control.colors.fontOriginal
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Item {
        x: control.width - width - 13
        y: (control.height - height) / 2
        width: 12
        height: 8
        Rectangle {
            x: 1; y: 3; width: 6; height: 1.5; radius: 1
            rotation: 45
            color: control.colors.textSecondary
        }
        Rectangle {
            x: 5; y: 3; width: 6; height: 1.5; radius: 1
            rotation: -45
            color: control.colors.textSecondary
        }
    }
    background: Rectangle {
        radius: control.colors.radiusControl
        color: control.down ? control.colors.surfaceElevated : control.colors.surface
        border.width: control.visualFocus || control.popup.visible ? 2 : 1
        border.color: control.visualFocus || control.popup.visible ? control.colors.accent
                                                                 : control.colors.border
        opacity: control.enabled ? 1 : 0.6
    }

    delegate: Basic.ItemDelegate {
        id: option
        required property int index
        objectName: "comboOption" + index
        width: ListView.view ? ListView.view.width : control.width
        implicitHeight: 40
        leftPadding: 12
        rightPadding: 12
        hoverEnabled: true
        highlighted: control.highlightedIndex === index
        text: control.formatOption(control.textAt(index))

        contentItem: RowLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: option.text
                textFormat: Text.PlainText
                color: control.colors.textPrimary
                font.pixelSize: control.colors.fontOriginal
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                visible: option.index === control.currentIndex
                text: "✓"
                color: control.colors.accent
                font.pixelSize: 15
                font.bold: true
            }
        }
        background: Rectangle {
            radius: 8
            color: option.highlighted || option.hovered ? control.colors.accentSoft : "transparent"
        }
    }
    popup: Basic.Popup {
        y: control.height + 6
        width: control.width
        padding: 6
        margins: 8
        scale: control.zoom
        transformOrigin: Item.TopLeft
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 280)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { colors: control.colors }
        }
        background: Rectangle {
            radius: 12
            color: control.colors.surface
            border.color: control.colors.border
        }
    }
}
