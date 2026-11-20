// The only reader and writer of settings.json.
#pragma once

#include "core/paths.h"
#include "core/result.h"

#include <QJsonObject>
#include <QObject>

#include "settings/settings.h"

namespace settings {

class SettingsStore : public QObject {
    Q_OBJECT
public:
    explicit SettingsStore(core::XdgPaths paths, QObject *parent = nullptr);

    LoadResult load();
    core::Result<void> save(const Settings &next);

    [[nodiscard]] const Settings &current() const { return m_settings; }

    [[nodiscard]] QString settingsFile() const { return m_paths.settingsFile(); }

    core::Result<void> setAutomaticEnabled(bool enabled);
    core::Result<void> setPaused(bool paused);
    core::Result<void> setActiveProvider(const QString &id);
    // Records the user's consent for the currently active provider. The store never decides; the UI
    // asks.
    core::Result<void> grantConsent();
    core::Result<void> revokeConsent();

signals:
    void changed(const settings::Settings &next);
    void loadIssues(const QStringList &issues);

private:
    core::Result<void> persist(Settings next);

    core::XdgPaths m_paths;
    Settings m_settings;
    QJsonObject m_baseDocument;  // parsed document, kept so unknown keys survive a save
};

}  // namespace settings
