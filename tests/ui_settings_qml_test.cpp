// Settings window + consent sheet load offscreen with a stub controller: catches QML errors and
// verifies the privacy wording switches with the consent state.
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStringList>

namespace {
// QML type and binding errors are warnings, not failures: collect them so a test can assert none
// happened.
QStringList g_qmlWarnings;

void collectWarnings(QtMsgType type, const QMessageLogContext &, const QString &message) {
    if (type == QtWarningMsg || type == QtCriticalMsg)
        g_qmlWarnings.append(message);
}
}  // namespace

#include <QTemporaryDir>
#include <QtTest>

namespace {

class StubSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool automaticEnabled MEMBER m_automaticEnabled NOTIFY changed)
    Q_PROPERTY(bool paused MEMBER m_paused NOTIFY changed)
    Q_PROPERTY(bool showResultImmediately MEMBER m_showResultImmediately NOTIFY changed)
    Q_PROPERTY(bool observationActive MEMBER m_observationActive NOTIFY changed)
    Q_PROPERTY(bool waylandSession MEMBER m_waylandSession CONSTANT)
    Q_PROPERTY(QString selectionBackendId MEMBER m_selectionBackendId CONSTANT)
    Q_PROPERTY(QString selectionBackendChoice MEMBER m_selectionBackendChoice NOTIFY changed)
    Q_PROPERTY(bool selectionRestartRequired MEMBER m_selectionRestartRequired NOTIFY changed)
    Q_PROPERTY(QString sourceLanguage MEMBER m_sourceLanguage NOTIFY changed)
    Q_PROPERTY(QString targetLanguage MEMBER m_targetLanguage NOTIFY changed)
    Q_PROPERTY(QStringList targetLanguageCodes MEMBER m_codes CONSTANT)
    Q_PROPERTY(QString activeProviderId MEMBER m_activeProviderId NOTIFY changed)
    Q_PROPERTY(QVariantList providers MEMBER m_providers NOTIFY changed)
    Q_PROPERTY(bool consentGranted MEMBER m_consentGranted NOTIFY changed)
    Q_PROPERTY(bool consentStale MEMBER m_consentStale NOTIFY changed)
    Q_PROPERTY(QString consentDestination MEMBER m_consentDestination NOTIFY changed)
    Q_PROPERTY(bool credentialPersistent MEMBER m_credentialPersistent NOTIFY changed)
    Q_PROPERTY(QString credentialBackend MEMBER m_credentialBackend NOTIFY changed)
    Q_PROPERTY(QString credentialReason MEMBER m_credentialReason NOTIFY changed)
    Q_PROPERTY(QString proxyMode MEMBER m_proxyMode NOTIFY changed)
    Q_PROPERTY(QString proxyUrl MEMBER m_proxyUrl NOTIFY changed)
    Q_PROPERTY(QString proxyEffective MEMBER m_proxyEffective NOTIFY changed)

public:
    bool m_automaticEnabled = false;
    bool m_paused = false;
    bool m_showResultImmediately = false;
    bool m_observationActive = false;
    bool m_waylandSession = false;
    QString m_selectionBackendId = QStringLiteral("atspi");
    QString m_selectionBackendChoice = QStringLiteral("auto");
    bool m_selectionRestartRequired = false;
    QString m_sourceLanguage = QStringLiteral("auto");
    QString m_targetLanguage = QStringLiteral("zh-Hans");
    QStringList m_codes = {QStringLiteral("en"), QStringLiteral("zh-Hans")};
    QString m_activeProviderId;
    QVariantList m_providers;
    bool m_consentGranted = false;
    bool m_consentStale = false;
    QString m_consentDestination;
    bool m_credentialPersistent = false;
    QString m_credentialBackend = QStringLiteral("session");
    QString m_credentialReason = QStringLiteral("session only");
    QString m_proxyMode = QStringLiteral("system");
    QString m_proxyUrl;
    QString m_proxyEffective = QStringLiteral("system (no proxy variable set)");
    QString m_submittedText;

public slots:

    void setAutomaticEnabled(bool value) {
        m_automaticEnabled = value;
        emit changed();
    }

    void setPaused(bool value) {
        m_paused = value;
        emit changed();
    }

    void setShowResultImmediately(bool value) {
        m_showResultImmediately = value;
        emit changed();
    }

    void setTargetLanguage(const QString &value) {
        m_targetLanguage = value;
        emit changed();
    }

    QVariantMap translateText(const QString &text) {
        m_submittedText = text;
        return {{QStringLiteral("ok"), true}};
    }

    void setActiveProvider(const QString &value) {
        m_activeProviderId = value;
        emit changed();
    }

    QVariantMap setApiKey(const QString &, const QString &) {
        return {{QStringLiteral("ok"), true}};
    }

    void grantConsent() {
        m_consentGranted = true;
        m_consentStale = false;
        emit changed();
    }

    void revokeConsent() {
        m_consentGranted = false;
        m_consentStale = true;
        emit changed();
    }

signals:
    void changed();
    void consentRequired(const QString &destinationLabel, bool staleConsent);
    void failed(const QString &code, const QString &detail);
};

QVariantMap remoteProvider() {
    return {
        {QStringLiteral("id"), QStringLiteral("example")},
        {QStringLiteral("label"), QStringLiteral("Example")},
        {QStringLiteral("host"), QStringLiteral("api.example.com")},
        {QStringLiteral("baseUrl"), QStringLiteral("https://api.example.com/v1")},
        {QStringLiteral("isLocal"), false},
        {QStringLiteral("active"), true},
        {QStringLiteral("hasApiKey"), false},
        {QStringLiteral("needsApiKey"), true},
        {QStringLiteral("supportsApiKey"), true},
        {QStringLiteral("type"), QStringLiteral("openai-compatible")},
    };
}

}  // namespace

class UiSettingsQmlTest : public QObject {
    Q_OBJECT

private slots:
    void settingsWindowLoads();
    void consentSheetLoads();
    void settingsWindowSurvivesANullController();
    void engineControlsStayReachableOnceOneEngineExists();
    void openingTheLanguageChooserRaisesNoQmlErrors();
    void explicitTextCanBeSubmitted();
    void consentDialogIsVisibleAndSized();
    void hideKeepsAutomaticSetting();
    void selectionDisplaySettingFollowsController();
    void warnsWhenWaylandPointerMayBeUnavailable();
    void init();
    void cleanup();
};

void UiSettingsQmlTest::init() {
    g_qmlWarnings.clear();
    qInstallMessageHandler(collectWarnings);
}

void UiSettingsQmlTest::explicitTextCanBeSubmitted() {
    QQmlEngine engine;
    StubSettings controller;
    controller.m_providers = {remoteProvider()};
    controller.m_consentGranted = true;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    QObject *input = window->findChild<QObject *>(QStringLiteral("translationInput"));
    QObject *button = window->findChild<QObject *>(QStringLiteral("translateButton"));
    QVERIFY(input != nullptr);
    QVERIFY(button != nullptr);
    input->setProperty("text", QStringLiteral("hello"));
    QVERIFY(button->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QCOMPARE(controller.m_submittedText, QStringLiteral("hello"));
}

void UiSettingsQmlTest::hideKeepsAutomaticSetting() {
    QQmlEngine engine;
    StubSettings controller;
    controller.m_automaticEnabled = true;
    controller.m_observationActive = true;
    controller.m_consentGranted = true;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    qobject_cast<QQuickWindow *>(window.get())->show();
    QObject *hideButton = window->findChild<QObject *>(QStringLiteral("hideWindowButton"));
    QVERIFY(hideButton != nullptr);
    QVERIFY(QMetaObject::invokeMethod(hideButton, "clicked"));
    QVERIFY(!window->property("visible").toBool());
    QVERIFY(controller.m_automaticEnabled);
}

void UiSettingsQmlTest::selectionDisplaySettingFollowsController() {
    QQmlEngine engine;
    StubSettings controller;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    QObject *toggle = window->findChild<QObject *>(QStringLiteral("selectionImmediateSwitch"));
    QVERIFY(toggle != nullptr);
    QVERIFY(!toggle->property("checked").toBool());
    controller.setShowResultImmediately(true);
    QTRY_VERIFY(toggle->property("checked").toBool());
}

void UiSettingsQmlTest::warnsWhenWaylandPointerMayBeUnavailable() {
    QQmlEngine engine;
    StubSettings controller;
    controller.m_waylandSession = true;
    controller.m_selectionBackendId = QStringLiteral("x11-primary");
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    QObject *warning = window->findChild<QObject *>(QStringLiteral("waylandPositionWarning"));
    QVERIFY(warning != nullptr);
    QVERIFY(warning->property("visible").toBool());
}

void UiSettingsQmlTest::consentDialogIsVisibleAndSized() {
    QQmlEngine engine;
    StubSettings controller;
    controller.m_providers = {remoteProvider()};
    controller.m_consentDestination = QStringLiteral("api.example.com");
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    qobject_cast<QQuickWindow *>(window.get())->show();
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("consentDialog"));
    QVERIFY(dialog != nullptr);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(dialog->property("width").toInt() > 300);
    QVERIFY(dialog->property("height").toInt() > 100);
    QTest::qWait(50);
    QVERIFY(qobject_cast<QQuickWindow *>(window.get())
                ->grabWindow()
                .save(QDir::tempPath() + QStringLiteral("/sniplyngo-consent-window.png")));
}

void UiSettingsQmlTest::cleanup() {
    qInstallMessageHandler(nullptr);
    for (const QString &warning : g_qmlWarnings) {
        QVERIFY2(!warning.contains(QStringLiteral("Binding loop"))
                && !warning.contains(QStringLiteral("Unable to assign"))
                && !warning.contains(QStringLiteral("TypeError"))
                && !warning.contains(QStringLiteral("is not defined")),
            qPrintable(warning));
    }
}

void UiSettingsQmlTest::openingTheLanguageChooserRaisesNoQmlErrors() {
    // Regression: the combo delegate referenced an undefined id, so every delegate instance logged
    // "ReferenceError: control is not defined" while the window still looked fine.
    QQmlEngine engine;
    StubSettings controller;
    controller.m_providers = {remoteProvider()};
    controller.m_consentGranted = true;

    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    QTRY_COMPARE(window->property("providers").toList().size(), 1);

    QObject *combo = window->findChild<QObject *>(QStringLiteral("targetLanguageCombo"));
    QVERIFY2(combo != nullptr, "the language chooser is missing");
    QObject *popup = combo->property("popup").value<QObject *>();
    QVERIFY(popup != nullptr);
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTest::qWait(50);
    combo->setProperty("currentIndex", 1);
    QTest::qWait(50);

    for (const QString &warning : g_qmlWarnings) {
        QVERIFY2(!warning.contains(QStringLiteral("is not defined")),
            qPrintable(QStringLiteral("QML error: ") + warning));
        QVERIFY2(!warning.contains(QStringLiteral("TypeError")),
            qPrintable(QStringLiteral("QML error: ") + warning));
    }
}

void UiSettingsQmlTest::settingsWindowLoads() {
    QQmlEngine engine;
    StubSettings controller;
    controller.m_providers = {remoteProvider()};
    controller.m_activeProviderId = QStringLiteral("example");
    controller.m_consentDestination = QStringLiteral("api.example.com");

    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));

    window->setProperty("settingsController", QVariant::fromValue(&controller));
    QTRY_COMPARE(window->property("providers").toList().size(), 1);

    // Render once so a layout that pushes controls out of reach is visible, not just "compiles".
    if (auto *quickWindow = qobject_cast<QQuickWindow *>(window.get())) {
        quickWindow->show();
        QTest::qWait(150);
        const QString rendered
            = QDir::tempPath() + QStringLiteral("/sniplyngo-settings-window.png");
        QVERIFY2(quickWindow->grabWindow().save(rendered), qPrintable(rendered));
    }
    QCOMPARE(window->property("needsConsent").toBool(), true);

    controller.m_consentGranted = true;
    emit controller.changed();
    QTRY_COMPARE(window->property("needsConsent").toBool(), false);
}

void UiSettingsQmlTest::consentSheetLoads() {
    QQmlEngine engine;
    StubSettings controller;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/ConsentSheet.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> sheet(component.create());
    QVERIFY2(sheet, qPrintable(component.errorString()));
    sheet->setProperty("controller", QVariant::fromValue(&controller));
    sheet->setProperty("destination", QStringLiteral("api.example.com"));
    QCOMPARE(sheet->property("destination").toString(), QStringLiteral("api.example.com"));
}

void UiSettingsQmlTest::settingsWindowSurvivesANullController() {
    QQmlEngine engine;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    QCOMPARE(window->property("ready").toBool(), false);
    QCOMPARE(window->property("needsConsent").toBool(), false);
}

void UiSettingsQmlTest::engineControlsStayReachableOnceOneEngineExists() {
    // Regression: the "New engine" control used to live inside the collapsible panel, so
    // configuring one engine hid the panel *and* the only way to add another one.
    QQmlEngine engine;
    StubSettings controller;
    controller.m_providers = {remoteProvider()};
    controller.m_activeProviderId = QStringLiteral("example");
    controller.m_consentGranted = true;

    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    QTRY_COMPARE(window->property("providers").toList().size(), 1);

    // Render once so a layout that pushes controls out of reach is visible, not just "compiles".
    if (auto *quickWindow = qobject_cast<QQuickWindow *>(window.get())) {
        quickWindow->show();
        QTest::qWait(150);
        const QString rendered
            = QDir::tempPath() + QStringLiteral("/sniplyngo-settings-window.png");
        QVERIFY2(quickWindow->grabWindow().save(rendered), qPrintable(rendered));
    }

    QObject *newEngine = window->findChild<QObject *>(QStringLiteral("newEngineButton"));
    QVERIFY2(newEngine != nullptr, "the New engine control disappeared");
    QCOMPARE(newEngine->property("visible").toBool(), true);

    QObject *panel = window->findChild<QObject *>(QStringLiteral("addEnginePanel"));
    QVERIFY(panel != nullptr);
    QCOMPARE(
        panel->property("visible").toBool(), false);  // collapsed while one engine is configured

    window->setProperty("showAddEngine", true);
    QTRY_COMPARE(panel->property("visible").toBool(), true);

    if (auto *quickWindow = qobject_cast<QQuickWindow *>(window.get())) {
        quickWindow->show();
        QTest::qWait(150);
        const QString rendered
            = QDir::tempPath() + QStringLiteral("/sniplyngo-settings-expanded.png");
        QVERIFY2(quickWindow->grabWindow().save(rendered), qPrintable(rendered));
    }
}

QTEST_MAIN(UiSettingsQmlTest)
#include "ui_settings_qml_test.moc"
