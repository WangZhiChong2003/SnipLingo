// Settings surface for QML (docs/design/02-settings.md, 07-ui.md §6).
//
// The controller owns no policy of its own: every write goes through SettingsStore (which validates
// and persists) and consent stays a single, explicit user action. Credentials never leave the
// SecretStore and are never exposed as a readable property — only whether a key is present.
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "coordinator/request_coordinator.h"
#include "secrets/secret_store.h"
#include "settings/settings_store.h"

namespace ui {

class SettingsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool automaticEnabled READ automaticEnabled NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(bool showResultImmediately READ showResultImmediately NOTIFY changed)
    Q_PROPERTY(bool observationActive READ observationActive NOTIFY observationChanged)
    Q_PROPERTY(bool waylandSession READ waylandSession CONSTANT)
    Q_PROPERTY(QString selectionBackendId READ selectionBackendId CONSTANT)
    Q_PROPERTY(QString selectionBackendChoice READ selectionBackendChoice NOTIFY changed)
    Q_PROPERTY(bool selectionRestartRequired READ selectionRestartRequired NOTIFY changed)
    Q_PROPERTY(QString sourceLanguage READ sourceLanguage NOTIFY changed)
    Q_PROPERTY(QString targetLanguage READ targetLanguage NOTIFY changed)
    Q_PROPERTY(QStringList targetLanguageCodes READ targetLanguageCodes CONSTANT)
    Q_PROPERTY(QString activeProviderId READ activeProviderId NOTIFY changed)
    Q_PROPERTY(QVariantList providers READ providers NOTIFY changed)
    Q_PROPERTY(bool consentGranted READ consentGranted NOTIFY changed)
    Q_PROPERTY(bool consentStale READ consentStale NOTIFY changed)
    Q_PROPERTY(QString consentDestination READ consentDestination NOTIFY changed)
    Q_PROPERTY(bool credentialPersistent READ credentialPersistent NOTIFY changed)
    Q_PROPERTY(QString credentialBackend READ credentialBackend NOTIFY changed)
    Q_PROPERTY(QString credentialReason READ credentialReason NOTIFY changed)
    Q_PROPERTY(QString proxyMode READ proxyMode NOTIFY changed)
    Q_PROPERTY(QString proxyUrl READ proxyUrl NOTIFY changed)
    Q_PROPERTY(QString proxyEffective READ proxyEffective NOTIFY changed)

public:
    SettingsController(settings::SettingsStore *store, secrets::SecretStore *secrets,
        coordinator::RequestCoordinator *coordinator, QObject *parent = nullptr);

    [[nodiscard]] bool automaticEnabled() const;
    [[nodiscard]] bool paused() const;
    [[nodiscard]] bool showResultImmediately() const;
    [[nodiscard]] bool observationActive() const;
    [[nodiscard]] bool waylandSession() const;
    [[nodiscard]] QString selectionBackendId() const;
    [[nodiscard]] QString selectionBackendChoice() const;
    [[nodiscard]] bool selectionRestartRequired() const;
    [[nodiscard]] QString sourceLanguage() const;
    [[nodiscard]] QString targetLanguage() const;
    [[nodiscard]] QStringList targetLanguageCodes() const;
    [[nodiscard]] QString activeProviderId() const;
    [[nodiscard]] QVariantList providers() const;
    [[nodiscard]] bool consentGranted() const;
    [[nodiscard]] bool consentStale() const;
    [[nodiscard]] QString consentDestination() const;
    [[nodiscard]] bool credentialPersistent() const;
    [[nodiscard]] QString credentialBackend() const;
    [[nodiscard]] QString credentialReason() const;
    [[nodiscard]] QString proxyMode() const;
    [[nodiscard]] QString proxyUrl() const;
    [[nodiscard]] QString proxyEffective() const;

public slots:
    void setAutomaticEnabled(bool enabled);
    void setPaused(bool paused);
    void setShowResultImmediately(bool enabled);
    void setSelectionBackend(const QString &backend);
    void setSourceLanguage(const QString &code);
    void setTargetLanguage(const QString &code);
    void setActiveProvider(const QString &id);
    QVariantMap translateText(const QString &text);

    // Returns {ok: bool, id: string, error: string}. `baseUrl` is validated by the settings layer.
    QVariantMap addProvider(const QString &label, const QString &baseUrl, const QString &model);
    // Adds a documented keyless engine ("mymemory", "libretranslate", "google-free") with its
    // defaults.
    QVariantMap addPresetEngine(const QString &type);
    QVariantMap updateProvider(const QString &id, const QVariantMap &fields);
    bool removeProvider(const QString &id);

    // Stores the credential and points the provider at it; returns {ok, error}.
    QVariantMap setApiKey(const QString &id, const QString &key);
    void clearApiKey(const QString &id);

    void grantConsent();
    void revokeConsent();

    // "system" honours the environment proxy, "manual" uses the given URL, "none" connects
    // directly.
    QVariantMap setProxy(const QString &mode, const QString &url);

signals:
    void changed();
    void observationChanged();
    // Emitted when the coordinator needs consent for the destination currently being used.
    void consentRequired(const QString &destinationLabel, bool staleConsent);
    // Stable error code plus a detail that never carries user text or credential material.
    void failed(const QString &code, const QString &detail);

private:
    void refresh();
    void reportError(const core::Error &error);
    [[nodiscard]] const settings::ProviderConfig *findProvider(const QString &id) const;
    [[nodiscard]] QString uniqueProviderId(const QString &label) const;
    core::Result<void> applyProviderChange(const QString &id, const QVariantMap &fields);

    settings::SettingsStore *m_store = nullptr;
    secrets::SecretStore *m_secrets = nullptr;
    coordinator::RequestCoordinator *m_coordinator = nullptr;
    QStringList m_presentKeyRefs;
    QString m_initialSelectionBackend;
};

}  // namespace ui
