import QtQuick
import QtQuick.Controls

// Consent sheet: shown before the first automatic send to a remote destination, and again whenever the
// destination fingerprint changes (docs/ui-ux-spec.md §3.2). Denying is always available and is the default.
Dialog {
    id: sheet

    property var controller: null
    property string destination: ""
    property bool stale: false

    Theme { id: theme }

    modal: true
    implicitWidth: 480
    title: qsTr("Send selected text to this service?")
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape
    padding: 20
    background: Rectangle {
        radius: theme.radiusWindow
        color: theme.surface
        border.color: theme.border
    }
    header: Text {
        text: sheet.title
        color: theme.textPrimary
        font.pixelSize: 19
        font.bold: true
        leftPadding: 20; rightPadding: 20; topPadding: 20
        wrapMode: Text.Wrap
    }

    component ActionButton: Button {
        id: button
        implicitHeight: 42
        implicitWidth: Math.max(104, label.implicitWidth + 32)
        contentItem: Text {
            id: label
            text: button.text
            color: button.highlighted ? "white" : theme.textPrimary
            font.pixelSize: 14
            font.bold: button.highlighted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 10
            color: button.highlighted ? (button.down ? Qt.darker(theme.accent, 1.12) : theme.accent)
                                      : button.down || button.hovered ? theme.accentSoft : theme.surfaceElevated
            border.color: button.highlighted ? theme.accent : theme.border
        }
    }

    contentItem: Column {
        spacing: 14

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            color: theme.textPrimary
            font.pixelSize: theme.fontBody
            text: sheet.stale
                  ? qsTr("The destination changed. Allow sending to %1?").arg(sheet.destination)
                  : qsTr("SnipLingo can send the text you select to %1 for translation.").arg(
                        sheet.destination)
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            color: theme.textSecondary
            font.pixelSize: theme.fontOriginal
            text: qsTr("Only the selected text is sent. SnipLingo does not save it to disk. "
                       + "The service's own privacy policy still applies. You can stop this at any time.")
        }
        Row {
            spacing: 8
            anchors.right: parent.right
            ActionButton {
                text: qsTr("Not now")
                onClicked: sheet.close()
            }
            ActionButton {
                text: qsTr("Allow")
                highlighted: true
                onClicked: {
                    if (sheet.controller !== null)
                        sheet.controller.grantConsent();
                    sheet.close();
                }
            }
        }
    }
}
