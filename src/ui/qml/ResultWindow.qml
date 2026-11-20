import QtQuick

Window {
    id: root
    property var resultController: null
    property bool interactive: false
    property bool automaticRequest: false
    property bool inlineResult: false
    property bool expanded: true
    property bool activatedForDismissal: false
    property bool dismissAfterLeave: false
    property bool runningOnXWayland: false

    readonly property bool ready: resultController !== null
    readonly property string stateKey: resultCard.stateKey
    readonly property bool hasResult: resultCard.hasResult
    // XWayland cannot see clicks on native Wayland windows. A card that actually has focus can
    // still be dismissed when the compositor focuses the clicked window.
    readonly property bool showingCard: expanded && (!runningOnXWayland || active)

    Theme { id: theme }

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
            activatedForDismissal = false;
            interactive = false;
        } else {
            Qt.callLater(root.activateForPresentation);
        }
    }
    onExpandedChanged: if (!expanded) dismissAfterLeave = false
    onActiveChanged: {
        if (active && interactive && expanded) {
            activatedForDismissal = true;
        } else if (!active && activatedForDismissal && visible && expanded && ready) {
            activatedForDismissal = false;
            resultController.dismissOnFocusLoss();
        }
    }

    color: "transparent"
    width: showingCard ? 440 : selectionButton.implicitWidth
    height: showingCard ? resultCard.implicitHeight : selectionButton.implicitHeight

    Shortcut {
        sequence: "Escape"
        enabled: root.visible && root.showingCard && root.ready
        onActivated: root.resultController.dismiss()
    }

    Timer {
        interval: 350
        running: root.visible && root.showingCard && root.dismissAfterLeave && !rootHover.hovered
        onTriggered: root.resultController.dismissOnFocusLoss()
    }

    Item {
        anchors.fill: parent

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
                root.activateForInteraction();
            }
        }

        ResultCard {
            id: resultCard
            anchors.fill: parent
            colors: theme
            controller: root.resultController
            visible: root.showingCard
            onInteractionRequested: root.activateForInteraction()
        }
    }
}
