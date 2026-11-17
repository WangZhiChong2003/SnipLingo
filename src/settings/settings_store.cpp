#include "settings/settings_store.h"

#include "core/log.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace settings {
namespace {

QString corruptBackupPath(const QString &settingsFile) {
    const QString stamp
        = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmss"));
    return settingsFile + QStringLiteral(".corrupt-") + stamp;
}

core::Result<void> lockDown(const QString &path) {
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return core::Error {core::ErrorCode::Internal,
            QStringLiteral("cannot restrict permissions of %1").arg(path)};
    }
    return {};
}

}  // namespace

SettingsStore::SettingsStore(core::XdgPaths paths, QObject *parent)
    : QObject(parent)
    , m_paths(std::move(paths)) { }

LoadResult SettingsStore::load() {
    const QString path = m_paths.settingsFile();
    QJsonObject document;
    bool documentReadable = false;
    QString recoveryNote;

    const QFileInfo info(path);
    if (info.exists()) {
        QFile file(path);
        QJsonParseError parseError {};
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && parsed.isObject()) {
                document = parsed.object();
                documentReadable = true;
            }
        }
        if (!documentReadable) {
            // Preserve the unreadable file for diagnosis, then continue with defaults.
            const QString backup = corruptBackupPath(path);
            if (QFile::copy(path, backup))
                recoveryNote = QStringLiteral("preserved as %1").arg(backup);
            else
                recoveryNote = QStringLiteral("could not preserve the unreadable file");
        }
    }

    LoadResult result = loadFromDocument(document, documentReadable);
    if (!documentReadable && info.exists()) {
        result.outcome = Outcome::Recovered;
        if (!result.issues.contains(QStringLiteral("file")))
            result.issues.append(QStringLiteral("file"));
        core::log::write(core::log::Level::Warning, "settings", recoveryNote);
    }

    m_settings = result.settings;
    m_baseDocument = result.baseDocument;
    if (!result.issues.isEmpty())
        emit loadIssues(result.issues);
    return result;
}

core::Result<void> SettingsStore::save(const Settings &next) {
    return persist(next);
}

core::Result<void> SettingsStore::persist(Settings next) {
    if (auto ensured = m_paths.ensureDirectories(); !ensured.ok())
        return ensured;

    QStringList issues;
    validate(next, &issues);
    for (const QString &issue : issues)
        core::log::write(
            core::log::Level::Info, "settings", QStringLiteral("adjusted field %1").arg(issue));

    const QJsonObject document = mergePreservingUnknown(m_baseDocument, toJson(next));
    const QString path = m_paths.settingsFile();

    // QSaveFile commits atomically: a crash leaves either the previous document or the new one.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return core::Error {
            core::ErrorCode::Internal, QStringLiteral("cannot open %1 for writing").arg(path)};
    file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return core::Error {
            core::ErrorCode::Internal, QStringLiteral("cannot commit %1").arg(path)};
    if (auto locked = lockDown(path); !locked.ok())
        return locked;

    m_settings = next;
    m_baseDocument = document;
    emit changed(m_settings);
    return {};
}

core::Result<void> SettingsStore::setAutomaticEnabled(bool enabled) {
    Settings next = m_settings;
    next.selection.automaticEnabled = enabled;
    return persist(next);
}

core::Result<void> SettingsStore::setPaused(bool paused) {
    Settings next = m_settings;
    next.selection.paused = paused;
    return persist(next);
}

core::Result<void> SettingsStore::setActiveProvider(const QString &id) {
    Settings next = m_settings;
    next.provider.activeId = id;
    return persist(next);
}

core::Result<void> SettingsStore::grantConsent() {
    const ProviderConfig *active = m_settings.activeProvider();
    if (!active) {
        return core::Error {core::ErrorCode::ProviderNotConfigured,
            QStringLiteral("no active provider to consent to")};
    }
    Settings next = m_settings;
    next.consent.fingerprint = active->fingerprint();
    next.consent.destinationLabel = active->destinationLabel();
    next.consent.grantedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return persist(next);
}

core::Result<void> SettingsStore::revokeConsent() {
    Settings next = m_settings;
    next.consent = ConsentRecord {};
    return persist(next);
}

}  // namespace settings
