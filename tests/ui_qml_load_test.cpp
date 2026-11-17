// Loads the real result-window QML offscreen with a stub controller: the file must compile and its
// bindings must follow the controller. This is the cheap half of QML verification; layout still
// needs a real session.
#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QWindow>
#include <QtTest>

namespace {

class StubController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible MEMBER m_visible NOTIFY changed)
    Q_PROPERTY(QString translatedText MEMBER m_translatedText NOTIFY changed)
    Q_PROPERTY(QString originalText MEMBER m_originalText NOTIFY changed)
    Q_PROPERTY(QString providerLabel MEMBER m_providerLabel NOTIFY changed)
    Q_PROPERTY(QString stateKey MEMBER m_stateKey NOTIFY changed)
    Q_PROPERTY(QString errorKey MEMBER m_errorKey NOTIFY changed)
    Q_PROPERTY(bool canRetry MEMBER m_canRetry NOTIFY changed)
    Q_PROPERTY(bool originalVisible MEMBER m_originalVisible NOTIFY changed)
    Q_PROPERTY(bool originalCollapsed MEMBER m_originalCollapsed NOTIFY changed)

public:
    bool m_visible = false;
    QString m_translatedText;
    QString m_originalText;
    QString m_providerLabel = QStringLiteral("api.example.com · model");
    QString m_stateKey = QStringLiteral("hidden");
    QString m_errorKey;
    bool m_canRetry = false;
    bool m_originalVisible = false;
    bool m_originalCollapsed = true;

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
    void selectionIconExpandsOnHover();
    void expandedResultDismissesWhenAnotherWindowActivates();
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
    QVERIFY(window->property("height").toInt() >= 220);

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
    QVERIFY(qobject_cast<QQuickWindow *>(window.get())
                ->grabWindow()
                .save(QDir::tempPath() + QStringLiteral("/sniplyngo-result-window.png")));
    QVERIFY(QMetaObject::invokeMethod(window.get(), "activateForInteraction"));
    QTRY_VERIFY(window->property("interactive").toBool());
    controller.m_translatedText = QString(800, QChar(0x4f60));
    emit controller.changed();
    QTRY_COMPARE(window->property("height").toInt(), 460);

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

void UiQmlLoadTest::selectionIconExpandsOnHover() {
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
    QTRY_COMPARE(window->size(), QSize(44, 44));
    QTest::qWait(100);  // let the offscreen scene render before capturing the icon
    QVERIFY(!object->property("expanded").toBool());
    QVERIFY(!object->property("interactive").toBool());
    QVERIFY(window->grabWindow().save(
        QDir::tempPath() + QStringLiteral("/sniplyngo-selection-icon.png")));
    QTest::mouseMove(window, QPoint(22, 22));
    QTRY_VERIFY(object->property("expanded").toBool());
    QTRY_VERIFY(window->width() > 44);
    QTRY_VERIFY(object->property("interactive").toBool());
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

    QWindow other;
    other.setGeometry(20, 20, 120, 80);
    other.show();
    other.requestActivate();
    QTRY_COMPARE(controller.dismissCalls, 1);
}

QTEST_MAIN(UiQmlLoadTest)
#include "ui_qml_load_test.moc"
