import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    property var settingsController: null
    property bool showAddEngine: false
    Theme { id: theme }

    component ActionButton: Button {
        id: button
        implicitHeight: 40
        implicitWidth: Math.max(40, buttonLabel.implicitWidth + 30)
        contentItem: Text {
            id: buttonLabel
            text: button.text
            color: button.highlighted ? "white" : theme.textPrimary
            opacity: button.enabled ? 1 : 0.7
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
            opacity: button.enabled ? 1 : 0.58
        }
    }
    component ToggleSwitch: Switch {
        id: control
        implicitHeight: 36
        leftPadding: 0
        spacing: 10
        indicator: Rectangle {
            x: control.leftPadding
            y: (control.height - height) / 2
            implicitWidth: 42; implicitHeight: 24
            radius: 12
            color: control.checked ? theme.accent : theme.toggleOff
            opacity: control.enabled ? 1 : 0.5
            Rectangle {
                width: 18; height: 18; radius: 9
                x: control.checked ? 21 : 3
                anchors.verticalCenter: parent.verticalCenter
                color: "white"
            }
        }
        contentItem: Text {
            text: control.text
            leftPadding: control.indicator.width + control.spacing
            verticalAlignment: Text.AlignVCenter
            color: theme.textPrimary
            opacity: control.enabled ? 1 : 0.5
            font.pixelSize: 14
        }
    }
    component EngineRadio: RadioButton {
        id: radio
        implicitWidth: 28; implicitHeight: 28
        indicator: Rectangle {
            anchors.centerIn: parent
            width: 20; height: 20; radius: 10
            color: theme.surface
            border.width: radio.checked ? 2 : 1
            border.color: radio.checked ? theme.accent : theme.toggleOff
            Rectangle {
                anchors.centerIn: parent
                width: 10; height: 10; radius: 5
                color: theme.accent
                visible: radio.checked
            }
        }
    }
    component InputField: TextField {
        implicitHeight: 38
        leftPadding: 11
        rightPadding: 11
        color: theme.textPrimary
        font.pixelSize: theme.fontOriginal
        background: Rectangle { radius: 8; color: theme.surface; border.color: theme.border }
    }
    component LanguageCombo: ComboBox {
        id: combo
        implicitHeight: 38
        leftPadding: 11
        rightPadding: 30
        contentItem: Text {
            text: combo.displayText
            color: theme.textPrimary
            font.pixelSize: theme.fontOriginal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: combo.width - width - 12
            y: (combo.height - height) / 2
            text: "⌄"
            color: theme.textSecondary
            font.pixelSize: 16
        }
        background: Rectangle { radius: 8; color: theme.surface; border.color: theme.border }
    }

    readonly property bool ready: settingsController !== null
    readonly property var providers: ready ? settingsController.providers : []
    readonly property var activeProvider: {
        for (let i = 0; i < providers.length; ++i) {
            if (providers[i].active)
                return providers[i];
        }
        return null;
    }
    readonly property bool needsConsent: ready && activeProvider !== null
                                         && !activeProvider.isLocal
                                         && !settingsController.consentGranted

    function languageName(code) {
        switch (code) {
        case "auto": return qsTr("Detect automatically");
        case "en": return qsTr("English");
        case "zh-Hans": return qsTr("Chinese (Simplified)");
        case "zh-Hant": return qsTr("Chinese (Traditional)");
        case "ja": return qsTr("Japanese");
        case "ko": return qsTr("Korean");
        case "de": return qsTr("German");
        case "fr": return qsTr("French");
        case "es": return qsTr("Spanish");
        case "ru": return qsTr("Russian");
        case "pt": return qsTr("Portuguese");
        case "it": return qsTr("Italian");
        case "ar": return qsTr("Arabic");
        default: return code;
        }
    }
    function showOutcome(outcome) {
        errorBanner.message = outcome.detail || outcome.error;
        errorBanner.visible = !outcome.ok;
        if (!outcome.ok)
            scroll.contentItem.contentY = 0;
    }

    title: qsTr("SnipLingo")
    width: 820
    height: 820
    minimumWidth: 620
    minimumHeight: 480
    color: theme.background
    onClosing: Qt.quit()

    Connections {
        target: root.settingsController
        function onConsentRequired(destinationLabel, staleConsent) {
            consentSheet.destination = destinationLabel;
            consentSheet.stale = staleConsent;
            root.show();
            root.raise();
            consentSheet.open();
        }
        function onFailed(code, detail) {
            errorBanner.message = detail || code;
            errorBanner.visible = true;
            scroll.contentItem.contentY = 0;
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 16

            Item { implicitHeight: 8 }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                spacing: 14
                Rectangle {
                    implicitWidth: 48; implicitHeight: 48; radius: 14
                    color: theme.accent
                    Text {
                        anchors.centerIn: parent
                        text: "文"
                        color: "white"
                        font.pixelSize: 23
                        font.bold: true
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: qsTr("SnipLingo")
                        color: theme.textPrimary
                        font.pixelSize: 24
                        font.bold: true
                    }
                    Text {
                        text: qsTr("A little help across languages")
                        color: theme.textSecondary
                        font.pixelSize: theme.fontOriginal
                    }
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "hideWindowButton"
                    text: qsTr("Hide")
                    onClicked: root.hide()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Keep SnipLingo running in the background")
                }
                Rectangle {
                    radius: 12
                    color: theme.accentSoft
                    implicitWidth: statusText.implicitWidth + 20
                    implicitHeight: 28
                    Text {
                        id: statusText
                        anchors.centerIn: parent
                        text: {
                            if (!root.ready || !root.settingsController.automaticEnabled) return qsTr("Manual mode");
                            if (root.settingsController.paused) return qsTr("Paused");
                            return root.settingsController.observationActive ? qsTr("Listening") : qsTr("Unavailable");
                        }
                        color: theme.accent
                        font.pixelSize: theme.fontMeta
                        font.bold: true
                    }
                }
            }

            Rectangle {
                id: errorBanner
                property string message: ""
                visible: false
                Layout.fillWidth: true
                Layout.leftMargin: 24; Layout.rightMargin: 24
                implicitHeight: bannerText.implicitHeight + 24
                radius: theme.radiusControl
                color: theme.errorSoft
                Text {
                    id: bannerText
                    anchors.fill: parent; anchors.margins: 12
                    text: errorBanner.message
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: theme.error
                    font.pixelSize: theme.fontOriginal
                }
            }

            // Explicit text entry works even when the desktop cannot expose selection events.
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24; Layout.rightMargin: 24
                implicitHeight: translateColumn.implicitHeight + 2 * theme.padding
                radius: theme.radiusWindow
                color: theme.surface
                border.color: theme.border
                ColumnLayout {
                    id: translateColumn
                    anchors.fill: parent; anchors.margins: theme.padding
                    spacing: 12
                    Text { text: qsTr("Translate text"); color: theme.textPrimary; font.pixelSize: 18; font.bold: true }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Type or paste text here. It is sent only when you choose Translate.")
                        color: theme.textSecondary; font.pixelSize: theme.fontOriginal; wrapMode: Text.Wrap
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 104
                        clip: true
                        TextArea {
                            id: inputText
                            objectName: "translationInput"
                            placeholderText: qsTr("Enter text to translate…")
                            wrapMode: TextArea.Wrap
                            selectByMouse: true
                            font.pixelSize: theme.fontBody
                            leftPadding: 12; rightPadding: 12; topPadding: 10
                            background: Rectangle { radius: 8; color: theme.surfaceElevated; border.color: theme.border }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        LanguageCombo {
                            id: sourceCombo
                            objectName: "sourceLanguageCombo"
                            Layout.fillWidth: true
                            model: root.ready ? ["auto"].concat(root.settingsController.targetLanguageCodes) : []
                            currentIndex: root.ready ? model.indexOf(root.settingsController.sourceLanguage) : -1
                            displayText: currentIndex < 0 ? qsTr("From") : root.languageName(currentValue)
                            delegate: ItemDelegate {
                                required property string modelData
                                width: ListView.view ? ListView.view.width : implicitWidth
                                text: root.languageName(modelData)
                            }
                            onActivated: if (root.ready) root.settingsController.setSourceLanguage(currentValue)
                        }
                        Text { text: "→"; color: theme.textSecondary; font.pixelSize: 18 }
                        LanguageCombo {
                            id: targetCombo
                            objectName: "targetLanguageCombo"
                            Layout.fillWidth: true
                            model: root.ready ? root.settingsController.targetLanguageCodes : []
                            currentIndex: root.ready ? model.indexOf(root.settingsController.targetLanguage) : -1
                            displayText: currentIndex < 0 ? qsTr("Choose target language") : root.languageName(currentValue)
                            delegate: ItemDelegate {
                                required property string modelData
                                width: ListView.view ? ListView.view.width : implicitWidth
                                text: root.languageName(modelData)
                            }
                            onActivated: if (root.ready) root.settingsController.setTargetLanguage(currentValue)
                        }
                        ActionButton {
                            objectName: "translateButton"
                            text: qsTr("Translate")
                            highlighted: true
                            enabled: root.ready && inputText.text.trim().length > 0
                            onClicked: root.showOutcome(root.settingsController.translateText(inputText.text))
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24; Layout.rightMargin: 24
                implicitHeight: engineColumn.implicitHeight + 2 * theme.padding
                radius: theme.radiusWindow
                color: theme.surface
                border.color: theme.border
                ColumnLayout {
                    id: engineColumn
                    anchors.fill: parent; anchors.margins: theme.padding
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: qsTr("Translation engines"); color: theme.textPrimary; font.pixelSize: 18; font.bold: true }
                            Text { text: qsTr("Choose where your text is translated."); color: theme.textSecondary; font.pixelSize: theme.fontOriginal }
                        }
                        Item { Layout.fillWidth: true }
                        ActionButton {
                            objectName: "newEngineButton"
                            text: root.showAddEngine ? qsTr("Cancel") : qsTr("Add engine")
                            onClicked: root.showAddEngine = !root.showAddEngine
                        }
                    }
                    Text {
                        visible: root.providers.length === 0
                        text: qsTr("No engine configured yet. Add one to get started.")
                        color: theme.textSecondary; font.pixelSize: theme.fontOriginal
                    }
                    ListView {
                        objectName: "engineList"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 210)
                        clip: true
                        spacing: 5
                        model: root.providers
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: 64
                            radius: theme.radiusControl
                            color: modelData.active ? theme.accentSoft : theme.surfaceElevated
                            TapHandler { onTapped: if (root.ready) root.settingsController.setActiveProvider(modelData.id) }
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                                EngineRadio {
                                    checked: modelData.active
                                    onClicked: if (root.ready) root.settingsController.setActiveProvider(modelData.id)
                                    Accessible.name: modelData.label
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        color: theme.textPrimary; font.pixelSize: theme.fontBody; font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.isLocal ? qsTr("On this device · %1").arg(modelData.host) : modelData.host
                                        color: theme.textSecondary; font.pixelSize: theme.fontMeta
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    visible: modelData.needsApiKey && !modelData.hasApiKey
                                    text: qsTr("Key needed"); color: theme.error; font.pixelSize: theme.fontMeta
                                }
                                ActionButton {
                                    text: "✕"; flat: true
                                    Accessible.name: qsTr("Remove engine")
                                    onClicked: if (root.ready) root.settingsController.removeProvider(modelData.id)
                                }
                            }
                        }
                    }
                    Rectangle {
                        id: addEngineBox
                        objectName: "addEnginePanel"
                        visible: root.ready && (root.showAddEngine || root.providers.length === 0)
                        Layout.fillWidth: true
                        implicitHeight: addColumn.implicitHeight + 28
                        radius: theme.radiusControl
                        color: theme.surfaceElevated
                        ColumnLayout {
                            id: addColumn
                            anchors.fill: parent; anchors.margins: 14
                            spacing: 8
                            Text { text: qsTr("Quick setup"); color: theme.textPrimary; font.bold: true; font.pixelSize: theme.fontBody }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Public services may limit requests. LibreTranslate requires a running instance.")
                                color: theme.textSecondary; font.pixelSize: theme.fontOriginal; wrapMode: Text.Wrap
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                Repeater {
                                    model: [
                                        { type: "google-free", name: qsTr("Google · unofficial") },
                                        { type: "mymemory", name: "MyMemory" },
                                        { type: "libretranslate", name: "LibreTranslate" }
                                    ]
                                    ActionButton {
                                        required property var modelData
                                        text: modelData.name
                                        onClicked: {
                                            const outcome = root.settingsController.addPresetEngine(modelData.type);
                                            root.showOutcome(outcome);
                                            if (outcome.ok) {
                                                root.settingsController.setActiveProvider(outcome.id);
                                                root.showAddEngine = false;
                                            }
                                        }
                                    }
                                }
                            }
                            Text { text: qsTr("Or add an OpenAI-compatible service"); color: theme.textPrimary; font.pixelSize: theme.fontOriginal; font.bold: true }
                            InputField { id: addBaseUrl; Layout.fillWidth: true; placeholderText: qsTr("Base URL, e.g. http://127.0.0.1:11434/v1") }
                            RowLayout {
                                Layout.fillWidth: true
                                InputField { id: addModel; Layout.fillWidth: true; placeholderText: qsTr("Model name") }
                                InputField { id: addLabel; Layout.fillWidth: true; placeholderText: qsTr("Display name (optional)") }
                                ActionButton {
                                    text: qsTr("Add")
                                    enabled: addBaseUrl.text.length > 0 && addModel.text.length > 0
                                    onClicked: {
                                        const outcome = root.settingsController.addProvider(addLabel.text, addBaseUrl.text, addModel.text);
                                        root.showOutcome(outcome);
                                        if (outcome.ok) {
                                            root.settingsController.setActiveProvider(outcome.id);
                                            addBaseUrl.clear(); addModel.clear(); addLabel.clear();
                                            root.showAddEngine = false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        visible: root.activeProvider !== null
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: qsTr("Selected engine settings"); color: theme.textPrimary; font.pixelSize: theme.fontOriginal; font.bold: true }
                        RowLayout {
                            Layout.fillWidth: true
                            InputField {
                                id: selectedUrl
                                objectName: "engineUrlField"
                                Layout.fillWidth: true
                                text: root.activeProvider !== null ? root.activeProvider.baseUrl : ""
                                placeholderText: qsTr("Service URL")
                            }
                            ActionButton {
                                text: qsTr("Save URL")
                                onClicked: root.showOutcome(root.settingsController.updateProvider(root.activeProvider.id, { baseUrl: selectedUrl.text }))
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root.activeProvider !== null && root.activeProvider.type === "libretranslate"
                            text: qsTr("Start LibreTranslate locally or enter the URL of an instance you trust. Some instances require an API key.")
                            color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: root.activeProvider !== null && root.activeProvider.supportsApiKey
                            InputField {
                                id: keyField
                                Layout.fillWidth: true
                                placeholderText: qsTr("API key (if required)")
                                echoMode: TextInput.Password
                            }
                            ActionButton {
                                text: qsTr("Save key")
                                enabled: keyField.text.length > 0
                                onClicked: {
                                    const outcome = root.settingsController.setApiKey(root.activeProvider.id, keyField.text);
                                    root.showOutcome(outcome);
                                    if (outcome.ok) keyField.clear();
                                }
                            }
                            ActionButton {
                                text: qsTr("Remove key")
                                visible: root.activeProvider !== null && root.activeProvider.hasApiKey
                                onClicked: root.settingsController.clearApiKey(root.activeProvider.id)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root.activeProvider !== null && root.activeProvider.supportsApiKey
                            text: root.ready ? root.settingsController.credentialReason : ""
                            color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24; Layout.rightMargin: 24
                implicitHeight: autoColumn.implicitHeight + 2 * theme.padding
                radius: theme.radiusWindow; color: theme.surface; border.color: theme.border
                ColumnLayout {
                    id: autoColumn
                    anchors.fill: parent; anchors.margins: theme.padding
                    spacing: 8
                    Text { text: qsTr("Automatic selection"); color: theme.textPrimary; font.pixelSize: 18; font.bold: true }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Translate selections from supported applications. Availability depends on your desktop session.")
                        color: theme.textSecondary; font.pixelSize: theme.fontOriginal; wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: {
                            if (!root.ready || root.settingsController.selectionBackendId.length === 0)
                                return qsTr("No selection listener is available in this session.");
                            if (root.settingsController.selectionBackendId === "atspi")
                                return qsTr("Listening uses accessibility events. Some browsers and editors may not expose selections.");
                            return qsTr("Listening uses the X11 primary selection; support varies by application.");
                        }
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Hide keeps the app running; launch SnipLingo again to reopen it. Closing this window quits the app.")
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ToggleSwitch {
                            text: qsTr("Translate selected text")
                            checked: root.ready && root.settingsController.automaticEnabled
                            onToggled: if (root.ready) root.settingsController.setAutomaticEnabled(checked)
                        }
                        Item { Layout.fillWidth: true }
                        ToggleSwitch {
                            text: qsTr("Pause")
                            checked: root.ready && root.settingsController.paused
                            enabled: root.ready && root.settingsController.automaticEnabled
                            onToggled: if (root.ready) root.settingsController.setPaused(checked)
                        }
                    }
                    ToggleSwitch {
                        objectName: "selectionImmediateSwitch"
                        text: qsTr("Show translation immediately")
                        checked: root.ready && root.settingsController.showResultImmediately
                        onToggled: if (root.ready) root.settingsController.setShowResultImmediately(checked)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("When off, an icon appears near the selection; hover over it to open the translation. Translation starts when text is selected.")
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("The expanded translation requests focus and closes when you switch to another window.")
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        objectName: "waylandPositionWarning"
                        visible: root.ready && root.settingsController.waylandSession
                        text: qsTr("On Wayland, other applications may not expose the current mouse position. The icon can appear away from the selection.")
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("Selection source")
                            color: theme.textPrimary; font.pixelSize: theme.fontOriginal
                        }
                        Item { Layout.fillWidth: true }
                        LanguageCombo {
                            id: backendCombo
                            objectName: "selectionBackendCombo"
                            Layout.preferredWidth: 250
                            model: [
                                { code: "auto", name: qsTr("Automatic") },
                                { code: "atspi", name: qsTr("Accessibility events") },
                                { code: "x11-primary", name: qsTr("X11 / XWayland primary") }
                            ]
                            textRole: "name"; valueRole: "code"
                            currentIndex: root.ready
                                          ? ["auto", "atspi", "x11-primary"].indexOf(root.settingsController.selectionBackendChoice)
                                          : 0
                            onActivated: if (root.ready) root.settingsController.setSelectionBackend(currentValue)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.ready && root.settingsController.selectionRestartRequired
                        text: qsTr("Restart SnipLingo to apply the new selection source.")
                        color: theme.accent; font.pixelSize: theme.fontOriginal; wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.ready && root.settingsController.selectionBackendChoice === "x11-primary"
                        text: qsTr("XWayland can observe PRIMARY selections shared by the compositor; support varies by application.")
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: {
                            if (!root.ready || root.activeProvider === null) return qsTr("Choose an engine first.");
                            if (root.activeProvider.isLocal) return qsTr("The selected engine is on this device.");
                            if (root.settingsController.consentGranted) return qsTr("Automatic sending to %1 is allowed.").arg(root.settingsController.consentDestination);
                            return qsTr("Automatic sending to %1 needs your permission.").arg(root.settingsController.consentDestination);
                        }
                        color: theme.textSecondary; font.pixelSize: theme.fontOriginal; wrapMode: Text.Wrap
                    }
                    ActionButton {
                        visible: root.activeProvider !== null && !root.activeProvider.isLocal
                        text: root.ready && root.settingsController.consentGranted ? qsTr("Stop automatic sending") : qsTr("Allow automatic sending")
                        onClicked: {
                            if (root.settingsController.consentGranted) root.settingsController.revokeConsent();
                            else consentSheet.open();
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24; Layout.rightMargin: 24
                implicitHeight: proxyColumn.implicitHeight + 2 * theme.padding
                radius: theme.radiusWindow; color: theme.surface; border.color: theme.border
                ColumnLayout {
                    id: proxyColumn
                    anchors.fill: parent; anchors.margins: theme.padding
                    spacing: 8
                    Text { text: qsTr("Network"); color: theme.textPrimary; font.pixelSize: 18; font.bold: true }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Local engines connect directly. Remote connections use TLS 1.2 and the selected proxy.")
                        color: theme.textSecondary; font.pixelSize: theme.fontOriginal; wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        LanguageCombo {
                            id: proxyModeBox
                            objectName: "proxyModeBox"
                            Layout.preferredWidth: 140
                            model: [{mode: "system", name: qsTr("System")}, {mode: "manual", name: qsTr("Manual")}, {mode: "none", name: qsTr("Direct")}]
                            textRole: "name"; valueRole: "mode"
                            currentIndex: root.ready ? Math.max(0, ["system", "manual", "none"].indexOf(root.settingsController.proxyMode)) : 0
                        }
                        InputField {
                            id: proxyUrlField
                            objectName: "proxyUrlField"
                            Layout.fillWidth: true
                            placeholderText: qsTr("http://127.0.0.1:7890")
                            text: root.ready ? root.settingsController.proxyUrl : ""
                            visible: proxyModeBox.currentValue === "manual"
                        }
                        Item { Layout.fillWidth: true; visible: !proxyUrlField.visible }
                        ActionButton {
                            text: qsTr("Apply")
                            onClicked: root.showOutcome(root.settingsController.setProxy(proxyModeBox.currentValue, proxyUrlField.text))
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.ready ? qsTr("In use: %1").arg(root.settingsController.proxyEffective) : ""
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                }
            }
            Item { implicitHeight: 12 }
        }
    }

    ConsentSheet {
        id: consentSheet
        objectName: "consentDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, root.width - 48)
        height: 270
        controller: root.settingsController
        destination: root.ready ? root.settingsController.consentDestination : ""
    }
}
