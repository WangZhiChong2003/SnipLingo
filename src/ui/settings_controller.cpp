#include "ui/settings_controller.h"

#include "app/network.h"

#include <QRegularExpression>

#include <utility>

#include "provider/simple_translate_provider.h"

namespace ui {
namespace {

// Offered targets. Codes only: the display name belongs in QML so it can be translated.
const QStringList kTargetLanguages = {
    QStringLiteral("en"),
    QStringLiteral("zh-Hans"),
    QStringLiteral("zh-Hant"),
    QStringLiteral("ja"),
    QStringLiteral("ko"),
    QStringLiteral("de"),
    QStringLiteral("fr"),
    QStringLiteral("es"),
    QStringLiteral("ru"),
    QStringLiteral("pt"),
    QStringLiteral("it"),
    QStringLiteral("ar"),
};

QVariantMap failure(const core::Error &error) {
    QVariantMap result;
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("error"), QString::fromLatin1(core::stableName(error.code)));
    result.insert(QStringLiteral("detail"), error.detail);
    return result;
}

QVariantMap success(const QString &id = {}) {
    QVariantMap result;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("id"), id);
    return result;
}

bool usesUnofficialEndpoint(const QString &type) {
    return type == QLatin1String("google-free") || type == QLatin1String("bing-free")
        || type == QLatin1String("youdao") || type == QLatin1String("huoshanweb");
}

QString engineDisplayName(const settings::ProviderConfig &config) {
    // Recognize the old web preset names while preserving user-defined labels.
    const auto suffix = QLatin1String(" (unofficial)");
    if (config.label.endsWith(suffix) && usesUnofficialEndpoint(config.type)) {
        const QString name = provider::simplePreset(config.type).label;
        if (config.label == name + suffix)
            return name;
    }
    return config.label;
}

}  // namespace

SettingsController::SettingsController(settings::SettingsStore *store,
    secrets::SecretStore *secrets, coordinator::RequestCoordinator *coordinator, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_secrets(secrets)
    , m_coordinator(coordinator)
    , m_initialSelectionBackend(store->current().selection.backend)
    , m_initialXwaylandPopup(store->current().selection.xwaylandPopup) {
    Q_ASSERT(m_store != nullptr);
    Q_ASSERT(m_secrets != nullptr);

    connect(m_store, &settings::SettingsStore::changed, this, &SettingsController::refresh);
    if (m_coordinator != nullptr) {
        connect(m_coordinator, &coordinator::RequestCoordinator::stateChanged, this,
            [this] { emit observationChanged(); });
        connect(m_coordinator, &coordinator::RequestCoordinator::selectionTestChanged, this,
            &SettingsController::selectionTestChanged);
        connect(m_coordinator, &coordinator::RequestCoordinator::consentRequired, this,
            &SettingsController::consentRequired);
        connect(m_coordinator, &coordinator::RequestCoordinator::notice, this,
            &SettingsController::reportError);
    }
    refresh();
}

void SettingsController::refresh() {
    // Only a presence check: the key value itself must never be copied into a property.
    m_presentKeyRefs.clear();
    for (const settings::ProviderConfig &provider : m_store->current().provider.providers) {
        if (provider.apiKeyRef.isEmpty())
            continue;
        if (m_secrets->lookup(provider.apiKeyRef).ok())
            m_presentKeyRefs.append(provider.apiKeyRef);
    }
    emit changed();
}

bool SettingsController::automaticEnabled() const {
    return m_store->current().selection.automaticEnabled;
}

bool SettingsController::paused() const {
    return m_store->current().selection.paused;
}

bool SettingsController::showResultImmediately() const {
    return m_store->current().selection.showResultImmediately;
}

bool SettingsController::xwaylandPopupAvailable() const {
    return waylandSession() && !qEnvironmentVariableIsEmpty("DISPLAY");
}

bool SettingsController::xwaylandPopupEnabled() const {
    return m_store->current().selection.xwaylandPopup;
}

bool SettingsController::popupRestartRequired() const {
    return m_initialXwaylandPopup != xwaylandPopupEnabled();
}

qreal SettingsController::uiScale() const {
    return m_store->current().ui.scale;
}

QString SettingsController::uiLanguage() const {
    return m_store->current().ui.language;
}

qreal SettingsController::fontScale() const {
    return m_store->current().ui.fontScale;
}

bool SettingsController::observationActive() const {
    return m_coordinator != nullptr && m_coordinator->observationActive();
}

QString SettingsController::selectionTestState() const {
    using coordinator::SelectionTest;
    const auto state = m_coordinator ? m_coordinator->selectionTest() : SelectionTest::Idle;
    switch (state) {
    case SelectionTest::Waiting:
        return QStringLiteral("waiting");
    case SelectionTest::Readable:
        return QStringLiteral("readable");
    case SelectionTest::TimedOut:
        return QStringLiteral("timed-out");
    case SelectionTest::Unavailable:
        return QStringLiteral("unavailable");
    case SelectionTest::Idle:
        return QString();
    }
    return {};
}

void SettingsController::startSelectionTest() {
    if (m_coordinator != nullptr)
        m_coordinator->startSelectionTest();
}

void SettingsController::cancelSelectionTest() {
    if (m_coordinator != nullptr)
        m_coordinator->cancelSelectionTest();
}

void SettingsController::requestConsent() {
    emit consentRequired(consentDestination(), !m_store->current().consents.isEmpty());
}

bool SettingsController::waylandSession() const {
    return qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")
        || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
}

QString SettingsController::selectionBackendId() const {
    return m_coordinator != nullptr ? m_coordinator->selectionBackendId() : QString();
}

QString SettingsController::selectionBackendChoice() const {
    return m_store->current().selection.backend;
}

bool SettingsController::selectionRestartRequired() const {
    return selectionBackendChoice() != m_initialSelectionBackend;
}

QString SettingsController::sourceLanguage() const {
    return m_store->current().language.source;
}

QString SettingsController::targetLanguage() const {
    return m_store->current().language.target;
}

QStringList SettingsController::targetLanguageCodes() const {
    return kTargetLanguages;
}

QString SettingsController::activeProviderId() const {
    return m_store->current().provider.activeId;
}

QVariantList SettingsController::providers() const {
    QVariantList list;
    for (const settings::ProviderConfig &provider : m_store->current().provider.providers) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), provider.id);
        entry.insert(QStringLiteral("label"), engineDisplayName(provider));
        entry.insert(QStringLiteral("host"), provider.host());
        entry.insert(QStringLiteral("baseUrl"), provider.baseUrl);
        entry.insert(QStringLiteral("model"), provider.model);
        entry.insert(QStringLiteral("isLocal"), provider.isLocal());
        entry.insert(QStringLiteral("type"), provider.type);
        entry.insert(QStringLiteral("unofficial"), usesUnofficialEndpoint(provider.type));
        const bool libre = provider.type == QLatin1String("libretranslate");
        const bool deepL = provider.type == QLatin1String("deepl");
        entry.insert(QStringLiteral("needsApiKey"),
            deepL || (!libre && !provider.isLocal() && !provider.apiKeyRef.isEmpty()));
        entry.insert(
            QStringLiteral("supportsApiKey"), libre || deepL || !provider.apiKeyRef.isEmpty());
        entry.insert(QStringLiteral("active"), provider.id == activeProviderId());
        entry.insert(QStringLiteral("hasApiKey"), m_presentKeyRefs.contains(provider.apiKeyRef));
        list.append(entry);
    }
    return list;
}

bool SettingsController::consentGranted() const {
    const settings::Settings &current = m_store->current();
    const settings::ProviderConfig *provider = current.activeProvider();
    if (provider == nullptr)
        return false;
    // Local engines never need consent, so nothing is pending for them.
    return provider->isLocal() || current.hasConsentFor(provider->fingerprint());
}

bool SettingsController::consentStale() const {
    const settings::Settings &current = m_store->current();
    const settings::ProviderConfig *provider = current.activeProvider();
    if (provider == nullptr || provider->isLocal())
        return false;  // local engines never need consent
    return !current.hasConsentFor(provider->fingerprint());
}

QString SettingsController::consentDestination() const {
    const settings::ProviderConfig *provider = m_store->current().activeProvider();
    return provider == nullptr ? QString() : provider->destinationLabel();
}

bool SettingsController::credentialPersistent() const {
    return m_secrets->availability().availability == secrets::Availability::Available;
}

QString SettingsController::credentialBackend() const {
    return m_secrets->availability().backend;
}

QString SettingsController::credentialReason() const {
    return m_secrets->availability().reason;
}

QString SettingsController::proxyMode() const {
    return m_store->current().network.proxy;
}

QString SettingsController::proxyUrl() const {
    return m_store->current().network.proxyUrl;
}

QString SettingsController::proxyEffective() const {
    return app::describeProxy();
}

QVariantMap SettingsController::setProxy(const QString &mode, const QString &url) {
    settings::Settings next = m_store->current();
    next.network.proxy = mode;
    next.network.proxyUrl = mode == QLatin1String("manual") ? url : QString();

    // Validate before saving: the store would otherwise persist a reset value while rejecting the
    // input.
    QStringList issues;
    settings::validate(next, &issues);
    if (issues.contains(QStringLiteral("network.proxy"))
        || issues.contains(QStringLiteral("network.proxyUrl")))
        return failure({core::ErrorCode::SettingsInvalid,
            QStringLiteral("the proxy address is not usable (http://, https:// or socks5://)")});

    const core::Result<void> saved = m_store->save(next);
    if (!saved.ok())
        return failure(saved.error());
    app::applyNetworkSettings(m_store->current());
    refresh();
    return success();
}

void SettingsController::restoreAutomaticSelection() {
    if (automaticEnabled() && !paused())
        setAutomaticEnabled(true, false);
}

void SettingsController::setAutomaticEnabled(bool enabled) {
    setAutomaticEnabled(enabled, true);
}

void SettingsController::setAutomaticEnabled(bool enabled, bool askForConsent) {
    if (m_coordinator != nullptr && !enabled) {
        m_coordinator->disableAutomatic();
        return;
    }
    const core::Result<void> result = m_coordinator != nullptr
        ? m_coordinator->enableAutomatic()
        : m_store->setAutomaticEnabled(enabled);
    if (!result.ok()) {
        if (m_store->current().selection.automaticEnabled)
            m_store->setAutomaticEnabled(false);
        reportError(result.error());
        emit changed();
    } else if (askForConsent && m_coordinator != nullptr) {
        m_coordinator->recheckConsent();
    }
}

void SettingsController::setPaused(bool paused) {
    const core::Result<void> result
        = m_coordinator != nullptr ? m_coordinator->setPaused(paused) : m_store->setPaused(paused);
    if (!result.ok()) {
        reportError(result.error());
        emit changed();
    }
}

void SettingsController::setShowResultImmediately(bool enabled) {
    settings::Settings next = m_store->current();
    next.selection.showResultImmediately = enabled;
    const core::Result<void> result = m_store->save(next);
    if (!result.ok())
        reportError(result.error());
}

QVariantMap SettingsController::setXwaylandPopup(bool enabled) {
    settings::Settings next = m_store->current();
    next.selection.xwaylandPopup = enabled;
    const core::Result<void> result = m_store->save(next);
    return result.ok() ? success() : failure(result.error());
}

void SettingsController::setUiLanguage(const QString &code) {
    if (code != QLatin1String("system") && code != QLatin1String("en")
        && code != QLatin1String("zh-Hans")) {
        reportError({core::ErrorCode::SettingsInvalid, QStringLiteral("unsupported ui language")});
        return;
    }
    settings::Settings next = m_store->current();
    next.ui.language = code;
    const core::Result<void> result = m_store->save(next);
    if (!result.ok())
        reportError(result.error());
}

void SettingsController::setUiScale(qreal scale) {
    settings::Settings next = m_store->current();
    next.ui.scale = scale;
    // The store clamps to the supported range; the applied value follows the validated one.
    const core::Result<void> result = m_store->save(next);
    if (!result.ok())
        reportError(result.error());
}

void SettingsController::setFontScale(qreal scale) {
    settings::Settings next = m_store->current();
    next.ui.fontScale = scale;
    const core::Result<void> result = m_store->save(next);
    if (!result.ok())
        reportError(result.error());
}

void SettingsController::setSelectionBackend(const QString &backend) {
    if (backend != QLatin1String("auto") && backend != QLatin1String("atspi")
        && backend != QLatin1String("x11-primary")) {
        reportError(
            {core::ErrorCode::SettingsInvalid, QStringLiteral("unknown selection backend")});
        return;
    }
    settings::Settings next = m_store->current();
    next.selection.backend = backend;
    const core::Result<void> result = m_store->save(next);
    if (!result.ok())
        reportError(result.error());
}

void SettingsController::setSourceLanguage(const QString &code) {
    if (code != QLatin1String("auto") && !kTargetLanguages.contains(code)) {
        reportError(
            {core::ErrorCode::SettingsInvalid, QStringLiteral("unsupported source language")});
        return;
    }
    settings::Settings next = m_store->current();
    next.language.source = code;
    const core::Result<void> result = m_store->save(next);
    if (!result.ok())
        reportError(result.error());
}

QVariantMap SettingsController::translateText(const QString &text) {
    if (m_coordinator == nullptr)
        return failure({core::ErrorCode::Internal, QStringLiteral("translation is unavailable")});
    const core::Result<void> result
        = m_coordinator->submitExplicitText(text, coordinator::Origin::ExplicitPaste);
    return result.ok() ? success() : failure(result.error());
}

QVariantMap SettingsController::testEngine() {
    if (m_coordinator == nullptr)
        return failure({core::ErrorCode::Internal, QStringLiteral("translation is unavailable")});
    const QString sample = targetLanguage() == QLatin1String("en") ? QStringLiteral("你好")
                                                                   : QStringLiteral("hello");
    const auto result = m_coordinator->submitExplicitText(sample, coordinator::Origin::EngineTest);
    return result.ok() ? success() : failure(result.error());
}

void SettingsController::setTargetLanguage(const QString &code) {
    if (!kTargetLanguages.contains(code)) {
        reportError(
            {core::ErrorCode::SettingsInvalid, QStringLiteral("unsupported target language")});
        return;
    }
    settings::Settings next = m_store->current();
    next.language.target = code;
    const core::Result<void> result = m_store->save(next);
    if (!result.ok())
        reportError(result.error());
}

void SettingsController::setActiveProvider(const QString &id) {
    const core::Result<void> result = m_store->setActiveProvider(id);
    if (!result.ok()) {
        reportError(result.error());
        return;
    }
    if (m_coordinator != nullptr && automaticEnabled() && !paused()) {
        const core::Result<void> recheck = m_coordinator->recheckConsent();
        if (!recheck.ok())
            reportError(recheck.error());
    }
}

const settings::ProviderConfig *SettingsController::findProvider(const QString &id) const {
    for (const settings::ProviderConfig &provider : m_store->current().provider.providers) {
        if (provider.id == id)
            return &provider;
    }
    return nullptr;
}

QString SettingsController::uniqueProviderId(const QString &label) const {
    QString base = label.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    while (base.startsWith(QLatin1Char('-')) || base.endsWith(QLatin1Char('-')))
        base = base.startsWith(QLatin1Char('-')) ? base.mid(1) : base.chopped(1);
    if (base.isEmpty())
        base = QStringLiteral("provider");

    QString candidate = base;
    int suffix = 2;
    while (findProvider(candidate) != nullptr)
        candidate = QStringLiteral("%1-%2").arg(base).arg(suffix++);
    return candidate;
}

QVariantMap SettingsController::addProvider(
    const QString &label, const QString &baseUrl, const QString &model) {
    settings::ProviderConfig provider;
    provider.id = uniqueProviderId(label.isEmpty() ? baseUrl : label);
    provider.label = label.isEmpty() ? provider.id : label;
    provider.baseUrl = baseUrl;
    provider.model = model;
    provider.apiKeyRef = QStringLiteral("provider/%1/apiKey").arg(provider.id);
    return addEngine(provider);
}

QVariantMap SettingsController::addEngine(
    const settings::ProviderConfig &provider, bool consentApproved) {
    settings::Settings next = m_store->current();
    next.provider.providers.append(provider);
    if (next.provider.activeId.isEmpty() || consentApproved)
        next.provider.activeId = provider.id;

    QStringList issues;
    settings::validate(next, &issues);
    if (issues.contains(QStringLiteral("provider.baseUrl"))
        || issues.contains(QStringLiteral("provider.baseUrl(http-remote)")))
        return failure({core::ErrorCode::SettingsInvalid,
            QStringLiteral("use HTTPS, or HTTP for a local address")});
    if (issues.contains(QStringLiteral("provider.id")))
        return failure(
            {core::ErrorCode::SettingsInvalid, QStringLiteral("invalid or duplicate engine ID")});

    if (consentApproved) {
        // Publish the new engine and its grant in the same settings update.
        next.recordConsent(provider);
    } else if (next.selection.automaticEnabled && !next.selection.paused && !provider.isLocal()
        && !next.hasConsentFor(provider.fingerprint())) {
        m_pendingEngine = provider;
        return {{QStringLiteral("ok"), true}, {QStringLiteral("requiresConsent"), true},
            {QStringLiteral("destination"), provider.destinationLabel()},
            {QStringLiteral("unofficial"), usesUnofficialEndpoint(provider.type)}};
    }

    const core::Result<void> saved = m_store->save(next);
    if (!saved.ok())
        return failure(saved.error());
    return success(provider.id);
}

core::Result<void> SettingsController::applyProviderChange(
    const QString &id, const QVariantMap &fields) {
    const settings::ProviderConfig *provider = findProvider(id);
    if (provider == nullptr)
        return core::Error {core::ErrorCode::SettingsInvalid, QStringLiteral("unknown provider")};
    if (fields.isEmpty())
        return {};

    settings::Settings next = m_store->current();
    for (settings::ProviderConfig &candidate : next.provider.providers) {
        if (candidate.id != id)
            continue;
        if (fields.contains(QStringLiteral("label")))
            candidate.label = fields.value(QStringLiteral("label")).toString();
        if (fields.contains(QStringLiteral("baseUrl")))
            candidate.baseUrl = fields.value(QStringLiteral("baseUrl")).toString();
        if (fields.contains(QStringLiteral("model")))
            candidate.model = fields.value(QStringLiteral("model")).toString();
    }
    QStringList issues;
    settings::validate(next, &issues);
    if (issues.contains(QStringLiteral("provider.baseUrl"))
        || issues.contains(QStringLiteral("provider.baseUrl(http-remote)")))
        return core::Error {core::ErrorCode::SettingsInvalid,
            QStringLiteral("use HTTPS, or HTTP for a local address")};
    return m_store->save(next);
}

QVariantMap SettingsController::addPresetEngine(const QString &type) {
    provider::SimpleApi api {};
    if (!provider::simpleApiForType(type, &api))
        return failure({core::ErrorCode::SettingsInvalid, QStringLiteral("unknown preset engine")});

    settings::ProviderConfig preset = provider::simplePreset(type);
    if (findProvider(preset.id) != nullptr)
        preset.id = uniqueProviderId(preset.id);
    return addEngine(preset);
}

QVariantMap SettingsController::confirmPendingEngine() {
    const auto pending = std::exchange(m_pendingEngine, std::nullopt);
    if (!pending)
        return failure(
            {core::ErrorCode::SettingsInvalid, QStringLiteral("no engine awaiting approval")});
    return addEngine(*pending, true);
}

void SettingsController::cancelPendingEngine() {
    m_pendingEngine.reset();
}

QVariantMap SettingsController::updateProvider(const QString &id, const QVariantMap &fields) {
    const core::Result<void> result = applyProviderChange(id, fields);
    if (!result.ok())
        return failure(result.error());
    return success(id);
}

bool SettingsController::removeProvider(const QString &id) {
    settings::Settings next = m_store->current();
    const auto before = next.provider.providers.size();
    for (int index = static_cast<int>(next.provider.providers.size()) - 1; index >= 0; --index) {
        if (next.provider.providers.at(index).id != id)
            continue;
        const settings::ProviderConfig removed = next.provider.providers.takeAt(index);
        if (!removed.apiKeyRef.isEmpty())
            m_secrets->remove(removed.apiKeyRef);
    }
    if (next.provider.providers.size() == before) {
        reportError({core::ErrorCode::SettingsInvalid, QStringLiteral("unknown provider")});
        return false;
    }
    if (next.provider.activeId == id)
        next.provider.activeId
            = next.provider.providers.isEmpty() ? QString() : next.provider.providers.first().id;

    const core::Result<void> saved = m_store->save(next);
    if (!saved.ok()) {
        reportError(saved.error());
        return false;
    }
    return true;
}

QVariantMap SettingsController::setApiKey(const QString &id, const QString &key) {
    const settings::ProviderConfig *provider = findProvider(id);
    if (provider == nullptr)
        return failure({core::ErrorCode::SettingsInvalid, QStringLiteral("unknown provider")});
    if (provider->apiKeyRef.isEmpty() && provider->type != QLatin1String("libretranslate")
        && provider->type != QLatin1String("deepl"))
        return failure(
            {core::ErrorCode::SettingsInvalid, QStringLiteral("provider has no credential slot")});
    if (key.trimmed().isEmpty())
        return failure({core::ErrorCode::SettingsInvalid, QStringLiteral("empty credential")});

    const QString keyRef = provider->apiKeyRef.isEmpty()
        ? QStringLiteral("provider/%1/apiKey").arg(id)
        : provider->apiKeyRef;
    const bool addReference = provider->apiKeyRef.isEmpty();
    const core::Result<void> stored = m_secrets->store(keyRef, key.trimmed());
    if (!stored.ok())
        return failure(stored.error());
    if (addReference) {
        settings::Settings next = m_store->current();
        for (settings::ProviderConfig &candidate : next.provider.providers) {
            if (candidate.id == id)
                candidate.apiKeyRef = keyRef;
        }
        const core::Result<void> saved = m_store->save(next);
        if (!saved.ok()) {
            m_secrets->remove(keyRef);
            return failure(saved.error());
        }
    }
    refresh();
    return success(id);
}

void SettingsController::clearApiKey(const QString &id) {
    const settings::ProviderConfig *provider = findProvider(id);
    if (provider == nullptr || provider->apiKeyRef.isEmpty())
        return;
    const core::Result<void> removed = m_secrets->remove(provider->apiKeyRef);
    if (!removed.ok())
        reportError(removed.error());
    refresh();
}

void SettingsController::grantConsent() {
    const core::Result<void> result = m_store->grantConsent();
    if (!result.ok()) {
        reportError(result.error());
        return;
    }
    if (m_coordinator != nullptr) {
        const core::Result<void> recheck = m_coordinator->recheckConsent();
        if (!recheck.ok())
            reportError(recheck.error());
    }
}

void SettingsController::revokeConsent() {
    const core::Result<void> result = m_store->revokeConsent();
    if (!result.ok()) {
        reportError(result.error());
        return;
    }
    if (m_coordinator != nullptr)
        m_coordinator->recheckConsent();
}

void SettingsController::reportError(const core::Error &error) {
    emit failed(QString::fromLatin1(core::stableName(error.code)), error.detail);
}

}  // namespace ui
