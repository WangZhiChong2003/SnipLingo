import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Provider output is always displayed as plain text.
Window {
    id: root
    property var resultController: null
    property bool interactive: false
    property bool automaticRequest: false
    property bool expanded: true
    property bool activatedForDismissal: false
    Theme { id: theme }

    component ActionButton: Button {
        id: button
        property bool activateOnClick: true
        implicitHeight: 40
        implicitWidth: Math.max(40, label.implicitWidth + 30)
        contentItem: Text {
            id: label
            text: button.text
            color: button.highlighted ? "white" : theme.textPrimary
            opacity: button.enabled ? 1 : 0.55
            font.pixelSize: 14
            font.bold: button.highlighted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 10
            color: button.highlighted ? (button.down ? Qt.darker(theme.accent, 1.12) : theme.accent)
                                      : button.flat && !button.hovered ? "transparent" : theme.surfaceElevated
            border.color: button.highlighted ? theme.accent
                                             : button.flat && !button.hovered ? "transparent" : theme.border
            opacity: button.enabled ? 1 : 0.6
        }
        Connections {
            target: button
            function onClicked() {
                if (button.activateOnClick)
                    root.activateForInteraction();
            }
        }
    }

    readonly property bool ready: resultController !== null
    readonly property string stateKey: ready ? resultController.stateKey : "hidden"
    readonly property bool hasResult: stateKey === "done" || stateKey === "streaming"

    function errorMessage(code) {
        switch (code) {
        case "ProviderRateLimited": return qsTr("This service is limiting requests or its quota is exhausted. Try again later or choose another engine.");
        case "ProviderAuthFailed": return qsTr("The service rejected the credentials. Check the API key and endpoint.");
        case "ProviderNotConfigured": return qsTr("Configure a translation engine and its credentials in settings.");
        case "ProviderNetwork": return qsTr("Could not connect. Check the endpoint, network and proxy settings.");
        case "ProviderTimeout": return qsTr("The service took too long to respond. Try again or choose another engine.");
        case "ProviderUnavailable": return qsTr("This service is unavailable. Try another engine or check its endpoint.");
        case "ProviderBadResponse": return qsTr("The service returned an unexpected response. Check the configured endpoint.");
        case "SelectionTooLarge": return qsTr("The text is too long for this engine. Try a shorter selection.");
        case "SettingsInvalid": return qsTr("Choose a target language and check the engine settings.");
        default: return qsTr("Translation failed. Check the engine and try again.");
        }
    }
    function activateForInteraction() {
        if (!root.visible || !root.expanded)
            return;
        root.interactive = true;
        root.requestActivate();
    }
    function expandTranslation() {
        root.expanded = true;
        Qt.callLater(root.activateForInteraction);
    }

    visible: ready ? resultController.visible : false
    // The icon stays passive. The expanded result requests focus so a click in another window
    // can dismiss it without monitoring or intercepting desktop input.
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | (interactive ? 0 : Qt.WindowDoesNotAcceptFocus)
    onVisibleChanged: {
        if (!visible) {
            activatedForDismissal = false;
            interactive = false;
        } else if (expanded) {
            Qt.callLater(root.activateForInteraction);
        }
    }
    onActiveChanged: {
        if (active) {
            activatedForDismissal = true;
        } else if (activatedForDismissal && visible && expanded && ready) {
            activatedForDismissal = false;
            resultController.dismissOnFocusLoss();
        }
    }
    color: "transparent"
    width: expanded ? 470 : 44
    // Size short translations compactly while leaving long translations scrollable.
    height: !expanded ? 44 : Math.min(460, Math.max(220, 170 + measuredText.implicitHeight
                     + (ready && resultController.originalVisible && !resultController.originalCollapsed
                        ? Math.min(originalText.implicitHeight, 60) : 0)))
    minimumWidth: 44
    minimumHeight: 44

    Text {
        id: measuredText
        visible: false
        width: 430
        text: root.hasResult && root.ready ? root.resultController.translatedText : ""
        textFormat: Text.PlainText
        font.pixelSize: 18
        wrapMode: Text.Wrap
    }

    Shortcut { sequence: "Escape"; onActivated: if (root.ready) root.resultController.dismiss() }
    Shortcut { sequence: "Ctrl+C"; onActivated: if (root.ready) root.resultController.copyResult() }
    Shortcut { sequence: "Ctrl+T"; onActivated: if (root.ready) root.resultController.toggleOriginal() }
    Shortcut { sequence: "Ctrl+R"; onActivated: if (root.ready) root.resultController.retry() }

    Rectangle {
        objectName: "selectionIcon"
        anchors.fill: parent
        visible: !root.expanded
        radius: 14
        color: theme.accent
        border.color: Qt.lighter(theme.accent, 1.15)
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Show translation")

        Text {
            anchors.centerIn: parent
            text: "文"
            color: "white"
            font.pixelSize: 21
            font.bold: true
        }
        HoverHandler {
            onHoveredChanged: if (hovered) root.expandTranslation()
        }
        TapHandler {
            onTapped: root.expandTranslation()
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.expanded
        radius: theme.radiusWindow
        color: theme.surface
        border.color: theme.border

        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: root.activateForInteraction()
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Rectangle {
                    implicitWidth: 40; implicitHeight: 40; radius: 12
                    color: theme.accentSoft
                    Text {
                        anchors.centerIn: parent
                        text: "文"
                        color: theme.accent
                        font.pixelSize: 19
                        font.bold: true
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        text: qsTr("Translation")
                        font.pixelSize: 17
                        font.bold: true
                        color: theme.textPrimary
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.ready ? root.resultController.providerLabel : ""
                        elide: Text.ElideRight
                        font.pixelSize: theme.fontMeta
                        color: theme.textSecondary
                    }
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: "✕"
                    flat: true
                    activateOnClick: false
                    Accessible.name: qsTr("Close translation")
                    onClicked: if (root.ready) root.resultController.dismiss()
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScrollView {
                    anchors.fill: parent
                    visible: root.hasResult
                    clip: true
                    TextEdit {
                        text: root.ready ? root.resultController.translatedText : ""
                        textFormat: TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        color: theme.textPrimary
                        font.pixelSize: 18
                        TapHandler { onTapped: root.activateForInteraction() }
                    }
                }
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    visible: root.stateKey === "loading"
                    Rectangle {
                        width: 9; height: 9; radius: 5
                        anchors.verticalCenter: parent.verticalCenter
                        color: theme.accent
                    }
                    Text {
                        text: qsTr("Translating…")
                        color: theme.textSecondary
                        font.pixelSize: theme.fontBody
                    }
                }
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: errorText.implicitHeight + 24
                    radius: theme.radiusControl
                    color: theme.errorSoft
                    visible: root.stateKey === "error"
                    Text {
                        id: errorText
                        anchors.fill: parent; anchors.margins: 12
                        text: root.ready ? root.errorMessage(root.resultController.errorKey) : ""
                        color: theme.error
                        font.pixelSize: theme.fontOriginal
                        wrapMode: Text.Wrap
                    }
                }
            }
            Text {
                id: originalText
                Layout.fillWidth: true
                visible: root.ready && root.resultController.originalVisible
                         && !root.resultController.originalCollapsed
                text: root.ready ? root.resultController.originalText : ""
                textFormat: Text.PlainText
                color: theme.textSecondary
                font.pixelSize: theme.fontOriginal
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    text: qsTr("Copy translation")
                    enabled: root.hasResult
                    highlighted: true
                    onClicked: if (root.ready) root.resultController.copyResult()
                }
                ActionButton {
                    text: qsTr("Retry")
                    visible: root.ready && root.resultController.canRetry
                    onClicked: if (root.ready) root.resultController.retry()
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: root.ready && root.resultController.originalCollapsed
                          ? qsTr("Show original") : qsTr("Hide original")
                    visible: root.ready && root.resultController.originalVisible
                    flat: true
                    onClicked: if (root.ready) root.resultController.toggleOriginal()
                }
            }
        }
    }
}
