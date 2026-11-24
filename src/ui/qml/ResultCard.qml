import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "LanguageNames.js" as Languages

Rectangle {
    id: card
    required property var colors
    required property var controller
    // Pinning belongs to the floating result window; the inline card in the main window never
    // offers it.
    property bool pinVisible: false
    property bool pinned: false
    // Translation and original text multiplier, and the interface zoom for overlay popups that
    // live outside the scaled window layer.
    property real textScale: 1.0
    property real zoom: 1.0
    signal interactionRequested
    signal pinRequested

    readonly property string stateKey: controller === null ? "hidden" : controller.stateKey
    readonly property bool hasResult: stateKey === "streaming" || stateKey === "done"
    readonly property bool showsOriginal: controller !== null && controller.originalVisible
                                          && !controller.originalCollapsed
    readonly property int resultFontSize: Math.round(colors.fontResult * textScale)
    // An editor's implicit width measures its longest line before wrapping.
    readonly property real preferredWidth: {
        const translatedWidth = hasResult ? translationText.implicitWidth : 0
        const textWidth = Math.max(translatedWidth, originalText.implicitWidth)
        return Math.max(440, Math.min(720, textWidth + contentLayout.anchors.margins * 2))
    }
    // Each text pane grows with its content up to half the available screen height or 320 px.
    readonly property real maxTextHeight: {
        const available = Math.min(Screen.height, Screen.desktopAvailableHeight)
        return Math.min(320, available > 0 ? available * 0.5 : 320)
    }

    // The column's implicit height does not include its anchor margins; add them back so the
    // floating window sizes itself to the content plus its padding.
    implicitHeight: contentLayout.implicitHeight + contentLayout.anchors.margins * 2
    radius: colors.radiusWindow
    color: colors.surface
    border.color: colors.border
    Keys.onEscapePressed: controller.dismiss()

    function errorMessage(code) {
        switch (code) {
        case "ProviderRateLimited": return qsTr("The service is limiting requests or its quota is exhausted. Try again later or choose another engine.");
        case "ProviderAuthFailed": return qsTr("The service rejected the credentials. Check the API key and endpoint.");
        case "ProviderNotConfigured": return qsTr("Configure a translation engine and its credentials in settings.");
        case "ProviderNetwork": return qsTr("Could not connect. Check the endpoint, network and proxy settings.");
        case "ProviderTimeout": return qsTr("The service took too long to respond. Try again or choose another engine.");
        case "ProviderUnavailable": return qsTr("This service is unavailable. Try another engine or check its endpoint.");
        case "ProviderBadResponse": return qsTr("The service returned an unexpected response. Check the configured endpoint.");
        case "SelectionTooLarge": return qsTr("The selected text is too long. Try a shorter selection.");
        case "SettingsInvalid": return qsTr("Choose a target language and check the engine settings.");
        default: return qsTr("Translation failed. Check the engine and try again.");
        }
    }

    component CardButton: AppButton {
        colors: card.colors
        implicitHeight: 34
    }

    component CardTextScroll: AppScrollView {
        required property real textHeight
        colors: card.colors
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: Math.max(40, Math.min(textHeight, card.maxTextHeight))
        contentWidth: availableWidth
        clip: true
    }

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: 14
        spacing: 7

        RowLayout {
            Layout.fillWidth: true
            Text {
                id: direction
                Layout.fillWidth: true
                text: {
                    if (card.stateKey === "hidden" || card.controller.targetLanguage.length === 0)
                        return qsTr("Translation");
                    const source = card.controller.sourceLanguage === "auto"
                                 ? qsTr("Auto") : Languages.name(card.controller.sourceLanguage);
                    return qsTr("%1 → %2").arg(source).arg(Languages.name(card.controller.targetLanguage));
                }
                textFormat: Text.PlainText
                color: card.colors.textSecondary
                font.pixelSize: card.colors.fontOriginal
                elide: Text.ElideRight

                HoverHandler { id: directionHover }
                ToolTip {
                    id: destinationTip
                    visible: directionHover.hovered && text.length > 0
                    text: card.controller === null ? "" : card.controller.providerLabel
                    delay: 600
                    width: Math.min(implicitWidth, card.width - 28)
                    scale: card.zoom
                    contentItem: Text {
                        text: destinationTip.text
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: card.colors.textPrimary
                    }
                    background: Rectangle {
                        radius: card.colors.radiusControl
                        color: card.colors.surfaceElevated
                        border.color: card.colors.border
                    }
                }
            }
            CardButton {
                objectName: "pinButton"
                visible: card.pinVisible
                iconSource: card.pinned ? "qrc:/icons/pin-filled.svg" : "qrc:/icons/pin.svg"
                flat: true
                Accessible.name: card.pinned ? qsTr("Unpin the translation")
                                             : qsTr("Keep the translation on screen")
                HoverHandler { id: pinHover }
                ToolTip {
                    id: pinTip
                    visible: pinHover.hovered
                    text: card.pinned ? qsTr("Unpin") : qsTr("Pin translation")
                    delay: 600
                    scale: card.zoom
                    contentItem: Text {
                        text: pinTip.text
                        textFormat: Text.PlainText
                        color: card.colors.textPrimary
                    }
                    background: Rectangle {
                        radius: card.colors.radiusControl
                        color: card.colors.surfaceElevated
                        border.color: card.colors.border
                    }
                }
                onClicked: {
                    card.interactionRequested();
                    card.pinRequested();
                }
            }
            WindowButton {
                kind: 2
                colors: card.colors
                onClicked: card.controller.dismiss()
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: card.colors.border }

        CardTextScroll {
            id: translationScroll
            objectName: "translationScroll"
            textHeight: translationText.implicitHeight

            TextEdit {
                id: translationText
                objectName: "translatedText"
                width: translationScroll.availableWidth
                rightPadding: 10
                topPadding: 4
                text: {
                    switch (card.stateKey) {
                    case "loading": return qsTr("Translating…");
                    case "error": return card.errorMessage(card.controller.errorKey);
                    case "done":
                    case "streaming": return card.controller.translatedText;
                    default: return "";
                    }
                }
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: card.hasResult
                activeFocusOnTab: card.hasResult
                Accessible.name: qsTr("Translated text")
                wrapMode: TextEdit.Wrap
                color: card.stateKey === "error" ? card.colors.error
                      : card.hasResult ? card.colors.textPrimary : card.colors.textSecondary
                selectionColor: card.colors.accentFill
                selectedTextColor: card.colors.accentText
                font.pixelSize: card.hasResult ? card.resultFontSize : card.colors.fontBody
                TapHandler {
                    enabled: card.hasResult
                    onPressedChanged: if (pressed) card.interactionRequested()
                }
            }
        }

        CardTextScroll {
            id: originalScroll
            objectName: "originalScroll"
            textHeight: originalText.implicitHeight
            visible: card.showsOriginal
            TextEdit {
                id: originalText
                objectName: "originalText"
                width: originalScroll.availableWidth
                rightPadding: 10
                topPadding: 4
                text: card.showsOriginal ? card.controller.originalText : ""
                textFormat: TextEdit.PlainText
                color: card.colors.textSecondary
                font.pixelSize: card.resultFontSize
                wrapMode: TextEdit.Wrap
                readOnly: true
                selectByMouse: true
                Accessible.name: qsTr("Original text")
                TapHandler { onPressedChanged: if (pressed) card.interactionRequested() }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            CardButton {
                objectName: "originalButton"
                visible: card.controller !== null && card.controller.originalVisible
                text: card.showsOriginal ? qsTr("Hide original") : qsTr("Original")
                flat: true
                onClicked: {
                    card.interactionRequested();
                    card.controller.toggleOriginal();
                }
            }
            Item { Layout.fillWidth: true }
            CardButton {
                text: qsTr("Retry")
                visible: card.controller !== null && card.controller.canRetry
                onClicked: {
                    card.interactionRequested();
                    card.controller.retry();
                }
            }
            CardButton {
                text: qsTr("Copy translation")
                visible: card.stateKey !== "error"
                enabled: card.hasResult
                highlighted: true
                onClicked: card.controller.copyResult()
            }
        }
    }

    // Streamed deltas keep the reader's scroll position; a fresh loading state resets it.
    onStateKeyChanged: {
        if (stateKey === "loading")
            translationScroll.contentItem.contentY = 0;
    }
}
