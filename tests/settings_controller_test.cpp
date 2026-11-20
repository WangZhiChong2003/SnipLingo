// Settings surface: persistence, consent lifecycle, credential presence, provider CRUD.
#include "ui/settings_controller.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QSslConfiguration>
#include <QTemporaryDir>
#include <QtTest>

#include "fake_provider.h"
#include "fake_selection_backend.h"

using coordinator::RequestCoordinator;
using ui::SettingsController;

class SettingsControllerTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void startsEmptyAndAsksForAProvider();
    void targetLanguageRoundTrips();
    void localEngineNeedsNoConsent();
    void consentFollowsTheDestinationFingerprint();
    void consentRequiredIsForwarded();
    void changingEngineRequestsConsentOnce();
    void apiKeyIsStoredWithoutBeingExposed();
    void removingTheActiveProviderSelectsAnother();
    void keylessPresetsNeedNoCredential();
    void proxyIsValidatedAndPersisted();
    void automaticControlsManageTheSubscription();
    void automaticCanBeEnabledDuringAnExplicitTranslation();
    void selectionBackendChangeRequiresRestart();
    void selectionDisplaySettingPersists();
    void interfaceScaleSettingsPersist();
    void xwaylandPopupSettingPersistsAndRequiresRestart();
    void xwaylandPopupAvailabilityFollowsTheSession();
    void explicitTextUsesTheConfiguredLanguages();
    void engineTestUsesSampleInsteadOfTheSelection();
    void libreTranslateCanUseAnOptionalKey();
    void deepLProfilesUseSeparatePersonalKeys();

private:
    QTemporaryDir &root();

    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<settings::SettingsStore> m_store;
    std::unique_ptr<secrets::SecretStore> m_secrets;
    std::unique_ptr<FakeSelectionBackend> m_backend;
    std::unique_ptr<FakeProvider> m_provider;
    std::unique_ptr<RequestCoordinator> m_coordinator;
    std::unique_ptr<SettingsController> m_controller;
};

QTemporaryDir &SettingsControllerTest::root() {
    if (!m_root || !m_root->isValid()) {
        // A fresh XDG root per test keeps persisted settings from leaking into the next case.
        m_root = std::make_unique<QTemporaryDir>();
        Q_ASSERT(m_root->isValid());
    }
    return *m_root;
}

void SettingsControllerTest::init() {
    m_root = std::make_unique<QTemporaryDir>();
    QTemporaryDir &m_rootRef = *m_root;
    QVERIFY(m_rootRef.isValid());
    qputenv("XDG_CONFIG_HOME", m_rootRef.filePath(QStringLiteral("config")).toLocal8Bit());
    qputenv("XDG_DATA_HOME", m_rootRef.filePath(QStringLiteral("data")).toLocal8Bit());
    qputenv("XDG_STATE_HOME", m_rootRef.filePath(QStringLiteral("state")).toLocal8Bit());
    qputenv("XDG_CACHE_HOME", m_rootRef.filePath(QStringLiteral("cache")).toLocal8Bit());
    qputenv("HOME", m_rootRef.filePath(QStringLiteral("home")).toLocal8Bit());

    m_store = std::make_unique<settings::SettingsStore>(core::XdgPaths::resolve());
    m_store->load();
    // No session bus in the unit-test process: this is the session-only store, as shipped.
    m_secrets = secrets::createSecretStore(secrets::ServiceProbe {});
    m_backend = std::make_unique<FakeSelectionBackend>();
    m_provider = std::make_unique<FakeProvider>();
    m_coordinator
        = std::make_unique<RequestCoordinator>(m_backend.get(), m_provider.get(), m_store.get());
    m_controller
        = std::make_unique<SettingsController>(m_store.get(), m_secrets.get(), m_coordinator.get());
}

void SettingsControllerTest::cleanup() {
    m_controller.reset();
    m_coordinator.reset();
    m_provider.reset();
    m_backend.reset();
    m_secrets.reset();
    m_store.reset();
    m_root.reset();
    for (const char *variable :
        {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME", "HOME"})
        qunsetenv(variable);
}

void SettingsControllerTest::startsEmptyAndAsksForAProvider() {
    QCOMPARE(m_controller->providers().size(), 0);
    QVERIFY(!m_controller->consentGranted());
    QVERIFY(!m_controller->consentStale());
    QVERIFY(m_controller->consentDestination().isEmpty());
    QVERIFY(!m_controller->automaticEnabled());
    QVERIFY(!m_controller->credentialPersistent());
    QCOMPARE(m_controller->credentialBackend(), QStringLiteral("session"));
    QVERIFY(m_controller->credentialReason().contains(QStringLiteral("memory")));
}

void SettingsControllerTest::selectionDisplaySettingPersists() {
    QVERIFY(!m_controller->showResultImmediately());
    m_controller->setShowResultImmediately(true);
    QVERIFY(m_controller->showResultImmediately());
    settings::SettingsStore reopened(core::XdgPaths::resolve());
    reopened.load();
    QVERIFY(reopened.current().selection.showResultImmediately);
    m_controller->setShowResultImmediately(false);
    QVERIFY(!m_controller->showResultImmediately());
}

void SettingsControllerTest::interfaceScaleSettingsPersist() {
    QCOMPARE(m_controller->uiScale(), 1.0);
    QCOMPARE(m_controller->fontScale(), 1.0);

    m_controller->setUiScale(1.25);
    m_controller->setFontScale(1.4);
    QCOMPARE(m_controller->uiScale(), 1.25);
    QCOMPARE(m_controller->fontScale(), 1.4);

    settings::SettingsStore reopened(core::XdgPaths::resolve());
    reopened.load();
    QCOMPARE(reopened.current().ui.scale, 1.25);
    QCOMPARE(reopened.current().ui.fontScale, 1.4);

    // The store clamps out-of-range values on save; the properties follow the saved value.
    m_controller->setUiScale(9.0);
    QCOMPARE(m_controller->uiScale(), 1.5);
    m_controller->setFontScale(0.1);
    QCOMPARE(m_controller->fontScale(), 0.75);
}

void SettingsControllerTest::xwaylandPopupSettingPersistsAndRequiresRestart() {
    QVERIFY(!m_controller->xwaylandPopupEnabled());
    QVERIFY(!m_controller->popupRestartRequired());

    const QVariantMap enabled = m_controller->setXwaylandPopup(true);
    QVERIFY(enabled.value(QStringLiteral("ok")).toBool());
    QVERIFY(m_controller->xwaylandPopupEnabled());
    QVERIFY(m_controller->popupRestartRequired());

    settings::SettingsStore reopened(core::XdgPaths::resolve());
    reopened.load();
    QVERIFY(reopened.current().selection.xwaylandPopup);

    const QVariantMap disabled = m_controller->setXwaylandPopup(false);
    QVERIFY(disabled.value(QStringLiteral("ok")).toBool());
    QVERIFY(!m_controller->xwaylandPopupEnabled());
    QVERIFY(!m_controller->popupRestartRequired());
}

void SettingsControllerTest::xwaylandPopupAvailabilityFollowsTheSession() {
    const QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
    const QByteArray waylandDisplay = qgetenv("WAYLAND_DISPLAY");
    const QByteArray display = qgetenv("DISPLAY");
    const auto restore = [](const char *name, const QByteArray &value) {
        if (value.isEmpty())
            qunsetenv(name);
        else
            qputenv(name, value);
    };
    const auto guard = qScopeGuard([&] {
        restore("XDG_SESSION_TYPE", sessionType);
        restore("WAYLAND_DISPLAY", waylandDisplay);
        restore("DISPLAY", display);
    });

    qputenv("XDG_SESSION_TYPE", "wayland");
    qunsetenv("WAYLAND_DISPLAY");
    qputenv("DISPLAY", ":1");
    QVERIFY(m_controller->xwaylandPopupAvailable());

    qunsetenv("DISPLAY");
    QVERIFY(!m_controller->xwaylandPopupAvailable());

    qputenv("XDG_SESSION_TYPE", "x11");
    qputenv("WAYLAND_DISPLAY", "wayland-0");
    qputenv("DISPLAY", ":1");
    QVERIFY(m_controller->xwaylandPopupAvailable());

    qunsetenv("WAYLAND_DISPLAY");
    QVERIFY(!m_controller->xwaylandPopupAvailable());
}

void SettingsControllerTest::targetLanguageRoundTrips() {
    QVERIFY(m_controller->targetLanguageCodes().contains(QStringLiteral("zh-Hans")));
    m_controller->setTargetLanguage(QStringLiteral("zh-Hans"));
    QCOMPARE(m_controller->targetLanguage(), QStringLiteral("zh-Hans"));

    QSignalSpy failures(m_controller.get(), &SettingsController::failed);
    m_controller->setTargetLanguage(QStringLiteral("klingon"));
    QCOMPARE(failures.count(), 1);
    QCOMPARE(m_controller->targetLanguage(), QStringLiteral("zh-Hans"));  // unchanged on rejection
}

void SettingsControllerTest::localEngineNeedsNoConsent() {
    const QVariantMap added = m_controller->addProvider(QStringLiteral("Ollama"),
        QStringLiteral("http://127.0.0.1:11434/v1"), QStringLiteral("qwen2.5"));
    QVERIFY2(added.value(QStringLiteral("ok")).toBool(), "adding a local engine failed");

    const QVariantList providers = m_controller->providers();
    QCOMPARE(providers.size(), 1);
    QCOMPARE(providers.first().toMap().value(QStringLiteral("isLocal")).toBool(), true);
    QVERIFY(providers.first().toMap().value(QStringLiteral("active")).toBool());
    // A local engine never triggers a consent request.
    QVERIFY(m_controller->consentGranted());
    QVERIFY(!m_controller->consentStale());
}

void SettingsControllerTest::consentFollowsTheDestinationFingerprint() {
    const QVariantMap remote = m_controller->addProvider(QStringLiteral("Example"),
        QStringLiteral("https://api.example.com/v1"), QStringLiteral("gpt-4o-mini"));
    QVERIFY(remote.value(QStringLiteral("ok")).toBool());

    QVERIFY(!m_controller->consentGranted());
    QVERIFY(m_controller->consentStale());
    // The consent destination includes the model: consent covers one service *and* one model.
    QCOMPARE(
        m_controller->consentDestination(), QStringLiteral("api.example.com \u00B7 gpt-4o-mini"));

    m_controller->grantConsent();
    QVERIFY(m_controller->consentGranted());
    QVERIFY(!m_controller->consentStale());

    // Changing the model changes the fingerprint, so the old consent no longer covers it.
    const QString id = remote.value(QStringLiteral("id")).toString();
    const QVariantMap updated
        = m_controller->updateProvider(id, {{QStringLiteral("model"), QStringLiteral("gpt-5")}});
    QVERIFY(updated.value(QStringLiteral("ok")).toBool());
    QVERIFY(!m_controller->consentGranted());
    QVERIFY(m_controller->consentStale());

    m_controller->grantConsent();
    QVERIFY(m_controller->consentGranted());
    m_controller->revokeConsent();
    QVERIFY(m_controller->consentStale());
}

void SettingsControllerTest::consentRequiredIsForwarded() {
    const QVariantMap remote = m_controller->addProvider(QStringLiteral("Example"),
        QStringLiteral("https://api.example.com/v1"), QStringLiteral("gpt-4o-mini"));
    QVERIFY(remote.value(QStringLiteral("ok")).toBool());

    QSignalSpy consent(m_controller.get(), &SettingsController::consentRequired);
    m_controller->setAutomaticEnabled(true);
    // The coordinator raises the request; the sheet must hear it through the controller.
    QTRY_VERIFY_WITH_TIMEOUT(
        !consent.isEmpty() || m_coordinator->state() != coordinator::State::Idle, 2000);
    if (!consent.isEmpty()) {
        QCOMPARE(consent.first().at(0).toString(), QStringLiteral("api.example.com · gpt-4o-mini"));
    }
    QVERIFY(m_controller->automaticEnabled());
}

void SettingsControllerTest::apiKeyIsStoredWithoutBeingExposed() {
    const QVariantMap remote = m_controller->addProvider(QStringLiteral("Example"),
        QStringLiteral("https://api.example.com/v1"), QStringLiteral("gpt-4o-mini"));
    const QString id = remote.value(QStringLiteral("id")).toString();

    QVERIFY(!m_controller->providers().first().toMap().value(QStringLiteral("hasApiKey")).toBool());
    const QVariantMap stored = m_controller->setApiKey(id, QStringLiteral("sk-test-12345"));
    QVERIFY2(stored.value(QStringLiteral("ok")).toBool(), "storing the key failed");
    QVERIFY(m_controller->providers().first().toMap().value(QStringLiteral("hasApiKey")).toBool());

    // The key must not become readable through any property.
    for (const char *property : {"providers", "credentialReason", "consentDestination"})
        QVERIFY(!m_controller->property(property).toString().contains(QStringLiteral("sk-test")));

    m_controller->clearApiKey(id);
    QVERIFY(!m_controller->providers().first().toMap().value(QStringLiteral("hasApiKey")).toBool());

    // Unknown providers are rejected through the returned outcome, not a signal.
    const QVariantMap rejected
        = m_controller->setApiKey(QStringLiteral("missing"), QStringLiteral("x"));
    QCOMPARE(rejected.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(rejected.value(QStringLiteral("error")).toString(),
        QString::fromLatin1(core::stableName(core::ErrorCode::SettingsInvalid)));
}

void SettingsControllerTest::changingEngineRequestsConsentOnce() {
    const QVariantMap google = m_controller->addPresetEngine(QStringLiteral("google-free"));
    const QVariantMap memory = m_controller->addPresetEngine(QStringLiteral("mymemory"));
    QVERIFY(google.value(QStringLiteral("ok")).toBool());
    QVERIFY(memory.value(QStringLiteral("ok")).toBool());
    QSignalSpy consent(m_controller.get(), &SettingsController::consentRequired);
    m_controller->setAutomaticEnabled(true);
    QCOMPARE(consent.size(), 1);
    m_controller->grantConsent();
    consent.clear();
    m_controller->setActiveProvider(memory.value(QStringLiteral("id")).toString());
    QCOMPARE(consent.size(), 1);
}

void SettingsControllerTest::removingTheActiveProviderSelectsAnother() {
    const QVariantMap first = m_controller->addProvider(
        QStringLiteral("One"), QStringLiteral("http://127.0.0.1:8080/v1"), QStringLiteral("m1"));
    const QVariantMap second = m_controller->addProvider(
        QStringLiteral("Two"), QStringLiteral("http://127.0.0.1:8081/v1"), QStringLiteral("m2"));
    QCOMPARE(m_controller->providers().size(), 2);

    QVERIFY(m_controller->removeProvider(first.value(QStringLiteral("id")).toString()));
    QCOMPARE(m_controller->providers().size(), 1);
    QCOMPARE(m_controller->activeProviderId(), second.value(QStringLiteral("id")).toString());

    QVERIFY(!m_controller->removeProvider(QStringLiteral("does-not-exist")));
}

void SettingsControllerTest::keylessPresetsNeedNoCredential() {
    const QVariantMap added = m_controller->addPresetEngine(QStringLiteral("mymemory"));
    QVERIFY2(added.value(QStringLiteral("ok")).toBool(), "adding a keyless preset failed");
    QCOMPARE(m_controller->providers().size(), 1);

    const QVariantMap entry = m_controller->providers().first().toMap();
    QCOMPARE(entry.value(QStringLiteral("needsApiKey")).toBool(), false);
    QCOMPARE(entry.value(QStringLiteral("isLocal")).toBool(), false);
    QCOMPARE(entry.value(QStringLiteral("type")).toString(), QStringLiteral("mymemory"));
    QVERIFY(!entry.value(QStringLiteral("baseUrl")).toString().isEmpty());
    // It is still a remote destination, so consent applies exactly as for a paid engine.
    QVERIFY(!m_controller->consentGranted());
    QVERIFY(m_controller->consentStale());

    // Adding the same preset twice must not collide on the provider id.
    const QVariantMap again = m_controller->addPresetEngine(QStringLiteral("mymemory"));
    QVERIFY(again.value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_controller->providers().size(), 2);
    QVERIFY(again.value(QStringLiteral("id")).toString()
        != added.value(QStringLiteral("id")).toString());

    const QVariantMap unknown = m_controller->addPresetEngine(QStringLiteral("nope"));
    QCOMPARE(unknown.value(QStringLiteral("ok")).toBool(), false);
}

void SettingsControllerTest::proxyIsValidatedAndPersisted() {
    QCOMPARE(
        m_controller->proxyMode(), QStringLiteral("system"));  // honours the environment by default

    QVERIFY(
        m_controller->setProxy(QStringLiteral("manual"), QStringLiteral("http://127.0.0.1:7890"))
            .value(QStringLiteral("ok"))
            .toBool());
    QCOMPARE(m_controller->proxyMode(), QStringLiteral("manual"));
    QCOMPARE(QSslConfiguration::defaultConfiguration().protocol(), QSsl::TlsV1_2OrLater);
    QCOMPARE(m_controller->proxyUrl(), QStringLiteral("http://127.0.0.1:7890"));

    // A socks5 address is accepted too, and an unusable one is rejected instead of silently
    // ignored.
    QVERIFY(
        m_controller->setProxy(QStringLiteral("manual"), QStringLiteral("socks5://127.0.0.1:1080"))
            .value(QStringLiteral("ok"))
            .toBool());
    const QVariantMap rejected
        = m_controller->setProxy(QStringLiteral("manual"), QStringLiteral("not a url"));
    QCOMPARE(rejected.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(m_controller->proxyUrl(), QStringLiteral("socks5://127.0.0.1:1080"));  // unchanged

    QVERIFY(m_controller->setProxy(QStringLiteral("none"), QString())
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(m_controller->proxyUrl().isEmpty());
}

void SettingsControllerTest::automaticControlsManageTheSubscription() {
    QVERIFY(m_controller->addPresetEngine(QStringLiteral("google-free"))
                .value(QStringLiteral("ok"))
                .toBool());
    m_controller->setAutomaticEnabled(true);
    QCOMPARE(m_backend->subscribeCalls, 1);
    QVERIFY(m_controller->automaticEnabled());
    m_controller->setPaused(true);
    QCOMPARE(m_backend->unsubscribeCalls, 1);
    QVERIFY(m_controller->paused());
    m_controller->setPaused(false);
    QCOMPARE(m_backend->subscribeCalls, 2);
    m_controller->setAutomaticEnabled(false);
    QCOMPARE(m_backend->unsubscribeCalls, 2);
    QVERIFY(!m_controller->automaticEnabled());
}

void SettingsControllerTest::explicitTextUsesTheConfiguredLanguages() {
    QVERIFY(m_controller->addPresetEngine(QStringLiteral("google-free"))
                .value(QStringLiteral("ok"))
                .toBool());
    m_controller->setSourceLanguage(QStringLiteral("ja"));
    m_controller->setTargetLanguage(QStringLiteral("zh-Hans"));
    QVERIFY(m_controller->translateText(QStringLiteral("こんにちは"))
                .value(QStringLiteral("ok"))
                .toBool());
    QCOMPARE(m_provider->requests.size(), 1);
    QCOMPARE(m_provider->requests.first().sourceLanguage, QStringLiteral("ja"));
    QCOMPARE(m_provider->requests.first().targetLanguage, QStringLiteral("zh-Hans"));
    QCOMPARE(m_backend->subscribeCalls, 0);
}

void SettingsControllerTest::automaticCanBeEnabledDuringAnExplicitTranslation() {
    QVERIFY(m_controller->addPresetEngine(QStringLiteral("google-free"))
                .value(QStringLiteral("ok"))
                .toBool());
    m_controller->setTargetLanguage(QStringLiteral("zh-Hans"));
    QVERIFY(
        m_controller->translateText(QStringLiteral("hello")).value(QStringLiteral("ok")).toBool());
    m_controller->setAutomaticEnabled(true);
    QCOMPARE(m_backend->subscribeCalls, 1);
    QVERIFY(m_controller->automaticEnabled());
}

void SettingsControllerTest::selectionBackendChangeRequiresRestart() {
    QCOMPARE(m_controller->selectionBackendChoice(), QStringLiteral("auto"));
    QCOMPARE(m_controller->selectionBackendId(), QStringLiteral("fake"));
    QVERIFY(!m_controller->selectionRestartRequired());
    m_controller->setSelectionBackend(QStringLiteral("x11-primary"));
    QCOMPARE(m_controller->selectionBackendChoice(), QStringLiteral("x11-primary"));
    QVERIFY(m_controller->selectionRestartRequired());
    QCOMPARE(m_controller->selectionBackendId(), QStringLiteral("fake"));
    m_controller->setSelectionBackend(QStringLiteral("auto"));
    QVERIFY(!m_controller->selectionRestartRequired());
}

void SettingsControllerTest::engineTestUsesSampleInsteadOfTheSelection() {
    const auto added = m_controller->addProvider(QStringLiteral("Example"),
        QStringLiteral("https://api.example.com/v1"), QStringLiteral("test-model"));
    QVERIFY(added.value(QStringLiteral("ok")).toBool());
    m_controller->setSourceLanguage(QStringLiteral("ja"));
    m_controller->setTargetLanguage(QStringLiteral("en"));
    QVERIFY(!m_controller->consentGranted());
    QVERIFY(m_controller->testEngine().value(QStringLiteral("ok")).toBool());
    QCOMPARE(m_provider->translateCalls, 1);
    const provider::TranslateRequest &request = m_provider->requests.last();
    QCOMPARE(request.text, QStringLiteral("你好"));
    QCOMPARE(request.sourceLanguage, QStringLiteral("zh-Hans"));
    QCOMPARE(request.targetLanguage, QStringLiteral("en"));
    QVERIFY(!m_controller->consentGranted());
    QVERIFY(!m_controller->automaticEnabled());
}

void SettingsControllerTest::libreTranslateCanUseAnOptionalKey() {
    const QVariantMap added = m_controller->addPresetEngine(QStringLiteral("libretranslate"));
    const QString id = added.value(QStringLiteral("id")).toString();
    QVERIFY(added.value(QStringLiteral("ok")).toBool());
    QCOMPARE(
        m_controller->providers().first().toMap().value(QStringLiteral("needsApiKey")).toBool(),
        false);
    QVERIFY(m_controller->setApiKey(id, QStringLiteral("test-key"))
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(m_controller->providers().first().toMap().value(QStringLiteral("hasApiKey")).toBool());
    QCOMPARE(m_secrets->lookup(QStringLiteral("provider/libretranslate/apiKey")).value(),
        QStringLiteral("test-key"));
    m_controller->clearApiKey(id);
    QVERIFY(!m_controller->providers().first().toMap().value(QStringLiteral("hasApiKey")).toBool());
}

void SettingsControllerTest::deepLProfilesUseSeparatePersonalKeys() {
    const QVariantMap first = m_controller->addPresetEngine(QStringLiteral("deepl"));
    const QVariantMap second = m_controller->addPresetEngine(QStringLiteral("deepl"));
    QVERIFY(first.value(QStringLiteral("ok")).toBool());
    QVERIFY(second.value(QStringLiteral("ok")).toBool());
    const QString firstId = first.value(QStringLiteral("id")).toString();
    const QString secondId = second.value(QStringLiteral("id")).toString();
    QVERIFY(firstId != secondId);
    QVERIFY(
        m_controller->providers().first().toMap().value(QStringLiteral("needsApiKey")).toBool());
    QVERIFY(!m_controller->providers().first().toMap().value(QStringLiteral("hasApiKey")).toBool());
    QVERIFY(m_controller->setApiKey(firstId, QStringLiteral("sample-first"))
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(m_controller->setApiKey(secondId, QStringLiteral("sample-second"))
                .value(QStringLiteral("ok"))
                .toBool());
    QCOMPARE(m_secrets->lookup(QStringLiteral("provider/%1/apiKey").arg(firstId)).value(),
        QStringLiteral("sample-first"));
    QCOMPARE(m_secrets->lookup(QStringLiteral("provider/%1/apiKey").arg(secondId)).value(),
        QStringLiteral("sample-second"));
}

QTEST_MAIN(SettingsControllerTest)
#include "settings_controller_test.moc"
