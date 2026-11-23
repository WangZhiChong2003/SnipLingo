import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import "LanguageNames.js" as Languages

Window {
    id: root
    property var settingsController: null
    property var resultController: null
    property bool showAddEngine: false
    property bool showPreferences: false
    property string settingsSection: "engines"
    property bool showResultInline: false
    property bool automaticPopupAvailable: true
    Theme { id: theme }

    component ActionButton: AppButton { colors: theme }
    component ToggleSwitch: Basic.Switch {
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
            border.width: control.visualFocus ? 2 : 0
            border.color: theme.accent
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
    component EngineRadio: Basic.RadioButton {
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
    component InputField: AppTextField { colors: theme }
    component LanguageCombo: AppComboBox { colors: theme; zoom: root.uiScale }
    component ResizeHandle: MouseArea {
        required property int edges
        acceptedButtons: Qt.LeftButton
        enabled: root.visibility !== Window.Maximized
        onPressed: root.startSystemResize(edges)
    }

    readonly property bool ready: settingsController !== null
    // A stubbed controller in tests may not expose the scale properties; fall back to 1.0.
    readonly property real uiScale: {
        const value = ready ? settingsController.uiScale : undefined
        return value === undefined ? 1.0 : value
    }
    readonly property real textScale: {
        const value = ready ? settingsController.fontScale : undefined
        return value === undefined ? 1.0 : value
    }

    // The interface choosers offer fixed steps; map a stored value onto the closest listed option.
    function nearestListedIndex(value, listed) {
        let best = 0
        for (let i = 1; i < listed.length; ++i) {
            if (Math.abs(listed[i] - value) < Math.abs(listed[best] - value))
                best = i
        }
        return best
    }
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
    readonly property bool selectionAvailable: ready && settingsController.selectionBackendId.length > 0
    readonly property bool selectionEnabled: ready && settingsController.automaticEnabled
    readonly property bool engineNeedsSetup: activeProvider === null
                                            || (activeProvider.needsApiKey && !activeProvider.hasApiKey)
    readonly property string selectionTestState: ready ? settingsController.selectionTestState : ""
    readonly property bool inlineResultVisible: showResultInline && resultController !== null
                                               && resultController.visible

    function selectionStatus() {
        if (!selectionAvailable) return qsTr("Unavailable");
        if (!selectionEnabled) return qsTr("Off");
        if (settingsController.paused) return qsTr("Paused");
        if (!settingsController.observationActive) return qsTr("Unavailable");
        if (engineNeedsSetup || !settingsController.targetLanguage.length)
            return qsTr("Setup needed");
        if (needsConsent) return qsTr("Permission needed");
        return qsTr("On");
    }

    function selectionHint() {
        if (!selectionAvailable)
            return qsTr("This desktop session cannot expose selections. You can paste text below.");
        if (activeProvider === null)
            return qsTr("Choose a translation engine to get started.");
        if (engineNeedsSetup)
            return qsTr("Add an API key for the selected engine in Settings.");
        if (!settingsController.targetLanguage.length)
            return qsTr("Choose a target language below.");
        if (!selectionEnabled)
            return qsTr("Enable translation for text you select in supported apps.");
        if (settingsController.paused)
            return qsTr("After resuming, translation starts with the next selection.");
        if (needsConsent)
            return qsTr("Allow automatic sending to translate your selections.");
        if (settingsController.popupRestartRequired)
            return qsTr("Restart SnipLingo to apply the floating result mode.");
        if (!automaticPopupAvailable || inlineResultVisible)
            return qsTr("Select text in another app, then return here to see the translation.");
        if (settingsController.showResultImmediately)
            return qsTr("Select text in a supported app to see its translation.");
        return qsTr("Select text in a supported app, then hover over or click Translate next to it.");
    }

    function selectionTestMessage() {
        switch (selectionTestState) {
        case "waiting": return qsTr("Within 10 seconds, select a few words in the app you want to test. This test runs on your device.");
        case "readable": return qsTr("SnipLingo could read that selection. Test the engine separately in Settings.");
        case "timed-out": return qsTr("SnipLingo could not read a selection in time. Try again or paste the text below.");
        case "unavailable": return qsTr("Selection access is unavailable. Check the selection source in Settings.");
        default: return "";
        }
    }

    function openPreferences(open) {
        if (open && selectionTestState === "waiting")
            settingsController.cancelSelectionTest();
        showPreferences = open;
        scroll.contentItem.contentY = 0;
    }

    function openSettingsSection(section) {
        settingsSection = section;
        openPreferences(true);
    }

    function selectNewEngine(outcome) {
        showOutcome(outcome);
        if (outcome.ok) {
            showAddEngine = false;
            if (settingsController.activeProviderId !== outcome.id)
                settingsController.setActiveProvider(outcome.id);
        }
    }

    function showOutcome(outcome) {
        errorBanner.message = outcome.detail || outcome.error || "";
        errorBanner.visible = !outcome.ok;
        if (!outcome.ok)
            scroll.contentItem.contentY = 0;
    }

    function toggleMaximized() {
        if (visibility === Window.Maximized)
            showNormal();
        else
            showMaximized();
    }

    title: qsTr("SnipLingo")
    width: Math.round(870 * uiScale)
    height: Math.round(650 * uiScale)
    minimumWidth: Math.round(620 * uiScale)
    minimumHeight: Math.round(480 * uiScale)
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
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

    Rectangle {
        id: windowFrame
        objectName: "windowFrame"
        // Lay the interface out in logical pixels and scale it up to the window: the frameless
        // chrome, every control and all text zoom together.
        width: root.width / root.uiScale
        height: root.height / root.uiScale
        transform: Scale { xScale: root.uiScale; yScale: root.uiScale }
        radius: 10
        color: theme.background
        border.width: 1
        border.color: theme.border

        ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        Rectangle {
            id: titleBar
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onPressed: root.startSystemMove()
                onDoubleClicked: root.toggleMaximized()
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 8
                spacing: 10

                Image {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    source: "qrc:/branding/sniplingo-icon.png"
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("SnipLingo")
                    color: theme.textPrimary
                    font.pixelSize: 18
                    font.bold: true
                    elide: Text.ElideRight
                }
                ActionButton {
                    text: qsTr("Translate")
                    flat: root.showPreferences
                    highlighted: !root.showPreferences
                    onClicked: root.openPreferences(false)
                }
                ActionButton {
                    objectName: "preferencesButton"
                    text: qsTr("Settings")
                    highlighted: root.showPreferences
                    onClicked: root.openPreferences(true)
                }
                ActionButton {
                    id: hideButton
                    objectName: "hideWindowButton"
                    text: qsTr("Run in background")
                    flat: true
                    onClicked: root.hide()
                    ToolTip {
                        id: hideTip
                        visible: hideButton.hovered
                        delay: 500
                        text: qsTr("Reopen SnipLingo from the system tray or the application menu.")
                        padding: 10
                        contentItem: Text {
                            text: hideTip.text
                            color: theme.textPrimary
                            font.pixelSize: theme.fontMeta
                        }
                        background: Rectangle {
                            radius: 9
                            color: theme.surface
                            border.color: theme.border
                        }
                    }
                }
                Rectangle {
                    Layout.leftMargin: 2
                    Layout.rightMargin: 2
                    implicitWidth: 1
                    implicitHeight: 22
                    color: theme.border
                }
                WindowButton {
                    objectName: "minimizeButton"
                    colors: theme
                    kind: 0
                    onClicked: root.showMinimized()
                }
                WindowButton {
                    objectName: "maximizeButton"
                    colors: theme
                    kind: 1
                    maximized: root.visibility === Window.Maximized
                    onClicked: root.toggleMaximized()
                }
                WindowButton {
                    objectName: "closeButton"
                    colors: theme
                    kind: 2
                    onClicked: root.close()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: theme.border
            }
        }

        RowLayout {
            visible: root.showPreferences
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 12
            spacing: 8
            Repeater {
                model: [
                    { section: "engines", label: qsTr("Engines") },
                    { section: "selection", label: qsTr("Selection") },
                    { section: "interface", label: qsTr("Interface") },
                    { section: "network", label: qsTr("Network") },
                    { section: "about", label: qsTr("About") }
                ]
                ActionButton {
                    required property var modelData
                    objectName: "settingsSection-" + modelData.section
                    text: modelData.label
                    highlighted: root.settingsSection === modelData.section
                    flat: !highlighted
                    onClicked: root.openSettingsSection(modelData.section)
                }
            }
            Item { Layout.fillWidth: true }
        }

        AppScrollView {
        id: scroll
        objectName: "settingsScrollView"
        colors: theme
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.rightMargin: 8
        Layout.bottomMargin: 8
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 16

            Item { implicitHeight: 8 }

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

            Rectangle {
                visible: !root.showPreferences
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
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Translate text")
                            color: theme.textPrimary
                            font.pixelSize: 22
                            font.bold: true
                        }
                        ActionButton {
                            objectName: "homeEngineButton"
                            text: root.activeProvider === null ? qsTr("Choose engine") : root.activeProvider.label
                            Accessible.name: qsTr("Configure translation engine")
                            Layout.maximumWidth: 230
                            flat: !root.engineNeedsSetup
                            highlighted: root.engineNeedsSetup
                            onClicked: root.openSettingsSection("engines")
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        LanguageCombo {
                            id: sourceCombo
                            objectName: "sourceLanguageCombo"
                            Accessible.name: qsTr("Source language")
                            Layout.fillWidth: true
                            model: root.ready ? ["auto"].concat(root.settingsController.targetLanguageCodes) : []
                            formatOption: Languages.name
                            currentIndex: root.ready ? model.indexOf(root.settingsController.sourceLanguage) : -1
                            displayText: currentIndex < 0 ? qsTr("From") : Languages.name(currentValue)
                            onActivated: if (root.ready) root.settingsController.setSourceLanguage(currentValue)
                        }
                        Text { text: "→"; color: theme.textSecondary; font.pixelSize: 18 }
                        LanguageCombo {
                            id: targetCombo
                            objectName: "targetLanguageCombo"
                            Accessible.name: qsTr("Target language")
                            Layout.fillWidth: true
                            model: root.ready ? root.settingsController.targetLanguageCodes : []
                            formatOption: Languages.name
                            currentIndex: root.ready ? model.indexOf(root.settingsController.targetLanguage) : -1
                            displayText: currentIndex < 0 ? qsTr("Choose target language") : Languages.name(currentValue)
                            onActivated: if (root.ready) root.settingsController.setTargetLanguage(currentValue)
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= 790 ? 2 : 1
                        columnSpacing: 12
                        rowSpacing: 10

                        AppScrollView {
                            colors: theme
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            Layout.preferredHeight: parent.columns === 2 ? outputPane.implicitHeight : 150
                            clip: true
                            Basic.TextArea {
                                id: inputText
                                objectName: "translationInput"
                                Accessible.name: qsTr("Source text")
                                placeholderText: qsTr("Type or paste text…")
                                wrapMode: TextArea.Wrap
                                selectByMouse: true
                                color: theme.textPrimary
                                placeholderTextColor: theme.textSecondary
                                selectionColor: theme.accentFill
                                selectedTextColor: theme.accentText
                                font.pixelSize: theme.fontBody
                                leftPadding: 12; rightPadding: 12; topPadding: 12
                                background: Rectangle {
                                    radius: theme.radiusControl
                                    color: theme.surface
                                    border.width: inputText.activeFocus ? 2 : 1
                                    border.color: inputText.activeFocus ? theme.accent : theme.border
                                }
                            }
                        }
                        Item {
                            id: outputPane
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            implicitHeight: root.inlineResultVisible ? inlineCard.implicitHeight
                                          : (parent.columns === 2 ? 204 : 74)
                            Rectangle {
                                anchors.fill: parent
                                visible: !root.inlineResultVisible
                                color: theme.surfaceElevated
                                border.color: theme.border
                                radius: theme.radiusControl
                                Text {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.margins: 12
                                    text: qsTr("Translation appears here")
                                    color: theme.textSecondary
                                    font.pixelSize: theme.fontBody
                                }
                            }
                            ResultCard {
                                id: inlineCard
                                objectName: "inlineResultCard"
                                anchors.fill: parent
                                visible: root.inlineResultVisible
                                colors: theme
                                controller: root.resultController
                                textScale: root.textScale
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: {
                                if (root.activeProvider === null)
                                    return qsTr("Choose an engine to start translating.");
                                if (root.engineNeedsSetup)
                                    return qsTr("Add your API key in engine settings.");
                                if (root.activeProvider.isLocal)
                                    return qsTr("Local service");
                                return qsTr("Text is sent to %1 when you translate.").arg(root.activeProvider.host);
                            }
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: theme.textSecondary
                            font.pixelSize: theme.fontOriginal
                        }
                        ActionButton {
                            id: translateButton
                            objectName: "translateButton"
                            text: qsTr("Translate")
                            highlighted: true
                            enabled: root.ready && !root.engineNeedsSetup
                                     && root.settingsController.targetLanguage.length > 0
                                     && inputText.text.trim().length > 0
                            onClicked: root.showOutcome(root.settingsController.translateText(inputText.text))
                        }
                        Shortcut {
                            sequences: ["Ctrl+Return", "Ctrl+Enter"]
                            enabled: !root.showPreferences && translateButton.enabled
                            onActivated: translateButton.clicked()
                        }
                    }
                }
            }

            Rectangle {
                visible: !root.showPreferences
                Layout.fillWidth: true
                Layout.leftMargin: 24; Layout.rightMargin: 24
                implicitHeight: selectionColumn.implicitHeight + 2 * theme.padding
                radius: theme.radiusWindow
                color: theme.surface
                border.color: theme.border

                ColumnLayout {
                    id: selectionColumn
                    anchors.fill: parent; anchors.margins: theme.padding
                    spacing: 9

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Automatic selection")
                            color: theme.textPrimary
                            font.pixelSize: 18
                            font.bold: true
                        }
                        Text {
                            objectName: "selectionStatus"
                            text: root.selectionStatus()
                            color: theme.accent
                            font.pixelSize: theme.fontOriginal
                            font.bold: true
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.selectionHint()
                        color: theme.textPrimary
                        font.pixelSize: theme.fontBody
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ActionButton {
                            objectName: "testSelectionButton"
                            visible: root.selectionAvailable
                            text: root.selectionTestState === "waiting" ? qsTr("Cancel test") : qsTr("Test selection")
                            flat: true
                            onClicked: {
                                if (root.selectionTestState === "waiting")
                                    root.settingsController.cancelSelectionTest();
                                else
                                    root.settingsController.startSelectionTest();
                            }
                        }
                        Item { Layout.fillWidth: true }
                        ActionButton {
                            objectName: "selectionActionButton"
                            visible: root.selectionAvailable || root.selectionEnabled
                            enabled: root.engineNeedsSetup || root.selectionEnabled
                                     || root.settingsController.targetLanguage.length > 0
                            highlighted: true
                            text: {
                                if (!root.selectionEnabled)
                                    return root.engineNeedsSetup ? qsTr("Set up engine") : qsTr("Enable");
                                if (root.settingsController.paused) return qsTr("Resume");
                                if (root.needsConsent) return qsTr("Allow automatic sending");
                                return qsTr("Pause");
                            }
                            onClicked: {
                                if (!root.selectionEnabled) {
                                    if (root.engineNeedsSetup) root.openSettingsSection("engines");
                                    else root.settingsController.setAutomaticEnabled(true);
                                } else if (root.settingsController.paused) {
                                    root.settingsController.setPaused(false);
                                } else if (root.needsConsent) {
                                    root.settingsController.requestConsent();
                                } else {
                                    root.settingsController.setPaused(true);
                                }
                            }
                        }
                        ActionButton {
                            visible: root.selectionEnabled && root.needsConsent && !root.settingsController.paused
                            text: qsTr("Pause")
                            onClicked: root.settingsController.setPaused(true)
                        }
                    }
                    Text {
                        objectName: "selectionTestMessage"
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: root.selectionTestMessage()
                        color: theme.textSecondary
                        font.pixelSize: theme.fontOriginal
                        wrapMode: Text.Wrap
                    }
                }
            }

            Rectangle {
                visible: root.showPreferences && root.settingsSection === "engines"
                Layout.fillWidth: true
                Layout.leftMargin: 24; Layout.rightMargin: 24
                implicitHeight: engineColumn.implicitHeight + 2 * theme.padding
                radius: theme.radiusWindow
                color: theme.surface
                border.color: theme.border
                ColumnLayout {
                    id: engineColumn
                    readonly property bool choosingEngine: root.showAddEngine || root.providers.length === 0
                    anchors.fill: parent; anchors.margins: theme.padding
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: engineColumn.choosingEngine ? qsTr("Add a translation engine")
                                                             : qsTr("Translation engines")
                            color: theme.textPrimary
                            font.pixelSize: 18
                            font.bold: true
                        }
                        ActionButton {
                            objectName: "testEngineButton"
                            visible: !engineColumn.choosingEngine && root.activeProvider !== null
                            enabled: root.ready && !root.engineNeedsSetup
                                     && root.settingsController.targetLanguage.length > 0
                            text: qsTr("Test with sample text")
                            onClicked: root.showOutcome(root.settingsController.testEngine())
                        }
                        ActionButton {
                            objectName: "newEngineButton"
                            visible: root.providers.length > 0
                            text: root.showAddEngine ? qsTr("Cancel") : qsTr("Add engine")
                            onClicked: root.showAddEngine = !root.showAddEngine
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.providers.length === 0
                        text: qsTr("Choose an engine below. SnipLingo ships without a service API key.")
                        color: theme.textSecondary; font.pixelSize: theme.fontOriginal
                        wrapMode: Text.Wrap
                    }
                    ListView {
                        objectName: "engineList"
                        visible: !engineColumn.choosingEngine
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 210)
                        clip: true
                        ScrollBar.vertical: AppScrollBar { colors: theme }
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
                                        textFormat: Text.PlainText
                                        color: theme.textPrimary; font.pixelSize: theme.fontBody; font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.isLocal ? qsTr("On this device · %1").arg(modelData.host) : modelData.host
                                        textFormat: Text.PlainText
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
                        objectName: "addEnginePanel"
                        visible: root.ready && engineColumn.choosingEngine
                        Layout.fillWidth: true
                        implicitHeight: catalog.implicitHeight + 28
                        radius: theme.radiusControl
                        color: theme.surfaceElevated
                        border.color: theme.border

                        EngineCatalog {
                            id: catalog
                            anchors.fill: parent
                            anchors.margins: 14
                            colors: theme
                            onPresetRequested: type => root.selectNewEngine(root.settingsController.addPresetEngine(type))
                            onCustomRequested: (label, url, model) =>
                                root.selectNewEngine(root.settingsController.addProvider(label, url, model))
                        }
                    }
                    ColumnLayout {
                        visible: !engineColumn.choosingEngine && root.activeProvider !== null
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
                            visible: text.length > 0
                            text: {
                                if (root.activeProvider === null) return "";
                                if (root.activeProvider.type === "libretranslate")
                                    return qsTr("Start LibreTranslate or enter the address of your instance. Some instances require an API key.");
                                if (root.activeProvider.type === "deepl")
                                    return qsTr("Use the endpoint for your DeepL plan. Legacy API Free accounts use https://api-free.deepl.com.");
                                return "";
                            }
                            color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: root.activeProvider !== null && root.activeProvider.supportsApiKey
                            InputField {
                                id: keyField
                                Layout.fillWidth: true
                                placeholderText: root.activeProvider && root.activeProvider.needsApiKey
                                                 ? qsTr("Your API key") : qsTr("API key (optional)")
                                Accessible.name: qsTr("API key")
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
                visible: root.showPreferences && root.settingsSection === "selection"
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
                        text: {
                            if (!root.ready || root.settingsController.selectionBackendId.length === 0)
                                return qsTr("No selection listener is available in this session.");
                            if (root.settingsController.selectionBackendId === "atspi")
                                return qsTr("Listening uses accessibility events. Some browsers and editors may not expose selections.");
                            return qsTr("Listening uses the X11 primary selection; support varies by application.");
                        }
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    ActionButton {
                        visible: root.selectionEnabled
                        text: qsTr("Turn off selection translation")
                        onClicked: root.settingsController.setAutomaticEnabled(false)
                    }
                    ToggleSwitch {
                        objectName: "selectionImmediateSwitch"
                        enabled: root.automaticPopupAvailable
                        text: qsTr("Show translation immediately")
                        checked: root.ready && root.settingsController.showResultImmediately
                        onToggled: if (root.ready) root.settingsController.setShowResultImmediately(checked)
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.automaticPopupAvailable
                        text: qsTr("With this off, hover over or click the Translate button next to the selection.")
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    ToggleSwitch {
                        objectName: "xwaylandPopupSwitch"
                        visible: root.ready && root.settingsController.xwaylandPopupAvailable
                        text: qsTr("Show the floating result near the selection (XWayland)")
                        checked: root.ready && root.settingsController.xwaylandPopupEnabled
                        onToggled: if (root.ready) root.showOutcome(root.settingsController.setXwaylandPopup(checked))
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.ready && root.settingsController.xwaylandPopupAvailable
                        text: qsTr("This mode positions the floating result through XWayland and requires a restart. The result takes focus when it opens so clicking another window closes it. Clipboard integration may differ.")
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        objectName: "popupRestartHint"
                        visible: root.ready && root.settingsController.popupRestartRequired
                        text: qsTr("Restart SnipLingo to apply the floating result mode.")
                        color: theme.accent; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        objectName: "waylandPositionWarning"
                        visible: root.ready && !root.automaticPopupAvailable
                                 && !root.settingsController.xwaylandPopupEnabled
                        text: qsTr("This session cannot position a window next to the selection. Enable the floating result above, or read the translation on Home.")
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
                            else root.settingsController.requestConsent();
                        }
                    }
                }
            }
            Rectangle {
                visible: root.showPreferences && root.settingsSection === "interface"
                Layout.fillWidth: true
                Layout.leftMargin: 24; Layout.rightMargin: 24
                implicitHeight: interfaceColumn.implicitHeight + 2 * theme.padding
                radius: theme.radiusWindow; color: theme.surface; border.color: theme.border
                ColumnLayout {
                    id: interfaceColumn
                    anchors.fill: parent; anchors.margins: theme.padding
                    spacing: 8
                    Text { text: qsTr("Interface"); color: theme.textPrimary; font.pixelSize: 18; font.bold: true }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Choose the interface language and how large the interface and result text appear. Changes apply immediately.")
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("Language")
                            color: theme.textPrimary; font.pixelSize: theme.fontOriginal
                        }
                        Item { Layout.fillWidth: true }
                        LanguageCombo {
                            objectName: "uiLanguageCombo"
                            enabled: root.ready
                            Layout.preferredWidth: 250
                            textRole: "name"; valueRole: "code"
                            model: [
                                { code: "system", name: qsTr("Follow system") },
                                { code: "en", name: "English" },
                                { code: "zh-Hans", name: "简体中文" }
                            ]
                            currentIndex: root.ready
                                          ? Math.max(0, ["system", "en", "zh-Hans"].indexOf(root.settingsController.uiLanguage))
                                          : 0
                            onActivated: if (root.ready) root.settingsController.setUiLanguage(currentValue)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Follow system uses Chinese on a Chinese desktop and English everywhere else. Translation results are not affected.")
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("Interface zoom")
                            color: theme.textPrimary; font.pixelSize: theme.fontOriginal
                        }
                        Item { Layout.fillWidth: true }
                        LanguageCombo {
                            objectName: "uiScaleCombo"
                            enabled: root.ready
                            Layout.preferredWidth: 250
                            textRole: "name"; valueRole: "code"
                            model: [
                                { code: 0.75, name: "75%" },
                                { code: 1.0, name: "100%" },
                                { code: 1.25, name: "125%" },
                                { code: 1.5, name: "150%" }
                            ]
                            currentIndex: root.nearestListedIndex(root.uiScale, [0.75, 1.0, 1.25, 1.5])
                            onActivated: if (root.ready) root.settingsController.setUiScale(currentValue)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Scales the whole main window and the floating result. The floating result follows the new size with the next selection.")
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("Translation text size")
                            color: theme.textPrimary; font.pixelSize: theme.fontOriginal
                        }
                        Item { Layout.fillWidth: true }
                        // The sizes are the translation text's pixel height; they are stored as a
                        // multiplier of the 18 px base that ResultCard renders.
                        LanguageCombo {
                            objectName: "textScaleCombo"
                            enabled: root.ready
                            Layout.preferredWidth: 250
                            textRole: "name"; valueRole: "code"
                            model: [
                                { code: 14 / 18, name: "14 px" },
                                { code: 16 / 18, name: "16 px" },
                                { code: 1.0, name: "18 px" },
                                { code: 20 / 18, name: "20 px" },
                                { code: 22 / 18, name: "22 px" },
                                { code: 24 / 18, name: "24 px" },
                                { code: 26 / 18, name: "26 px" },
                                { code: 28 / 18, name: "28 px" },
                                { code: 30 / 18, name: "30 px" }
                            ]
                            currentIndex: root.nearestListedIndex(
                                18 * root.textScale, [14, 16, 18, 20, 22, 24, 26, 28, 30])
                            onActivated: if (root.ready) root.settingsController.setFontScale(currentValue)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Sets the translation text size. The original text follows at a proportionally smaller size.")
                        color: theme.textSecondary; font.pixelSize: theme.fontMeta; wrapMode: Text.Wrap
                    }
                }
            }
            Rectangle {
                visible: root.showPreferences && root.settingsSection === "network"
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
                        text: qsTr("Local engines connect directly. Remote connections prefer TLS 1.3 and can use TLS 1.2 through the selected proxy.")
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
            AboutPage {
                visible: root.showPreferences && root.settingsSection === "about"
                Layout.fillWidth: true
                Layout.leftMargin: 24; Layout.rightMargin: 24
                colors: theme
            }
            Item { implicitHeight: 12 }
        }
    }
    }
    }

    ResizeHandle {
        edges: Qt.TopEdge
        anchors { top: parent.top; left: parent.left; right: parent.right; leftMargin: 12; rightMargin: 12 }
        height: 6
    }
    ResizeHandle {
        edges: Qt.BottomEdge
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right; leftMargin: 12; rightMargin: 12 }
        height: 6
    }
    ResizeHandle {
        edges: Qt.LeftEdge
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: 12; bottomMargin: 12 }
        width: 6
    }
    ResizeHandle {
        edges: Qt.RightEdge
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom; topMargin: 12; bottomMargin: 12 }
        width: 6
    }
    ResizeHandle {
        edges: Qt.LeftEdge | Qt.TopEdge
        anchors { left: parent.left; top: parent.top }
        width: 12; height: 12
    }
    ResizeHandle {
        edges: Qt.RightEdge | Qt.TopEdge
        anchors { right: parent.right; top: parent.top }
        width: 12; height: 12
    }
    ResizeHandle {
        edges: Qt.LeftEdge | Qt.BottomEdge
        anchors { left: parent.left; bottom: parent.bottom }
        width: 12; height: 12
    }
    ResizeHandle {
        edges: Qt.RightEdge | Qt.BottomEdge
        anchors { right: parent.right; bottom: parent.bottom }
        width: 12; height: 12
    }

    ConsentSheet {
        id: consentSheet
        objectName: "consentDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        // The sheet lives on the overlay, outside the window's scaled content layer: scale it to
        // match and clamp its logical size against the scaled window.
        scale: root.uiScale
        width: Math.min(500, root.width / root.uiScale - 48)
        height: Math.min(implicitHeight, root.height / root.uiScale - 48)
        controller: root.settingsController
        destination: root.ready ? root.settingsController.consentDestination : ""
    }
}
