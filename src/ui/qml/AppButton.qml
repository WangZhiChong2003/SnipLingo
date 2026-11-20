import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

Basic.Button {
    id: control

    required property var colors
    property int minimumButtonWidth: 40
    property string iconSource: ""
    property int iconSize: 18

    implicitHeight: 40
    implicitWidth: Math.max(minimumButtonWidth, contentRow.implicitWidth + 30)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight

        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width)
            spacing: 6

            Image {
                visible: control.iconSource.length > 0
                source: control.iconSource
                sourceSize.width: control.iconSize
                sourceSize.height: control.iconSize
                Layout.preferredWidth: control.iconSize
                Layout.preferredHeight: control.iconSize
                fillMode: Image.PreserveAspectFit
            }
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: control.text
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: control.highlighted ? control.colors.accentText : control.colors.textPrimary
                opacity: control.enabled ? 1 : 0.55
                font.pixelSize: 14
                font.bold: control.highlighted
            }
        }
    }
    background: Rectangle {
        radius: 10
        color: {
            if (control.highlighted)
                return control.down ? Qt.darker(control.colors.accentFill, 1.12)
                                    : control.hovered ? Qt.lighter(control.colors.accentFill, 1.08)
                                                      : control.colors.accentFill;
            if (control.flat && !control.hovered && !control.down)
                return "transparent";
            return control.down || control.hovered ? control.colors.accentSoft
                                                  : control.colors.surfaceElevated;
        }
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? control.colors.accent
                      : control.highlighted ? control.colors.accentFill
                      : control.flat && !control.hovered && !control.down ? "transparent"
                                                                          : control.colors.border
        opacity: control.enabled ? 1 : 0.6
    }
}
