// Loads the real result-window QML offscreen with a stub controller: the file must compile and its
// bindings must follow the controller. This is the cheap half of QML verification; layout still
// needs a real session.
#include <QDir>
#include <QFont>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWindow>
#include <QtTest>

#include "support/window_snapshot.h"

namespace {

class StubController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible MEMBER m_visible NOTIFY changed)
    Q_PROPERTY(QString translatedText MEMBER m_translatedText NOTIFY changed)
    Q_PROPERTY(QString originalText MEMBER m_originalText NOTIFY changed)
    Q_PROPERTY(QString providerLabel MEMBER m_providerLabel NOTIFY changed)
    Q_PROPERTY(QString sourceLanguage MEMBER m_sourceLanguage NOTIFY changed)
    Q_PROPERTY(QString targetLanguage MEMBER m_targetLanguage NOTIFY changed)
    Q_PROPERTY(QString stateKey MEMBER m_stateKey NOTIFY changed)
    Q_PROPERTY(QString errorKey MEMBER m_errorKey NOTIFY changed)
    Q_PROPERTY(bool canRetry MEMBER m_canRetry NOTIFY changed)
    Q_PROPERTY(bool originalVisible MEMBER m_originalVisible NOTIFY changed)
    Q_PROPERTY(bool originalCollapsed MEMBER m_originalCollapsed NOTIFY changed)
    Q_PROPERTY(qreal uiScale MEMBER m_uiScale NOTIFY changed)
    Q_PROPERTY(qreal fontScale MEMBER m_fontScale NOTIFY changed)

public:
    bool m_visible = false;
    QString m_translatedText;
    QString m_originalText;
    QString m_providerLabel = QStringLiteral("api.example.com · model");
    QString m_sourceLanguage = QStringLiteral("en");
    QString m_targetLanguage = QStringLiteral("zh-Hans");
    QString m_stateKey = QStringLiteral("hidden");
    QString m_errorKey;
    bool m_canRetry = false;
    bool m_originalVisible = false;
    bool m_originalCollapsed = true;
    qreal m_uiScale = 1.0;
    qreal m_fontScale = 1.0;

public slots:

    void dismiss() { ++dismissCalls; }

    void dismissOnFocusLoss() { ++dismissCalls; }

    void copyResult() { ++copyCalls; }

    void retry() { ++retryCalls; }

    void toggleOriginal() {
        m_originalCollapsed = !m_originalCollapsed;
        emit changed();
    }

signals:
    void changed();

public:
    int dismissCalls = 0;
    int copyCalls = 0;
    int retryCalls = 0;
};

}  // namespace

class UiQmlLoadTest : public QObject {
    Q_OBJECT

private slots:
    void resultWindowCompilesAndFollowsTheController();
    void resultWindowHandlesAMissingController();
    void selectionButtonRevealsOnHover_data();
    void selectionButtonRevealsOnHover();
    void selectionButtonExpandsOnClick();
    void directAutomaticResultUsesPlatformFocusPolicy_data();
    void directAutomaticResultUsesPlatformFocusPolicy();
    void inlineResultUsesTheHomeCard();
    void expandedResultDismissesWhenAnotherWindowActivates();
    void pinnedCardSurvivesFocusLossAndPointerLeave();
    void resultWindowScalesWithInterfaceSettings();
};

void UiQmlLoadTest::resultWindowCompilesAndFollowsTheController() {
    QQmlEngine engine;
    StubController controller;
    engine.rootContext()->setContextProperty(QStringLiteral("stubController"), &controller);

    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/ResultWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("resultController", QVariant::fromValue(&controller));
    const int compactHeight = window->property("height").toInt();
    QVERIFY(compactHeight < 220);

    QCOMPARE(window->property("visible").toBool(), false);
    QCOMPARE(window->property("stateKey").toString(), QStringLiteral("hidden"));

    controller.m_visible = true;
    controller.m_stateKey = QStringLiteral("done");
    controller.m_translatedText = QStringLiteral("你好，世界");
    controller.m_originalVisible = true;
    emit controller.changed();
    QTRY_COMPARE(window->property("visible").toBool(), true);
    QCOMPARE(window->property("stateKey").toString(), QStringLiteral("done"));
    QVERIFY(window->property("hasResult").toBool());
    QTest::qWait(50);
    QVERIFY(saveWindowSnapshot(qobject_cast<QQuickWindow *>(window.get()),
        QDir::tempPath() + QStringLiteral("/sniplyngo-result-window.png")));
    QVERIFY(QMetaObject::invokeMethod(window.get(), "activateForInteraction"));
    QTRY_VERIFY(window->property("interactive").toBool());
    controller.m_translatedText = QString(800, QChar(0x4f60));
    emit controller.changed();
    // The card grows with long text, but only up to the capped text area; beyond it, the text
    // scrolls inside the card. Layout propagation is asynchronous, so poll for the new height.
    QTRY_VERIFY(window->property("height").toInt() > compactHeight);
    const int grownHeight = window->property("height").toInt();
    QVERIFY(grownHeight <= compactHeight + 280);
    const QObject *text = window->findChild<QObject *>(QStringLiteral("translatedText"));
    QVERIFY(text);
    QVERIFY(text->property("contentHeight").toReal() > grownHeight);
    QVERIFY(saveWindowSnapshot(qobject_cast<QQuickWindow *>(window.get()),
        QDir::tempPath() + QStringLiteral("/sniplyngo-result-window-grown.png")));

    controller.m_stateKey = QStringLiteral("error");
    controller.m_errorKey = QStringLiteral("ProviderRateLimited");
    emit controller.changed();
    QTRY_COMPARE(window->property("stateKey").toString(), QStringLiteral("error"));
    controller.m_visible = false;
    emit controller.changed();
    QTRY_COMPARE(window->property("visible").toBool(), false);
}

void UiQmlLoadTest::resultWindowHandlesAMissingController() {
    // The file must be loadable in isolation: no binding may dereference a null controller.
    QQmlEngine engine;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/ResultWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    QCOMPARE(window->property("visible").toBool(), false);
    QCOMPARE(window->property("stateKey").toString(), QStringLiteral("hidden"));
}

void UiQmlLoadTest::selectionButtonRevealsOnHover_data() {
    QTest::addColumn<bool>("xwayland");
    QTest::newRow("x11-card-stays-on-pointer-leave") << false;
    QTest::newRow("xwayland-focus-loss") << true;
}

void UiQmlLoadTest::selectionButtonRevealsOnHover() {
    QFETCH(bool, xwayland);
    QQmlEngine engine;
    StubController controller;
    controller.m_stateKey = QStringLiteral("done");
    controller.m_translatedText = QStringLiteral("你好");
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/ResultWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    object->setProperty("resultController", QVariant::fromValue(&controller));
    object->setProperty("automaticRequest", true);
    object->setProperty("runningOnXWayland", xwayland);
    object->setProperty("expanded", false);
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(window != nullptr);
    // Use distinct global pointer positions so consecutive offscreen rows each deliver a move.
    window->setPosition(xwayland ? 230 : 100, 100);
    controller.m_visible = true;
    emit controller.changed();
    QTRY_VERIFY(window->isVisible());
    QTRY_VERIFY(window->width() < 150);
    QTest::qWait(100);  // let the offscreen scene render before capturing the icon
    QVERIFY(!object->property("expanded").toBool());
    QVERIFY(!object->property("interactive").toBool());
    QObject *button = window->findChild<QObject *>(QStringLiteral("selectionButton"));
    QVERIFY(button != nullptr);
    QVERIFY(!button->property("iconSource").toString().isEmpty());
    if (!xwayland) {
        QVERIFY(saveWindowSnapshot(
            window, QDir::tempPath() + QStringLiteral("/sniplyngo-selection-icon.png")));
    }
    QTest::mouseMove(window, QPoint(window->width() / 2, window->height() / 2));
    QTRY_VERIFY(object->property("showingCard").toBool());
    QCOMPARE(object->property("interactive").toBool(), xwayland);

    if (xwayland) {
        QVERIFY(window->isActive());
        QVERIFY(object->property("activatedForDismissal").toBool());
        // Simulate the compositor focusing the native application without sending pointer leave.
        QWindow other;
        other.setGeometry(20, 20, 120, 80);
        other.show();
        other.requestActivate();
        QTRY_COMPARE_WITH_TIMEOUT(controller.dismissCalls, 1, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!object->property("showingCard").toBool(), 1000);
    } else {
        // Plain X11 closes the card on an outside click, never because the pointer left it.
        // The click path itself needs a real X server; see x11_outside_click_live_test.
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(window, &leave);
        QTest::qWait(500);
        QCOMPARE(controller.dismissCalls, 0);
        QVERIFY(object->property("showingCard").toBool());
    }
}

void UiQmlLoadTest::selectionButtonExpandsOnClick() {
    QQmlEngine engine;
    StubController controller;
    controller.m_stateKey = QStringLiteral("done");
    controller.m_translatedText = QStringLiteral("你好");
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/ResultWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    object->setProperty("resultController", QVariant::fromValue(&controller));
    object->setProperty("automaticRequest", true);
    object->setProperty("expanded", false);
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(window != nullptr);
    window->setPosition(100, 100);
    controller.m_visible = true;
    emit controller.changed();
    QTRY_VERIFY(window->isVisible());
    QTRY_VERIFY(window->width() < 150);
    QObject *button = window->findChild<QObject *>(QStringLiteral("selectionButton"));
    QVERIFY(button != nullptr);
    // Keep the click path independent of whether the offscreen platform delivers hover events.
    button->setProperty("hoverEnabled", false);
    const QPoint center(window->width() / 2, window->height() / 2);
    QTest::mouseMove(window, center);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center);
    QTRY_VERIFY(object->property("expanded").toBool());
    QTRY_VERIFY(window->width() > 150);
    QTRY_VERIFY(object->property("interactive").toBool());
}

void UiQmlLoadTest::directAutomaticResultUsesPlatformFocusPolicy_data() {
    QTest::addColumn<bool>("xwayland");
    QTest::newRow("x11-passive") << false;
    QTest::newRow("xwayland-focus-loss") << true;
}

void UiQmlLoadTest::directAutomaticResultUsesPlatformFocusPolicy() {
    QFETCH(bool, xwayland);
    QQmlEngine engine;
    StubController controller;
    controller.m_stateKey = QStringLiteral("done");
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/ResultWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    object->setProperty("resultController", QVariant::fromValue(&controller));
    object->setProperty("automaticRequest", true);
    object->setProperty("runningOnXWayland", xwayland);
    controller.m_visible = true;
    emit controller.changed();
    QVERIFY(object->property("visible").toBool());
    QTRY_COMPARE(object->property("interactive").toBool(), xwayland);
    auto *window = qobject_cast<QWindow *>(object.get());
    QCOMPARE(window->flags().testFlag(Qt::WindowDoesNotAcceptFocus), !xwayland);
    QTRY_VERIFY(object->property("showingCard").toBool());

    object->setProperty("inlineResult", true);
    QVERIFY(!window->isVisible());
    QVERIFY(controller.m_visible);
}

void UiQmlLoadTest::inlineResultUsesTheHomeCard() {
    QQmlEngine engine;
    StubController controller;
    controller.m_stateKey = QStringLiteral("done");
    controller.m_translatedText = QStringLiteral("你好");
    controller.m_visible = true;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("resultController", QVariant::fromValue(&controller));
    qobject_cast<QQuickWindow *>(window.get())->show();
    QObject *card = window->findChild<QObject *>(QStringLiteral("inlineResultCard"));
    QVERIFY(card);
    QVERIFY(!card->property("visible").toBool());
    window->setProperty("showResultInline", true);
    QTRY_VERIFY(card->property("visible").toBool());
    // Pinning is a floating-result affordance; the inline card must not offer it.
    QObject *pin = card->findChild<QObject *>(QStringLiteral("pinButton"));
    QVERIFY(pin);
    QVERIFY(!pin->property("visible").toBool());
    QCOMPARE(
        card->findChild<QObject *>(QStringLiteral("translatedText"))->property("text").toString(),
        controller.m_translatedText);
    controller.m_visible = false;
    emit controller.changed();
    QTRY_VERIFY(!card->property("visible").toBool());
}

void UiQmlLoadTest::expandedResultDismissesWhenAnotherWindowActivates() {
    QQmlEngine engine;
    StubController controller;
    controller.m_stateKey = QStringLiteral("done");
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/ResultWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    object->setProperty("resultController", QVariant::fromValue(&controller));
    controller.m_visible = true;
    emit controller.changed();
    auto *result = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(result != nullptr);
    QTRY_VERIFY(result->isActive());
    // Wait until the window accepts focus before taking it away; the platform may have focused it
    // while it was still shown with WindowDoesNotAcceptFocus. Dismissal must be armed by then:
    // Qt 6.4's offscreen platform never delivers an activation transition after that point.
    QTRY_VERIFY(result->property("interactive").toBool());
    QTRY_VERIFY(result->property("activatedForDismissal").toBool());

    QWindow other;
    other.setGeometry(20, 20, 120, 80);
    other.show();
    other.requestActivate();
    QTRY_COMPARE(controller.dismissCalls, 1);
}

void UiQmlLoadTest::pinnedCardSurvivesFocusLossAndPointerLeave() {
    QQmlEngine engine;
    StubController controller;
    controller.m_stateKey = QStringLiteral("done");
    controller.m_translatedText = QStringLiteral("你好");
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/ResultWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    object->setProperty("resultController", QVariant::fromValue(&controller));
    object->setProperty("automaticRequest", true);
    object->setProperty("runningOnXWayland", true);
    object->setProperty("expanded", true);
    controller.m_visible = true;
    emit controller.changed();
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(window != nullptr);
    QTRY_VERIFY(object->property("showingCard").toBool());

    // Pin through the real button so the toggle wiring is exercised.
    auto *pin
        = qobject_cast<QQuickItem *>(window->findChild<QObject *>(QStringLiteral("pinButton")));
    QVERIFY(pin != nullptr);
    QVERIFY(pin->property("visible").toBool());
    QCOMPARE(pin->property("iconSource").toString(), QStringLiteral("qrc:/icons/pin.svg"));
    auto clickPin = [&]() {
        const QPointF center = pin->mapToScene(QPointF(pin->width() / 2, pin->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    };
    clickPin();
    QTRY_VERIFY(object->property("pinned").toBool());
    QCOMPARE(pin->property("iconSource").toString(), QStringLiteral("qrc:/icons/pin-filled.svg"));
    QVERIFY(
        saveWindowSnapshot(window, QDir::tempPath() + QStringLiteral("/sniplyngo-pin-active.png")));
    // The pin click is an interaction: the window takes focus and arms focus-loss dismissal.
    QTRY_VERIFY(object->property("activatedForDismissal").toBool());

    // A pinned card survives the pointer leaving it and another window taking focus.
    QWindow other;
    other.setGeometry(20, 20, 120, 80);
    other.show();
    other.requestActivate();
    QTRY_VERIFY(!window->isActive());
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(window, &leave);
    QTest::qWait(500);
    QCOMPARE(controller.dismissCalls, 0);
    QVERIFY(object->property("showingCard").toBool());

    // Unpinning re-arms dismissal for the next focus loss.
    clickPin();
    QTRY_VERIFY(!object->property("pinned").toBool());
    QTRY_VERIFY(window->isActive());
    other.requestActivate();
    QTRY_COMPARE(controller.dismissCalls, 1);
}

void UiQmlLoadTest::resultWindowScalesWithInterfaceSettings() {
    QQmlEngine engine;
    StubController controller;
    controller.m_stateKey = QStringLiteral("done");
    controller.m_translatedText = QStringLiteral("你好");
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/ResultWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    object->setProperty("resultController", QVariant::fromValue(&controller));
    object->setProperty("settingsController", QVariant::fromValue(&controller));
    object->setProperty("automaticRequest", true);
    object->setProperty("expanded", true);
    controller.m_visible = true;
    emit controller.changed();
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(window != nullptr);
    QTRY_COMPARE(window->width(), 440);

    // Interface zoom scales the window while the content layer keeps logical pixels.
    controller.m_uiScale = 1.25;
    emit controller.changed();
    QTRY_COMPARE(window->width(), 550);
    auto *zoomLayer = window->findChild<QQuickItem *>(QStringLiteral("zoomLayer"));
    QVERIFY(zoomLayer != nullptr);
    QCOMPARE(zoomLayer->width(), 440.0);

    // The text size applies to the translated text on top of the zoom.
    controller.m_fontScale = 1.5;
    emit controller.changed();
    const QObject *text = window->findChild<QObject *>(QStringLiteral("translatedText"));
    QVERIFY(text != nullptr);
    QTRY_COMPARE(text->property("font").value<QFont>().pixelSize(), qRound(18 * 1.5));
}

QTEST_MAIN(UiQmlLoadTest)
#include "ui_qml_load_test.moc"
