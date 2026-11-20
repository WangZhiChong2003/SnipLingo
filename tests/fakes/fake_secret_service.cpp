#include "fake_secret_service.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>

namespace {

constexpr const char *kServicePath = "/org/freedesktop/secrets";
constexpr const char *kPromptPath = "/org/freedesktop/secrets/prompt/1";

// A virtual object receives non-basic arguments as QDBusArgument variants; QVariant::value<T>()
// alone would silently produce an empty value, which is exactly how a broken harness hides a
// working client.
template <typename T>
T argumentAs(const QVariant &value) {
    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<T>(value.value<QDBusArgument>());
    return value.value<T>();
}

QList<QDBusObjectPath> objectPaths(const QVariant &value) {
    QList<QDBusObjectPath> list;
    for (const QVariant &entry : argumentAs<QList<QDBusObjectPath>>(value))
        list.append(entry.value<QDBusObjectPath>());
    return list;
}

}  // namespace

FakeSecretService::FakeSecretService(const QString &busAddress, QObject *parent)
    : QDBusVirtualObject(parent)
    , m_bus(QDBusConnection::connectToBus(busAddress, QStringLiteral("fake_secrets"))) {
    qDBusRegisterMetaType<secrets::ServiceSecret>();
    qDBusRegisterMetaType<QMap<QString, QString>>();

    if (!m_bus.isConnected()) {
        m_error = QStringLiteral("cannot connect to %1").arg(busAddress);
        return;
    }
    // QDBusConnection::registerService() collapses every failure into false. Ask the bus directly
    // so a broken private bus (refused authentication, denied name ownership) is diagnosed here.
    QDBusMessage request = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("RequestName"));
    request << QStringLiteral("org.freedesktop.secrets") << 0u;  // no queueing, no replacement
    const QDBusMessage reply = m_bus.call(request, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        m_error
            = QStringLiteral("cannot own org.freedesktop.secrets: %1").arg(reply.errorMessage());
        return;
    }
    // 1 is DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER, 4 is DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER.
    const uint code = reply.arguments().value(0).toUInt();
    if (code != 1u && code != 4u) {
        m_error = QStringLiteral("cannot own org.freedesktop.secrets: reply code %1").arg(code);
        return;
    }
    if (!m_bus.registerVirtualObject(
            QString::fromLatin1(kServicePath), this, QDBusConnection::SubPath)) {
        m_error = QStringLiteral("cannot export the service object");
        return;
    }
    m_ready = true;
}

FakeSecretService::~FakeSecretService() {
    m_bus.unregisterObject(QString::fromLatin1(kServicePath));
    m_bus.unregisterService(QStringLiteral("org.freedesktop.secrets"));
    QDBusConnection::disconnectFromBus(QStringLiteral("fake_secrets"));
}

void FakeSecretService::seed(const QString &key, const QString &value) {
    Item item;
    item.attributes.insert(QStringLiteral("application"), QStringLiteral("SnipLingo"));
    item.attributes.insert(QStringLiteral("key"), key);
    item.label = key;
    item.value = value.toUtf8();
    m_items.insert(QStringLiteral("/org/freedesktop/secrets/item/%1").arg(m_nextItemId++), item);
}

QString FakeSecretService::storedValue(const QString &key) const {
    const QString path = findItemByKey(key);
    return path.isEmpty() ? QString() : QString::fromUtf8(m_items.value(path).value);
}

QString FakeSecretService::findItemByKey(const QString &key) const {
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        if (it.value().attributes.value(QStringLiteral("key")) == key)
            return it.key();
    }
    return {};
}

QString FakeSecretService::introspect(const QString &path) const {
    Q_UNUSED(path)
    return QStringLiteral("<node/>");
}

bool FakeSecretService::handleMessage(
    const QDBusMessage &message, const QDBusConnection &connection) {
    m_signatures.insert(message.member(), message.signature());
    QDBusMessage reply = dispatch(message);
    if (reply.type() == QDBusMessage::InvalidMessage)
        return false;
    connection.send(reply);
    return true;
}

QDBusMessage FakeSecretService::dispatch(const QDBusMessage &message) {
    const QString interface = message.interface();
    const QString member = message.member();
    const QString path = message.path();
    const QVariantList arguments = message.arguments();

    if (interface == QLatin1String("org.freedesktop.DBus.Peer") && member == QLatin1String("Ping"))
        return message.createReply();
    if (interface == QLatin1String("org.freedesktop.DBus.Introspectable")
        && member == QLatin1String("Introspect"))
        return message.createReply(introspect(path));

    if (interface == QLatin1String("org.freedesktop.Secret.Service")) {
        if (member == QLatin1String("OpenSession")) {
            QDBusMessage reply = message.createReply();
            reply << QVariant::fromValue(QDBusVariant(QStringLiteral("")))
                  << QVariant::fromValue(
                         QDBusObjectPath(QStringLiteral("/org/freedesktop/secrets/session/1")));
            return reply;
        }
        if (member == QLatin1String("SearchItems")) {
            const QMap<QString, QString> attributes
                = argumentAs<QMap<QString, QString>>(arguments.value(0));
            QStringList matched;
            for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
                bool matches = true;
                for (auto attribute = attributes.constBegin(); attribute != attributes.constEnd();
                     ++attribute) {
                    if (it.value().attributes.value(attribute.key()) != attribute.value())
                        matches = false;
                }
                if (matches)
                    matched.append(it.key());
            }

            QList<QDBusObjectPath> unlocked;
            QList<QDBusObjectPath> locked;
            for (const QString &path : matched)
                (m_locked ? locked : unlocked).append(QDBusObjectPath(path));
            QDBusMessage reply = message.createReply();
            reply << QVariant::fromValue(unlocked) << QVariant::fromValue(locked);
            return reply;
        }
        if (member == QLatin1String("Unlock")) {
            QDBusMessage reply = message.createReply();
            if (m_locked) {
                // A real keyring asks the user here; report a prompt and leave the items locked.
                reply << QVariant::fromValue(QList<QDBusObjectPath> {})
                      << QVariant::fromValue(QDBusObjectPath(QString::fromLatin1(kPromptPath)));
                return reply;
            }
            reply << arguments.value(0)
                  << QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")));
            return reply;
        }
        if (member == QLatin1String("CreateItem")) {
            const QVariantMap properties = argumentAs<QVariantMap>(arguments.value(0));
            const secrets::ServiceSecret secret
                = argumentAs<secrets::ServiceSecret>(arguments.value(1));
            const QMap<QString, QString> attributes = argumentAs<QMap<QString, QString>>(
                properties.value(QStringLiteral("org.freedesktop.Secret.Item.Attributes")));
            const QString key = attributes.value(QStringLiteral("key"));

            const QString existing = findItemByKey(key);
            if (!existing.isEmpty())
                m_items.remove(existing);

            Item item;
            item.attributes = attributes.isEmpty()
                ? QMap<QString, QString> {{QStringLiteral("application"),
                                              QStringLiteral("SnipLingo")},
                      {QStringLiteral("key"), key}}
                : attributes;
            item.label
                = properties.value(QStringLiteral("org.freedesktop.Secret.Item.Label")).toString();
            item.value = secret.value;
            const QString itemPath
                = QStringLiteral("/org/freedesktop/secrets/item/%1").arg(m_nextItemId++);
            m_items.insert(itemPath, item);

            QDBusMessage reply = message.createReply();
            reply << QVariant::fromValue(QDBusObjectPath(itemPath))
                  << QVariant::fromValue(
                         QDBusObjectPath(m_requirePromptForWrites ? QString::fromLatin1(kPromptPath)
                                                                  : QStringLiteral("/")));
            return reply;
        }
    }

    if (interface == QLatin1String("org.freedesktop.Secret.Item")) {
        if (member == QLatin1String("GetSecret")) {
            const Item item = m_items.value(path);
            secrets::ServiceSecret secret;
            secret.session = QStringLiteral("/org/freedesktop/secrets/session/1");
            secret.value = item.value;
            secret.contentType = QStringLiteral("text/plain");
            QDBusMessage reply = message.createReply();
            reply << QVariant::fromValue(secret);
            return reply;
        }
        if (member == QLatin1String("Delete")) {
            m_items.remove(path);
            return message.createReply(QVariant::fromValue(QDBusObjectPath(QStringLiteral("/"))));
        }
    }

    if (interface == QLatin1String("org.freedesktop.Secret.Session")
        && member == QLatin1String("Close"))
        return message.createReply();

    if (interface == QLatin1String("org.freedesktop.DBus.Properties")
        && member == QLatin1String("Get")) {
        const QString property = arguments.value(1).toString();
        const Item item = m_items.value(path);
        QVariant value;
        if (property == QLatin1String("Label"))
            value = item.label;
        else if (property == QLatin1String("Locked"))
            value = m_locked;
        else if (property == QLatin1String("Attributes"))
            value = QVariant::fromValue(item.attributes);
        else
            return {};
        QDBusMessage reply = message.createReply();
        reply << QVariant::fromValue(QDBusVariant(value));
        return reply;
    }

    return {};
}
