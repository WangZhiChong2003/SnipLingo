#include "secrets/secret_service_store.h"

#include "core/log.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>

namespace secrets {
namespace {

constexpr int kCallTimeoutMs = 8000;
const QString kItemInterface = QStringLiteral("org.freedesktop.Secret.Item");
const QString kServiceInterface = QStringLiteral("org.freedesktop.Secret.Service");
const QString kPromptPath = QStringLiteral("/");

QDBusMessage call(const QDBusConnection &bus, const QString &service, const QString &path,
    const QString &interface, const QString &member, const QVariantList &arguments = {}) {
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, member);
    for (const QVariant &argument : arguments)
        message << argument;
    return bus.call(message, QDBus::Block, kCallTimeoutMs);
}

// Reply arguments arrive either as the registered type or wrapped in a QDBusArgument;
// QVariant::toList() and friends silently produce empty values in the second case, which reads as
// "nothing found".
template <typename T>
T argumentAs(const QVariant &value) {
    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<T>(value.value<QDBusArgument>());
    return value.value<T>();
}

QStringList pathsOf(const QVariant &value) {
    QStringList paths;
    for (const QDBusObjectPath &path : argumentAs<QList<QDBusObjectPath>>(value))
        paths.append(path.path());
    return paths;
}

bool replied(const QDBusMessage &reply, QString *error) {
    if (reply.type() == QDBusMessage::ReplyMessage)
        return true;
    if (error != nullptr) {
        *error = reply.errorMessage().isEmpty() ? QStringLiteral("D-Bus call failed")
                                                : reply.errorMessage();
    }
    return false;
}

// The API types attributes as a{ss}; QVariantMap (QMap<QString, QVariant>) would marshal as a{sv}
// and be rejected by a real keyring, so the exact map type matters.
QMap<QString, QString> attributesFor(const QString &key) {
    QMap<QString, QString> attributes;
    attributes.insert(QStringLiteral("application"), QStringLiteral("SnipLingo"));
    attributes.insert(QStringLiteral("key"), key);
    return attributes;
}

}  // namespace

// QtDBus derives a custom type's signature by marshalling a default-constructed value once, so a
// "QDBusObjectPath: invalid path" warning can appear at the first send. That warning is Qt's probe,
// not a failed conversion.
QDBusArgument &operator<<(QDBusArgument &argument, const ServiceSecret &secret) {
    argument.beginStructure();
    argument << QDBusObjectPath(secret.session) << secret.parameters << secret.value
             << secret.contentType;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, ServiceSecret &secret) {
    argument.beginStructure();
    QDBusObjectPath session;
    argument >> session >> secret.parameters >> secret.value >> secret.contentType;
    secret.session = session.path();
    argument.endStructure();
    return argument;
}

SecretServiceStore::SecretServiceStore(
    QDBusConnection connection, QString serviceName, QString servicePath)
    : m_bus(std::move(connection))
    , m_serviceName(std::move(serviceName))
    , m_servicePath(std::move(servicePath)) {
    qDBusRegisterMetaType<ServiceSecret>();
    // QtDBus has no built-in marshaller for this map; registering it is what makes attributes
    // travel as a{ss} instead of failing at marshal time.
    qDBusRegisterMetaType<QMap<QString, QString>>();
}

AvailabilityReport SecretServiceStore::availability() const {
    AvailabilityReport report;
    if (!m_bus.isConnected()) {
        report.availability = Availability::SessionOnly;
        report.backend = QStringLiteral("session");
        report.reason = QStringLiteral("session bus unreachable; keys live in memory until exit");
        return report;
    }
    QString error;
    const QString session = openSession(&error);
    if (session.isEmpty()) {
        report.availability = Availability::SessionOnly;
        report.backend = QStringLiteral("session");
        report.reason
            = QStringLiteral("the keyring is unusable (%1); keys live in memory until exit")
                  .arg(error);
        return report;
    }
    closeSession(session);
    report.availability = Availability::Available;
    report.backend = QStringLiteral("secret-service");
    report.reason = QStringLiteral("keys are stored in the desktop keyring");
    return report;
}

QString SecretServiceStore::openSession(QString *error) const {
    // "plain" transport: the value crosses the session bus unencrypted, exactly like the keyring's
    // own clients do. "dh" would add nothing without a private bus.
    const QDBusMessage reply = call(m_bus, m_serviceName, m_servicePath, kServiceInterface,
        QStringLiteral("OpenSession"),
        {QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString()))});
    if (!replied(reply, error))
        return {};
    if (reply.arguments().size() < 2) {
        if (error != nullptr)
            *error = QStringLiteral("OpenSession returned no session object");
        return {};
    }
    return argumentAs<QDBusObjectPath>(reply.arguments().at(1)).path();
}

void SecretServiceStore::closeSession(const QString &session) const {
    if (session.isEmpty() || session == kPromptPath)
        return;
    call(m_bus, m_serviceName, session, QStringLiteral("org.freedesktop.Secret.Session"),
        QStringLiteral("Close"));
}

core::Result<QStringList> SecretServiceStore::unlockedItems(const QString &key) const {
    QString error;
    const QDBusMessage reply = call(m_bus, m_serviceName, m_servicePath, kServiceInterface,
        QStringLiteral("SearchItems"), {QVariant::fromValue(attributesFor(key))});
    if (!replied(reply, &error))
        return core::Error {core::ErrorCode::SecretStoreUnavailable, error};
    if (reply.arguments().size() < 2)
        return core::Error {core::ErrorCode::SecretStoreUnavailable,
            QStringLiteral("SearchItems returned no result")};

    QStringList paths = pathsOf(reply.arguments().at(0));

    const QStringList lockedPaths = pathsOf(reply.arguments().at(1));
    if (lockedPaths.isEmpty())
        return paths;

    QList<QDBusObjectPath> locked;
    for (const QString &path : lockedPaths)
        locked.append(QDBusObjectPath(path));

    const QDBusMessage unlockReply = call(m_bus, m_serviceName, m_servicePath, kServiceInterface,
        QStringLiteral("Unlock"), {QVariant::fromValue(locked)});
    if (!replied(unlockReply, &error))
        return core::Error {core::ErrorCode::SecretStoreUnavailable, error};
    if (unlockReply.arguments().size() < 2)
        return core::Error {
            core::ErrorCode::SecretStoreUnavailable, QStringLiteral("Unlock returned no result")};

    const QString prompt = argumentAs<QDBusObjectPath>(unlockReply.arguments().at(1)).path();
    if (prompt != kPromptPath) {
        // An interactive prompt needs UI we do not have; say so instead of blocking or guessing.
        return core::Error {core::ErrorCode::SecretStoreUnavailable,
            QStringLiteral("the keyring is locked and needs an interactive unlock")};
    }
    paths.append(pathsOf(unlockReply.arguments().at(0)));
    return paths;
}

core::Result<QString> SecretServiceStore::lookup(const QString &key) {
    if (!isValidKeyId(key))
        return core::Error {core::ErrorCode::SettingsInvalid, QStringLiteral("invalid key id")};

    QString error;
    const QString session = openSession(&error);
    if (session.isEmpty())
        return core::Error {core::ErrorCode::SecretStoreUnavailable, error};

    const core::Result<QStringList> items = unlockedItems(key);
    if (!items.ok()) {
        closeSession(session);
        return items.error();
    }

    QString found;
    for (const QString &path : items.value()) {
        const QDBusMessage reply = call(m_bus, m_serviceName, path, kItemInterface,
            QStringLiteral("GetSecret"), {QVariant::fromValue(QDBusObjectPath(session))});
        if (!replied(reply, &error))
            continue;
        if (reply.arguments().isEmpty())
            continue;
        const ServiceSecret secret = argumentAs<ServiceSecret>(reply.arguments().first());
        if (!secret.value.isEmpty()) {
            found = QString::fromUtf8(secret.value);
            break;
        }
    }
    closeSession(session);

    if (found.isEmpty())
        return core::Error {
            core::ErrorCode::SecretNotFound, QStringLiteral("no secret for this key")};
    return found;
}

core::Result<void> SecretServiceStore::store(const QString &key, const QString &secret) {
    if (!isValidKeyId(key))
        return core::Error {core::ErrorCode::SettingsInvalid, QStringLiteral("invalid key id")};
    if (secret.isEmpty())
        return core::Error {core::ErrorCode::SettingsInvalid, QStringLiteral("empty secret")};

    QString error;
    const QString session = openSession(&error);
    if (session.isEmpty())
        return core::Error {core::ErrorCode::SecretStoreUnavailable, error};

    QVariantMap properties;
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Label"), key);
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Attributes"),
        QVariant::fromValue(attributesFor(key)));

    ServiceSecret serviceSecret;
    serviceSecret.session = session;
    serviceSecret.value = secret.toUtf8();
    serviceSecret.contentType = QStringLiteral("text/plain");

    const QDBusMessage reply = call(m_bus, m_serviceName, m_servicePath, kServiceInterface,
        QStringLiteral("CreateItem"), {properties, QVariant::fromValue(serviceSecret), true});
    const bool ok = replied(reply, &error);
    closeSession(session);
    if (!ok)
        return core::Error {core::ErrorCode::SecretStoreUnavailable, error};
    if (reply.arguments().size() >= 2) {
        const QString prompt = argumentAs<QDBusObjectPath>(reply.arguments().at(1)).path();
        if (prompt != kPromptPath)
            return core::Error {core::ErrorCode::SecretStoreUnavailable,
                QStringLiteral("the keyring needs an interactive prompt to store this key")};
    }
    return {};
}

core::Result<void> SecretServiceStore::remove(const QString &key) {
    if (!isValidKeyId(key))
        return core::Error {core::ErrorCode::SettingsInvalid, QStringLiteral("invalid key id")};

    const core::Result<QStringList> items = unlockedItems(key);
    if (!items.ok())
        return items.error();

    for (const QString &path : items.value()) {
        QString deleteError;
        const QDBusMessage reply
            = call(m_bus, m_serviceName, path, kItemInterface, QStringLiteral("Delete"));
        if (!replied(reply, &deleteError)) {
            core::log::write(core::log::Level::Warning, "secrets",
                QStringLiteral("could not delete a keyring item: %1").arg(deleteError));
        }
    }
    return {};
}

}  // namespace secrets
