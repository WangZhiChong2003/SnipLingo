#include <QDBusConnection>
#include <QRegularExpression>
#include <QStringList>

#include "secrets/secret_service_store.h"
#include "secrets/session_store.h"

namespace secrets {

AvailabilityReport SessionOnlyStore::availability() const {
    AvailabilityReport report;
    report.availability = Availability::SessionOnly;
    report.backend = QStringLiteral("session");
    report.reason
        = QStringLiteral("keys are held in memory and forgotten when the application exits");
    return report;
}

core::Result<QString> SessionOnlyStore::lookup(const QString &key) {
    if (!isValidKeyId(key)) {
        return core::Error {core::ErrorCode::SettingsInvalid, QStringLiteral("invalid key id")};
    }
    const auto found = m_secrets.constFind(key);
    if (found == m_secrets.constEnd()) {
        return core::Error {
            core::ErrorCode::SecretNotFound, QStringLiteral("no secret for this key")};
    }
    return *found;
}

core::Result<void> SessionOnlyStore::store(const QString &key, const QString &secret) {
    if (!isValidKeyId(key)) {
        return core::Error {core::ErrorCode::SettingsInvalid, QStringLiteral("invalid key id")};
    }
    if (secret.isEmpty()) {
        return core::Error {core::ErrorCode::SettingsInvalid, QStringLiteral("empty secret")};
    }
    m_secrets.insert(key, secret);
    return {};
}

core::Result<void> SessionOnlyStore::remove(const QString &key) {
    m_secrets.remove(key);
    return {};
}

void SessionOnlyStore::clear() {
    // Overwrite before releasing: QString does not guarantee zeroization, but this shrinks the
    // window.
    for (auto it = m_secrets.begin(); it != m_secrets.end(); ++it)
        it.value().fill(QChar(0));
    m_secrets.clear();
}

bool isValidKeyId(const QString &key) {
    // Segments are identifiers, not free-form text: letters, digits and dashes only. The documented
    // key shape ends in "apiKey", so case is kept.
    static const QRegularExpression segment(QStringLiteral("^[A-Za-z0-9-]{1,64}$"));
    if (key.isEmpty() || key.size() > 96)
        return false;
    const QStringList parts = key.split(QLatin1Char('/'));
    if (parts.size() < 2)
        return false;
    for (const QString &part : parts) {
        if (!segment.match(part).hasMatch())
            return false;
    }
    return true;
}

std::unique_ptr<SecretStore> createSecretStore(const ServiceProbe &probe) {
    if (!probe.sessionBusConnected || !probe.secretServiceRegistered) {
        // Nothing persistent is reachable here; the reason is surfaced to the user, never hidden.
        return std::make_unique<SessionOnlyStore>();
    }
    // A real Secret Service is registered: hand out the keyring client. Its availability() probes
    // OpenSession, so a registered-but-unusable keyring still degrades to session-only with a
    // reason.
    return std::make_unique<SecretServiceStore>(QDBusConnection::sessionBus(),
        QStringLiteral("org.freedesktop.secrets"), QStringLiteral("/org/freedesktop/secrets"));
}

}  // namespace secrets
