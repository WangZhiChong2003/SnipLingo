// Secret Service client over QtDBus (docs/design/03-secrets.md §5.1). No new dependency and no
// plaintext fallback: when keyring access fails the caller degrades to session-only storage and
// says so.
#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QStringList>

#include "secrets/secret_store.h"

namespace secrets {

// Secret struct of the Secret Service API: (o session, ay parameters, ay value, s content-type).
struct ServiceSecret {
    QString session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &argument, const ServiceSecret &secret);
const QDBusArgument &operator>>(const QDBusArgument &argument, ServiceSecret &secret);

class SecretServiceStore : public SecretStore {
public:
    SecretServiceStore(QDBusConnection connection, QString serviceName, QString servicePath);

    [[nodiscard]] AvailabilityReport availability() const override;
    core::Result<QString> lookup(const QString &key) override;
    core::Result<void> store(const QString &key, const QString &secret) override;
    core::Result<void> remove(const QString &key) override;

private:
    [[nodiscard]] QString openSession(QString *error) const;
    void closeSession(const QString &session) const;
    // Unlocked item paths for `key`; fails when the keyring needs an interactive unlock.
    core::Result<QStringList> unlockedItems(const QString &key) const;

    QDBusConnection m_bus;
    QString m_serviceName;
    QString m_servicePath;
};

}  // namespace secrets

Q_DECLARE_METATYPE(secrets::ServiceSecret)
