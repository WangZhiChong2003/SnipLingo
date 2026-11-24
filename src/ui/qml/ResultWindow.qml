import QtQuick

Window {
    id: root
    property var resultController: null
    property var settingsController: null
    property var placementScreen: null
    property bool interactive: false
    property bool automaticRequest: false
    property bool inlineResult: false
    property bool expanded: true
    property bool activatedForDismissal: false
    property bool dismissAfterLeave: false
    property bool userInteracted: false
    property bool pinned: false
    property bool runningOnXWayland: false

    // A stubbed controller in tests may not expose the scale properties; fall back to 1.0.
    readonly property real uiScale: {
        const value = settingsController !== null ? settingsController.uiScale : undefined
        return value === undefined ? 1.0 : value
    }
    readonly property real textScale: {
        const value = settingsController !== null ? settingsController.fontScale : undefined
        return value === undefined ? 1.0 : value
    }

    readonly property bool ready: resultController !== null
    readonly property string stateKey: resultCard.stateKey
    readonly property bool hasResult: resultCard.hasResult
    // XWayland cannot see clicks on native Wayland windows. A card that actually has focus can
    // still be dismissed when the compositor focuses the clicked window. A pinned card stays
    // expanded regardless of focus.
    readonly property bool showingCard: expanded && (pinned || !runningOnXWayland || active)
    readonly property rect workArea: placementScreen !== null ? placementScreen.availableGeometry
        : Qt.rect(0, 0, Math.min(screen.width, screen.desktopAvailableWidth),
                        Math.min(screen.height, screen.desktopAvailableHeight))

    Theme { id: theme }

    function togglePinned() {
        pinned = !pinned;
    }

    function fittedSize(preferred, available) {
        const scaled = preferred * uiScale;
        return available > 0 ? Math.min(scaled, Math.floor(available / 2)) : scaled;
    }

    function activateForInteraction() {
        if (!visible || !expanded)
            return;
        interactive = true;
        // Some platforms focus a window as soon as it is shown, before it accepts focus. No
        // activation transition follows the request in that case, so arm focus-loss dismissal
        // from the current state as well.
        if (active)
            activatedForDismissal = true;
        else
            requestActivate();
    }

    function activateForPresentation() {
        if (runningOnXWayland || !automaticRequest)
            activateForInteraction();
    }

    function showFromHover() {
        // After focus loss, the compact button can briefly reappear under a stale pointer.
        // Only a new presentation can start another hover activation.
        if (!expanded) {
            // Expansion can move the card away from the pointer before it gets an enter event.
            dismissAfterLeave = true;
            expanded = true;
            if (runningOnXWayland)
                activateForInteraction();
        }
    }

    visible: ready && resultController.visible && !inlineResult
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | (interactive ? 0 : Qt.WindowDoesNotAcceptFocus)
    onVisibleChanged: {
        if (!visible) {
            dismissAfterLeave = false;
            userInteracted = false;
            activatedForDismissal = false;
            interactive = false;
            pinned = false;
        } else {
            Qt.callLater(root.activateForPresentation);
        }
    }
    onExpandedChanged: if (!expanded) dismissAfterLeave = false
    onActiveChanged: {
        if (active && interactive && expanded) {
            activatedForDismissal = true;
        } else if (!active && activatedForDismissal && visible && expanded && ready && !pinned) {
            // Pinning outlives focus changes; unpinning re-arms dismissal for the next one.
            activatedForDismissal = false;
            resultController.dismissOnFocusLoss();
        }
    }

    color: "transparent"
    width: fittedSize(showingCard ? resultCard.preferredWidth : selectionButton.implicitWidth,
                      workArea.width)
    height: fittedSize(showingCard ? resultCard.implicitHeight : selectionButton.implicitHeight,
                       workArea.height)

    Shortcut {
        sequence: "Escape"
        enabled: root.visible && root.showingCard && root.ready
        onActivated: root.resultController.dismiss()
    }

    Timer {
        interval: 350
        // Keep pointer-leave dismissal for XWayland previews. An interaction such as opening
        // the original can move the window away from a stationary pointer. After interaction,
        // dismissal follows outside clicks and focus loss.
        running: root.visible && root.showingCard && root.dismissAfterLeave && root.runningOnXWayland
                 && !root.pinned && !root.userInteracted && !rootHover.hovered
        onTriggered: root.resultController.dismissOnFocusLoss()
    }

    // The card lays out in logical pixels and scales up to the window, so every control and all
    // text zoom together. Overlay popups (tooltips) read the factor through the card.
    Item {
        id: zoomLayer
        objectName: "zoomLayer"
        width: root.width / root.uiScale
        height: root.height / root.uiScale
        transform: Scale { xScale: root.uiScale; yScale: root.uiScale }

        HoverHandler {
            id: rootHover
            onHoveredChanged: if (hovered && root.showingCard) root.dismissAfterLeave = true
        }

        AppButton {
            id: selectionButton
            objectName: "selectionButton"
            anchors.fill: parent
            colors: theme
            visible: !root.showingCard
            highlighted: true
            iconSource: "qrc:/branding/sniplingo-icon.png"
            text: qsTr("Translate")
            Accessible.name: qsTr("Show translation")
            Accessible.description: qsTr("Hover or click to show the translation")
            onHoveredChanged: if (hovered) root.showFromHover()
            onClicked: {
                root.expanded = true;
                root.userInteracted = true;
                root.activateForInteraction();
            }
        }

        ResultCard {
            id: resultCard
            anchors.fill: parent
            colors: theme
            controller: root.resultController
            visible: root.showingCard
            pinVisible: true
            pinned: root.pinned
            textScale: root.textScale
            zoom: root.uiScale
            onInteractionRequested: {
                root.userInteracted = true;
                root.activateForInteraction();
            }
            onPinRequested: root.togglePinned()
        }
    }
}
