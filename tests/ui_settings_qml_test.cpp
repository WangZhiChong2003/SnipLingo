// Settings window + consent sheet load offscreen with a stub controller: catches QML errors and
// verifies the privacy wording switches with the consent state.
#include "ui/settings_controller.h"

#include <QGuiApplication>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QStringList>
#include <QTranslator>

#include "support/window_snapshot.h"

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

QQuickItem *visibleItem(QQuickItem *root, const QString &name) {
    QList<QQuickItem *> items {root};
    while (!items.isEmpty()) {
        QQuickItem *item = items.takeLast();
        if (item->objectName() == name && item->isVisible())
            return item;
        items.append(item->childItems());
    }
    return nullptr;
}

class StubSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool automaticEnabled MEMBER m_automaticEnabled NOTIFY changed)
    Q_PROPERTY(bool paused MEMBER m_paused NOTIFY changed)
    Q_PROPERTY(bool showResultImmediately MEMBER m_showResultImmediately NOTIFY changed)
    Q_PROPERTY(bool xwaylandPopupAvailable MEMBER m_xwaylandPopupAvailable NOTIFY changed)
    Q_PROPERTY(bool xwaylandPopupEnabled MEMBER m_xwaylandPopupEnabled NOTIFY changed)
    Q_PROPERTY(bool popupRestartRequired MEMBER m_popupRestartRequired NOTIFY changed)
    Q_PROPERTY(bool observationActive MEMBER m_observationActive NOTIFY changed)
    Q_PROPERTY(QString selectionTestState MEMBER m_selectionTestState NOTIFY changed)
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
    Q_PROPERTY(QString uiLanguage MEMBER m_uiLanguage NOTIFY changed)
    Q_PROPERTY(qreal uiScale MEMBER m_uiScale NOTIFY changed)
    Q_PROPERTY(qreal fontScale MEMBER m_fontScale NOTIFY changed)

public:
    bool m_automaticEnabled = false;
    bool m_paused = false;
    bool m_showResultImmediately = false;
    bool m_xwaylandPopupAvailable = false;
    bool m_xwaylandPopupEnabled = false;
    bool m_popupRestartRequired = false;
    bool m_initialXwaylandPopup = false;
    bool m_observationActive = false;
    QString m_selectionTestState;
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
    QString m_uiLanguage = QStringLiteral("system");
    QString m_submittedText;
    QString m_addedPreset;
    qreal m_uiScale = 1.0;
    qreal m_fontScale = 1.0;

public slots:

    void setAutomaticEnabled(bool value) {
        m_automaticEnabled = value;
        emit changed();
    }

    void setPaused(bool value) {
        m_paused = value;
        emit changed();
    }

    void setUiScale(qreal value) {
        m_uiScale = value;
        emit changed();
    }

    void setUiLanguage(const QString &value) {
        m_uiLanguage = value;
        emit changed();
    }

    void setFontScale(qreal value) {
        m_fontScale = value;
        emit changed();
    }

    void startSelectionTest() {
        m_selectionTestState = QStringLiteral("waiting");
        emit changed();
    }

    void cancelSelectionTest() {
        m_selectionTestState.clear();
        emit changed();
    }

    void requestConsent() { emit consentRequired(m_consentDestination, m_consentStale); }

    void setShowResultImmediately(bool value) {
        m_showResultImmediately = value;
        emit changed();
    }

    QVariantMap setXwaylandPopup(bool value) {
        m_xwaylandPopupEnabled = value;
        m_popupRestartRequired = value != m_initialXwaylandPopup;
        emit changed();
        return {{QStringLiteral("ok"), true}};
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

    QVariantMap addPresetEngine(const QString &type) {
        m_addedPreset = type;
        return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), type}};
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
    void engineCatalogRequiresExplicitChoice();
    void addingEngineRequiresConfirmationWhenAutomaticSendingNeedsConsent_data();
    void addingEngineRequiresConfirmationWhenAutomaticSendingNeedsConsent();
    void aboutUsesTheApplicationVersion_data();
    void aboutUsesTheApplicationVersion();
    void openingTheLanguageChooserRaisesNoQmlErrors();
    void languageChooserSupportsKeyboardSelection();
    void proxyChooserSupportsMouseSelectionNearWindowEdge();
    void explicitTextCanBeSubmitted();
    void consentDialogIsVisibleAndSized();
    void longConsentDestinationFitsSmallWindow();
    void darkPaletteKeepsDialogReadable();
    void hideKeepsAutomaticSetting();
    void selectionDisplaySettingFollowsController();
    void interfaceScaleChoosersFollowAndApplyTheController();
    void uiLanguageChooserFollowsAndAppliesTheController();
    void xwaylandPopupToggleExplainsRestart();
    void explainsInlineResultsWhenPlacementIsUnavailable();
    void homeExposesSelectionStatusAndTest();
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
    QVariantMap configured = remoteProvider();
    configured.insert(QStringLiteral("hasApiKey"), true);
    controller.m_providers = {configured};
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

void UiSettingsQmlTest::interfaceScaleChoosersFollowAndApplyTheController() {
    QQmlEngine engine;
    StubSettings controller;
    controller.m_uiScale = 1.25;
    controller.m_fontScale = 1.4;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    window->setProperty("showPreferences", true);
    window->setProperty("settingsSection", QStringLiteral("interface"));
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(quickWindow != nullptr);
    quickWindow->show();

    // Window defaults follow the zoom factor.
    QCOMPARE(window->property("width").toInt(), 1088);  // Math.round(870 * 1.25)
    QCOMPARE(window->property("minimumWidth").toInt(), 775);

    auto *zoomCombo = window->findChild<QQuickItem *>(QStringLiteral("uiScaleCombo"));
    auto *textCombo = window->findChild<QQuickItem *>(QStringLiteral("textScaleCombo"));
    QVERIFY(zoomCombo != nullptr);
    QVERIFY(textCombo != nullptr);
    QVERIFY(zoomCombo->isVisible());
    QVERIFY(textCombo->isVisible());
    QCOMPARE(zoomCombo->property("currentIndex").toInt(), 2);  // 125%
    QCOMPARE(textCombo->property("currentIndex").toInt(), 6);  // 18 * 1.4 = 25.2 -> "26 px"

    // Selecting an option applies immediately through the controller.
    QObject *popup = zoomCombo->property("popup").value<QObject *>();
    QVERIFY(popup != nullptr);
    zoomCombo->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(zoomCombo->hasActiveFocus());
    QTest::keyClick(quickWindow, Qt::Key_Space);
    QTRY_VERIFY(popup->property("visible").toBool());
    QTest::keyClick(quickWindow, Qt::Key_Down);
    QTest::keyClick(quickWindow, Qt::Key_Return);
    QTRY_COMPARE(controller.m_uiScale, 1.5);
    QTRY_COMPARE(window->property("width").toInt(), 1305);  // Math.round(870 * 1.5)
    QTRY_VERIFY(zoomCombo->property("currentIndex").toInt() == 3);
    QVERIFY(saveWindowSnapshot(
        quickWindow, QDir::tempPath() + QStringLiteral("/sniplyngo-settings-zoom.png")));

    QObject *textPopup = textCombo->property("popup").value<QObject *>();
    QVERIFY(textPopup != nullptr);
    textCombo->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(textCombo->hasActiveFocus());
    QTest::keyClick(quickWindow, Qt::Key_Space);
    QTRY_VERIFY(textPopup->property("visible").toBool());
    QTest::keyClick(quickWindow, Qt::Key_Down);
    QTest::keyClick(quickWindow, Qt::Key_Return);
    QTRY_COMPARE(controller.m_fontScale, 28.0 / 18.0);
}

void UiSettingsQmlTest::uiLanguageChooserFollowsAndAppliesTheController() {
    Q_INIT_RESOURCE(sniplyngo_runtime_translations);
    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(":/i18n/sniplyngo_zh_CN.qm")));
    QVERIFY(QCoreApplication::installTranslator(&translator));
    QQmlEngine engine;
    StubSettings controller;
    controller.m_uiLanguage = QStringLiteral("zh-Hans");
    connect(&controller, &StubSettings::changed, &engine, [&] {
        if (controller.m_uiLanguage == QStringLiteral("zh-Hans"))
            QCoreApplication::installTranslator(&translator);
        else
            QCoreApplication::removeTranslator(&translator);
        engine.retranslate();
    });
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    window->setProperty("showPreferences", true);
    window->setProperty("settingsSection", QStringLiteral("interface"));
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(quickWindow != nullptr);
    quickWindow->show();

    auto *languageCombo = window->findChild<QQuickItem *>(QStringLiteral("uiLanguageCombo"));
    QVERIFY(languageCombo != nullptr);
    QVERIFY(languageCombo->isVisible());
    QCOMPARE(languageCombo->property("displayText").toString(), QStringLiteral("简体中文"));

    QObject *popup = languageCombo->property("popup").value<QObject *>();
    QVERIFY(popup != nullptr);
    languageCombo->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(languageCombo->hasActiveFocus());
    QTest::keyClick(quickWindow, Qt::Key_Space);
    QTRY_VERIFY(popup->property("visible").toBool());
    QQuickItem *systemOption = nullptr;
    QTRY_VERIFY(
        (systemOption = visibleItem(quickWindow->contentItem(), QStringLiteral("comboOption0")))
        != nullptr);
    QCOMPARE(systemOption->property("text").toString(), QStringLiteral("跟随系统"));
    QTest::keyClick(quickWindow, Qt::Key_Up);
    QTest::keyClick(quickWindow, Qt::Key_Return);
    QTRY_COMPARE(controller.m_uiLanguage, QStringLiteral("en"));
    QCOMPARE(languageCombo->property("displayText").toString(), QStringLiteral("English"));

    QTest::keyClick(quickWindow, Qt::Key_Space);
    QTRY_VERIFY(popup->property("visible").toBool());
    QTRY_VERIFY(
        (systemOption = visibleItem(quickWindow->contentItem(), QStringLiteral("comboOption0")))
        != nullptr);
    QCOMPARE(systemOption->property("text").toString(), QStringLiteral("Follow system"));
    QTest::keyClick(quickWindow, Qt::Key_Down);
    QTest::keyClick(quickWindow, Qt::Key_Return);
    QTRY_COMPARE(controller.m_uiLanguage, QStringLiteral("zh-Hans"));
    QCOMPARE(languageCombo->property("displayText").toString(), QStringLiteral("简体中文"));

    controller.setUiLanguage(QStringLiteral("system"));
    QCOMPARE(languageCombo->property("displayText").toString(), QStringLiteral("Follow system"));
}

void UiSettingsQmlTest::xwaylandPopupToggleExplainsRestart() {
    QQmlEngine engine;
    StubSettings controller;
    controller.m_xwaylandPopupAvailable = true;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    window->setProperty("showPreferences", true);
    window->setProperty("settingsSection", QStringLiteral("selection"));
    window->setProperty("automaticPopupAvailable", false);
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(quickWindow != nullptr);
    quickWindow->show();
    quickWindow->requestActivate();

    auto *toggle = window->findChild<QQuickItem *>(QStringLiteral("xwaylandPopupSwitch"));
    QVERIFY(toggle != nullptr);
    QVERIFY(toggle->isVisible());
    QVERIFY(!toggle->property("checked").toBool());
    QObject *hint = window->findChild<QObject *>(QStringLiteral("popupRestartHint"));
    QVERIFY(hint != nullptr);
    QVERIFY(!hint->property("visible").toBool());
    QObject *warning = window->findChild<QObject *>(QStringLiteral("waylandPositionWarning"));
    QVERIFY(warning != nullptr);
    QVERIFY(warning->property("visible").toBool());

    toggle->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(toggle->hasActiveFocus());
    QTest::keyClick(quickWindow, Qt::Key_Space);
    QTRY_VERIFY(controller.m_xwaylandPopupEnabled);
    QTRY_VERIFY(toggle->property("checked").toBool());
    QTRY_VERIFY(hint->property("visible").toBool());
    QTRY_VERIFY(!warning->property("visible").toBool());
}

void UiSettingsQmlTest::explainsInlineResultsWhenPlacementIsUnavailable() {
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
    window->setProperty("automaticPopupAvailable", false);
    window->setProperty("showPreferences", true);
    window->setProperty("settingsSection", QStringLiteral("selection"));
    QObject *warning = window->findChild<QObject *>(QStringLiteral("waylandPositionWarning"));
    QVERIFY(warning != nullptr);
    QVERIFY(warning->property("visible").toBool());
    QObject *toggle = window->findChild<QObject *>(QStringLiteral("xwaylandPopupSwitch"));
    QVERIFY(toggle != nullptr);
    QVERIFY(!toggle->property("visible").toBool());
}

void UiSettingsQmlTest::homeExposesSelectionStatusAndTest() {
    QQmlEngine engine;
    StubSettings controller;
    QVariantMap configured = remoteProvider();
    configured.insert(QStringLiteral("hasApiKey"), true);
    controller.m_providers = {configured};
    controller.m_consentDestination = QStringLiteral("api.example.com");
    controller.m_automaticEnabled = true;
    controller.m_observationActive = true;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    qobject_cast<QQuickWindow *>(window.get())->show();

    QObject *status = window->findChild<QObject *>(QStringLiteral("selectionStatus"));
    QObject *test = window->findChild<QObject *>(QStringLiteral("testSelectionButton"));
    QObject *mode = window->findChild<QObject *>(QStringLiteral("selectionActionButton"));
    QVERIFY(status && test && mode);
    QCOMPARE(status->property("text").toString(), QStringLiteral("Permission needed"));
    QVERIFY(QMetaObject::invokeMethod(test, "clicked"));
    QCOMPARE(test->property("text").toString(), QStringLiteral("Cancel test"));
    QVERIFY(QMetaObject::invokeMethod(test, "clicked"));
    QCOMPARE(test->property("text").toString(), QStringLiteral("Test selection"));

    controller.grantConsent();
    QCOMPARE(status->property("text").toString(), QStringLiteral("On"));
    QVERIFY(QMetaObject::invokeMethod(mode, "clicked"));
    QCOMPARE(status->property("text").toString(), QStringLiteral("Paused"));
    QVERIFY(QMetaObject::invokeMethod(mode, "clicked"));
    QCOMPARE(status->property("text").toString(), QStringLiteral("On"));
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
    auto *frame = window->findChild<QQuickItem *>(QStringLiteral("windowFrame"));
    auto *background = dialog->property("background").value<QQuickItem *>();
    QVERIFY(frame && background);
    QCOMPARE(background->property("radius").toReal(), frame->property("radius").toReal());
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(dialog->property("width").toInt() > 300);
    QVERIFY(dialog->property("height").toInt() > 100);
    QTest::qWait(50);
    QVERIFY(saveWindowSnapshot(qobject_cast<QQuickWindow *>(window.get()),
        QDir::tempPath() + QStringLiteral("/sniplyngo-consent-window.png")));
    QTest::keyClick(qobject_cast<QQuickWindow *>(window.get()), Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(!controller.m_consentGranted);
}

void UiSettingsQmlTest::longConsentDestinationFitsSmallWindow() {
    QQmlEngine engine;
    StubSettings controller;
    controller.m_consentDestination = QStringLiteral("api.example.com · ") + QString(500, 'x');
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(quickWindow != nullptr);
    quickWindow->resize(620, 480);
    quickWindow->show();
    QTest::qWait(100);

    QObject *dialog = window->findChild<QObject *>(QStringLiteral("consentDialog"));
    QVERIFY(dialog != nullptr);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(dialog->property("height").toReal() <= quickWindow->height() - 48);

    auto *details = window->findChild<QQuickItem *>(QStringLiteral("consentDetails"));
    auto *allow = window->findChild<QQuickItem *>(QStringLiteral("allowConsentButton"));
    QVERIFY(details != nullptr);
    QVERIFY(allow != nullptr);
    QTRY_VERIFY(details->property("contentHeight").toReal() > details->height());
    const QPointF buttonCenter
        = allow->mapToScene(QPointF(allow->width() / 2, allow->height() / 2));
    QVERIFY2(QRectF(QPointF(0, 0), quickWindow->size()).contains(buttonCenter),
        qPrintable(QStringLiteral("Button center: %1, %2; dialog height: %3; details height: %4")
                       .arg(buttonCenter.x())
                       .arg(buttonCenter.y())
                       .arg(dialog->property("height").toReal())
                       .arg(details->height())));
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier, buttonCenter.toPoint());
    QTRY_VERIFY(controller.m_consentGranted);
    QTRY_VERIFY(!dialog->property("visible").toBool());
}

void UiSettingsQmlTest::darkPaletteKeepsDialogReadable() {
    const QPalette previousPalette = QGuiApplication::palette();
    const auto restorePalette
        = qScopeGuard([previousPalette] { QGuiApplication::setPalette(previousPalette); });
    QPalette darkPalette = previousPalette;
    darkPalette.setColor(QPalette::Window, QColor(QStringLiteral("#101725")));
    QGuiApplication::setPalette(darkPalette);

    QQmlEngine engine;
    StubSettings controller;
    controller.m_consentDestination = QStringLiteral("api.example.com");
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(quickWindow != nullptr);
    quickWindow->show();
    QObject *frame = window->findChild<QObject *>(QStringLiteral("windowFrame"));
    QVERIFY(frame != nullptr);
    QCOMPARE(frame->property("color").value<QColor>(), QColor(QStringLiteral("#101725")));
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("consentDialog"));
    QVERIFY(dialog != nullptr);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTest::qWait(50);
    QVERIFY(saveWindowSnapshot(
        quickWindow, QDir::tempPath() + QStringLiteral("/sniplyngo-consent-dark.png")));
}

void UiSettingsQmlTest::cleanup() {
    qInstallMessageHandler(nullptr);
    for (const QString &warning : g_qmlWarnings) {
        QVERIFY2(!warning.contains(QStringLiteral("Binding loop"))
                && !warning.contains(QStringLiteral("Unable to assign"))
                && !warning.contains(QStringLiteral("TypeError"))
                && !warning.contains(QStringLiteral("is not defined"))
                && !warning.startsWith(QStringLiteral("QSGPlainTexture: Mipmap settings changed")),
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
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(quickWindow != nullptr);
    quickWindow->show();
    QObject *scrollView = window->findChild<QObject *>(QStringLiteral("settingsScrollView"));
    QVERIFY(scrollView != nullptr);
    auto *scrollbar = scrollView->findChild<QQuickItem *>(QStringLiteral("verticalScrollBar"));
    QVERIFY(scrollbar != nullptr);
    QTRY_VERIFY2(scrollbar->height() > 200,
        qPrintable(QStringLiteral("Settings scrollbar geometry: %1, %2, %3, %4")
                       .arg(scrollbar->x())
                       .arg(scrollbar->y())
                       .arg(scrollbar->width())
                       .arg(scrollbar->height())));

    QObject *combo = window->findChild<QObject *>(QStringLiteral("targetLanguageCombo"));
    QVERIFY2(combo != nullptr, "the language chooser is missing");
    QObject *popup = combo->property("popup").value<QObject *>();
    QVERIFY(popup != nullptr);
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY(popup->property("visible").toBool());
    QTest::qWait(100);
    QVERIFY(saveWindowSnapshot(
        quickWindow, QDir::tempPath() + QStringLiteral("/sniplyngo-language-popup.png")));
    combo->setProperty("currentIndex", 1);
    QTest::qWait(50);

    for (const QString &warning : g_qmlWarnings) {
        QVERIFY2(!warning.contains(QStringLiteral("is not defined")),
            qPrintable(QStringLiteral("QML error: ") + warning));
        QVERIFY2(!warning.contains(QStringLiteral("TypeError")),
            qPrintable(QStringLiteral("QML error: ") + warning));
    }
}

void UiSettingsQmlTest::languageChooserSupportsKeyboardSelection() {
    QQmlEngine engine;
    StubSettings controller;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(quickWindow != nullptr);
    quickWindow->show();
    quickWindow->requestActivate();

    auto *combo = window->findChild<QQuickItem *>(QStringLiteral("targetLanguageCombo"));
    QVERIFY(combo != nullptr);
    QObject *popup = combo->property("popup").value<QObject *>();
    QVERIFY(popup != nullptr);
    combo->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(combo->hasActiveFocus());
    QTest::keyClick(quickWindow, Qt::Key_Space);
    QTRY_VERIFY(popup->property("visible").toBool());
    QTest::keyClick(quickWindow, Qt::Key_Up);
    QTest::keyClick(quickWindow, Qt::Key_Return);
    QTRY_COMPARE(controller.m_targetLanguage, QStringLiteral("en"));
    QTRY_VERIFY(!popup->property("visible").toBool());
}

void UiSettingsQmlTest::proxyChooserSupportsMouseSelectionNearWindowEdge() {
    QQmlEngine engine;
    StubSettings controller;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    window->setProperty("showPreferences", true);
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(quickWindow != nullptr);
    quickWindow->resize(620, 480);
    quickWindow->show();
    QTest::qWait(100);

    QQuickItem *network
        = visibleItem(quickWindow->contentItem(), QStringLiteral("settingsSection-network"));
    QVERIFY(network);
    QVERIFY(QMetaObject::invokeMethod(network, "clicked"));
    auto *combo = window->findChild<QQuickItem *>(QStringLiteral("proxyModeBox"));
    QVERIFY(combo != nullptr);
    const QPointF comboCenter = combo->mapToScene(QPointF(combo->width() / 2, combo->height() / 2));
    QVERIFY(QRectF(QPointF(0, 0), quickWindow->size()).contains(comboCenter));
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier, comboCenter.toPoint());
    QObject *popup = combo->property("popup").value<QObject *>();
    QVERIFY(popup != nullptr);
    QTRY_VERIFY(popup->property("visible").toBool());
    QObject *list = popup->property("contentItem").value<QObject *>();
    QVERIFY(list != nullptr);
    auto *listContent = list->property("contentItem").value<QQuickItem *>();
    QVERIFY(listContent != nullptr);
    QQuickItem *manual = nullptr;
    auto findManual = [listContent]() -> QQuickItem * {
        for (QQuickItem *item : listContent->childItems()) {
            if (item->objectName() == QStringLiteral("comboOption1"))
                return item;
        }
        return nullptr;
    };
    QTRY_VERIFY_WITH_TIMEOUT((manual = findManual()) != nullptr, 3000);
    QCOMPARE(manual->property("text").toString(), QStringLiteral("Manual"));
    const QPointF optionCenter
        = manual->mapToScene(QPointF(manual->width() / 2, manual->height() / 2));
    QVERIFY(QRectF(QPointF(0, 0), quickWindow->size()).contains(optionCenter));
    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier, optionCenter.toPoint());
    QTRY_COMPARE(combo->property("currentValue").toString(), QStringLiteral("manual"));
    QObject *proxyUrl = window->findChild<QObject *>(QStringLiteral("proxyUrlField"));
    QVERIFY(proxyUrl != nullptr);
    QTRY_VERIFY(proxyUrl->property("visible").toBool());
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
        QVERIFY2(saveWindowSnapshot(quickWindow, rendered), qPrintable(rendered));
    }
    QCOMPARE(window->property("needsConsent").toBool(), true);

    controller.m_consentGranted = true;
    emit controller.changed();
    QTRY_COMPARE(window->property("needsConsent").toBool(), false);
}

void UiSettingsQmlTest::consentSheetLoads() {
    QQmlEngine engine;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/ConsentSheet.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> sheet(component.create());
    QVERIFY2(sheet, qPrintable(component.errorString()));
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
    QObject *preferences = window->findChild<QObject *>(QStringLiteral("preferencesButton"));
    QVERIFY(preferences);
    QVERIFY(QMetaObject::invokeMethod(preferences, "clicked"));
    QTRY_COMPARE(window->property("providers").toList().size(), 1);

    // Render once so a layout that pushes controls out of reach is visible, not just "compiles".
    if (auto *quickWindow = qobject_cast<QQuickWindow *>(window.get())) {
        quickWindow->show();
        QTest::qWait(150);
        const QString rendered
            = QDir::tempPath() + QStringLiteral("/sniplyngo-settings-window.png");
        QVERIFY2(saveWindowSnapshot(quickWindow, rendered), qPrintable(rendered));
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
        QVERIFY2(saveWindowSnapshot(quickWindow, rendered), qPrintable(rendered));
    }
}

void UiSettingsQmlTest::engineCatalogRequiresExplicitChoice() {
    QQmlEngine engine;
    StubSettings controller;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    quickWindow->show();

    QVERIFY(QMetaObject::invokeMethod(
        window->findChild<QObject *>(QStringLiteral("homeEngineButton")), "clicked"));
    QCOMPARE(window->property("settingsSection").toString(), QStringLiteral("engines"));
    QVERIFY(controller.m_addedPreset.isEmpty());
    QQuickItem *ownKey = nullptr;
    QTRY_VERIFY(
        (ownKey = visibleItem(quickWindow->contentItem(), QStringLiteral("engineCategory2")))
        != nullptr);
    QVERIFY(QMetaObject::invokeMethod(ownKey, "clicked"));
    QQuickItem *deepL = nullptr;
    QTRY_VERIFY((deepL = visibleItem(quickWindow->contentItem(), QStringLiteral("addPreset-deepl")))
        != nullptr);
    QVERIFY(controller.m_addedPreset.isEmpty());
    QVERIFY(QMetaObject::invokeMethod(deepL, "clicked"));
    QCOMPARE(controller.m_addedPreset, QStringLiteral("deepl"));
    QCOMPARE(controller.m_activeProviderId, QStringLiteral("deepl"));
}

void UiSettingsQmlTest::addingEngineRequiresConfirmationWhenAutomaticSendingNeedsConsent_data() {
    QTest::addColumn<QString>("category");
    QTest::addColumn<QString>("engineId");
    QTest::addColumn<bool>("unofficial");
    QTest::newRow("public API") << QStringLiteral("engineCategory1") << QStringLiteral("mymemory")
                                << false;
    QTest::newRow("web translation")
        << QStringLiteral("engineCategory3") << QStringLiteral("youdao") << true;
}

void UiSettingsQmlTest::addingEngineRequiresConfirmationWhenAutomaticSendingNeedsConsent() {
    QFETCH(QString, category);
    QFETCH(QString, engineId);
    QFETCH(bool, unofficial);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto paths = [&] {
        const QList<QByteArray> keys {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME"};
        QList<QByteArray> previous;
        for (const QByteArray &key : keys) {
            previous.append(qgetenv(key.constData()));
            qputenv(key.constData(), temporary.path().toLocal8Bit());
        }
        const auto restoreEnvironment = qScopeGuard([&] {
            for (qsizetype i = 0; i < keys.size(); ++i) {
                if (previous[i].isNull())
                    qunsetenv(keys[i].constData());
                else
                    qputenv(keys[i].constData(), previous[i]);
            }
        });
        return core::XdgPaths::resolve();
    }();
    settings::SettingsStore store(paths);
    auto secretStore = secrets::createSecretStore(secrets::ServiceProbe {});
    ui::SettingsController controller(&store, secretStore.get(), nullptr);
    QVERIFY(controller.addPresetEngine(QStringLiteral("libretranslate"))
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(store.setAutomaticEnabled(true).ok());

    QQmlEngine engine;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    window->setProperty("settingsController", QVariant::fromValue(&controller));
    window->setProperty("showPreferences", true);
    window->setProperty("showAddEngine", true);
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    quickWindow->show();

    QQuickItem *categoryButton = nullptr;
    QTRY_VERIFY((categoryButton = visibleItem(quickWindow->contentItem(), category)) != nullptr);
    QVERIFY(QMetaObject::invokeMethod(categoryButton, "clicked"));
    QCOMPARE(visibleItem(quickWindow->contentItem(), QStringLiteral("webEngineWarning")) != nullptr,
        unofficial);
    const QString addButtonName = QStringLiteral("addPreset-") + engineId;
    QQuickItem *addButton = nullptr;
    QTRY_VERIFY((addButton = visibleItem(quickWindow->contentItem(), addButtonName)) != nullptr);
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("consentDialog"));
    QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(addButton, "clicked"));
    QTRY_VERIFY(dialog->property("visible").toBool());
    QCOMPARE(store.current().provider.providers.size(), 1);
    QObject *warning = window->findChild<QObject *>(QStringLiteral("unofficialConsentWarning"));
    QVERIFY(warning);
    QCOMPARE(warning->property("visible").toBool(), unofficial);

    QObject *cancel = window->findChild<QObject *>(QStringLiteral("cancelConsentButton"));
    QVERIFY(cancel);
    QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
    QTRY_VERIFY(!dialog->property("addingEngine").toBool());
    QCOMPARE(controller.activeProviderId(), QStringLiteral("libretranslate"));
    settings::SettingsStore reopened(paths);
    QCOMPARE(reopened.load().settings.provider.providers.size(), 1);

    QVERIFY(QMetaObject::invokeMethod(addButton, "clicked"));
    QTRY_VERIFY(dialog->property("visible").toBool());
    QObject *allow = window->findChild<QObject *>(QStringLiteral("allowConsentButton"));
    QVERIFY(allow);
    QVERIFY(QMetaObject::invokeMethod(allow, "clicked"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(controller.activeProviderId(), engineId);
    const settings::Settings saved = reopened.load().settings;
    QCOMPARE(saved.provider.providers.size(), 2);
    QCOMPARE(saved.provider.activeId, engineId);
    QVERIFY(saved.mayAutoSend());
}

void UiSettingsQmlTest::aboutUsesTheApplicationVersion_data() {
    QTest::addColumn<bool>("minimumSize");
    QTest::newRow("default window") << false;
    QTest::newRow("minimum window") << true;
}

void UiSettingsQmlTest::aboutUsesTheApplicationVersion() {
    QFETCH(bool, minimumSize);
    const QString originalVersion = QCoreApplication::applicationVersion();
    const auto restoreVersion
        = qScopeGuard([&] { QCoreApplication::setApplicationVersion(originalVersion); });
    QCoreApplication::setApplicationVersion(QStringLiteral("7.8.9"));

    QQmlEngine engine;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(SNIPLYNGO_QML_DIR "/SettingsWindow.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(quickWindow);
    if (minimumSize)
        quickWindow->resize(quickWindow->minimumSize());
    QSignalSpy frames(quickWindow, &QQuickWindow::frameSwapped);
    quickWindow->show();
    // Let the title bar upload the shared logo before rendering the About page.
    QTRY_VERIFY(!frames.isEmpty());

    QVERIFY(QMetaObject::invokeMethod(
        window->findChild<QObject *>(QStringLiteral("preferencesButton")), "clicked"));
    auto *about = visibleItem(quickWindow->contentItem(), QStringLiteral("settingsSection-about"));
    QVERIFY(about);
    frames.clear();
    QVERIFY(QMetaObject::invokeMethod(about, "clicked"));
    QTRY_VERIFY(!frames.isEmpty());
    auto *scroll = visibleItem(quickWindow->contentItem(), QStringLiteral("settingsScrollView"));
    QVERIFY(scroll);

    const auto checkText = [&](const QString &name, const QString &expected) {
        auto *item = visibleItem(quickWindow->contentItem(), name);
        QVERIFY(item);
        QCOMPARE(item->property("text").toString(), expected);
        QTRY_VERIFY2(item->width() > 0 && item->height() > 0
                && scroll->boundingRect().contains(
                    item->mapRectToItem(scroll, item->boundingRect())),
            qPrintable(name));
    };
    checkText(QStringLiteral("aboutVersion"), QStringLiteral("Version 7.8.9"));
    checkText(QStringLiteral("aboutAuthor"), QStringLiteral("WangZhiChong"));
    checkText(QStringLiteral("aboutEmail"), QStringLiteral("wangzhichong47@gmail.com"));
    checkText(QStringLiteral("aboutRepository"),
        QStringLiteral("https://github.com/WangZhiChong2003/SnipLingo"));
}

QTEST_MAIN(UiSettingsQmlTest)
#include "ui_settings_qml_test.moc"
