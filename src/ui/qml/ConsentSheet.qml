import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Consent is specific to the current destination; cancelling never grants it.
Dialog {
    id: sheet

    property var controller: null
    property string destination: ""
    property bool stale: false

    Theme { id: theme }

    modal: true
    implicitWidth: 500
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape
    padding: 24
    onOpened: cancelButton.forceActiveFocus()
    Overlay.modal: Rectangle { radius: 10; color: theme.overlay }

    background: Rectangle {
        radius: 20
        color: theme.surface
        border.color: theme.border
    }

    contentItem: ColumnLayout {
        spacing: 18

        AppScrollView {
            id: details
            objectName: "consentDetails"
            colors: theme
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: detailsColumn.implicitHeight
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: detailsColumn
                width: details.availableWidth
                spacing: 18

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: 44; implicitHeight: 44; radius: 14
                        color: theme.accentSoft
                        Text {
                            anchors.centerIn: parent
                            text: "↗"
                            color: theme.accent
                            font.pixelSize: 21
                            font.bold: true
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: sheet.stale ? qsTr("Confirm the new destination")
                                              : qsTr("Translate selections automatically?")
                            color: theme.textPrimary
                            font.pixelSize: 19
                            font.bold: true
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: sheet.stale
                          ? qsTr("The destination has changed. After you select text, SnipLingo will send it to this service automatically.")
                          : qsTr("After you select text, SnipLingo will send it to this service automatically, even if you do not open the translation.")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontBody
                    wrapMode: Text.Wrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: destinationColumn.implicitHeight + 28
                    radius: 12
                    color: theme.surfaceElevated
                    border.color: theme.border
                    ColumnLayout {
                        id: destinationColumn
                        anchors.fill: parent; anchors.margins: 14
                        spacing: 4
                        Text {
                            text: qsTr("Destination")
                            color: theme.textSecondary
                            font.pixelSize: theme.fontMeta
                        }
                        Text {
                            Layout.fillWidth: true
                            text: sheet.destination
                            textFormat: Text.PlainText
                            color: theme.textPrimary
                            font.pixelSize: theme.fontOriginal
                            font.bold: true
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("The service’s privacy policy and charges may apply. You can pause automatic translation or withdraw permission at any time.")
                    color: theme.textSecondary
                    font.pixelSize: theme.fontOriginal
                    wrapMode: Text.Wrap
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Item { Layout.fillWidth: true }
            AppButton {
                id: cancelButton
                objectName: "cancelConsentButton"
                colors: theme
                minimumButtonWidth: 104
                text: qsTr("Not now")
                onClicked: sheet.close()
            }
            AppButton {
                objectName: "allowConsentButton"
                colors: theme
                minimumButtonWidth: 104
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
