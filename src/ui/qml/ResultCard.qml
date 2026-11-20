import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "LanguageNames.js" as Languages

Rectangle {
    id: card
    required property var colors
    required property var controller
    signal interactionRequested

    readonly property string stateKey: controller === null ? "hidden" : controller.stateKey
    readonly property bool hasResult: stateKey === "streaming" || stateKey === "done"
    readonly property bool showsOriginal: controller !== null && controller.originalVisible
                                          && !controller.originalCollapsed

    implicitHeight: showsOriginal ? 276 : 204
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

    ColumnLayout {
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
                text: "×"
                flat: true
                Accessible.name: qsTr("Close translation")
                onClicked: card.controller.dismiss()
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: card.colors.border }

        AppScrollView {
            id: translationScroll
            colors: card.colors
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

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
                font.pixelSize: card.hasResult ? 18 : card.colors.fontBody
                TapHandler {
                    enabled: card.hasResult
                    onPressedChanged: if (pressed) card.interactionRequested()
                }
            }
        }

        AppScrollView {
            id: originalScroll
            colors: card.colors
            Layout.fillWidth: true
            Layout.preferredHeight: 65
            visible: card.showsOriginal
            contentWidth: availableWidth
            clip: true
            TextEdit {
                width: originalScroll.availableWidth
                rightPadding: 10
                text: card.showsOriginal ? card.controller.originalText : ""
                textFormat: TextEdit.PlainText
                color: card.colors.textSecondary
                font.pixelSize: card.colors.fontOriginal
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
