#include "fake_atspi_service.h"

#include <QDBusMetaType>
#include <QDBusVariant>

#include <atspi/atspi-constants.h>

namespace {

constexpr const char *kTextPath = "/org/a11y/atspi/accessible/1";
constexpr const char *kRootPath = "/org/a11y/atspi/accessible/root";
constexpr const char *kRegistryPath = "/org/a11y/atspi/registry";
constexpr const char *kBusPath = "/org/a11y/bus";

}  // namespace

QDBusArgument &operator<<(QDBusArgument &argument, const FakeAccessibleRef &reference) {
    argument.beginStructure();
    argument << reference.bus << QDBusObjectPath(reference.path);
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, FakeAccessibleRef &reference) {
    argument.beginStructure();
    argument >> reference.bus;
    QDBusObjectPath path;
    argument >> path;
    reference.path = path.path();
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const FakeTextRange &range) {
    argument.beginStructure();
    argument << range.start << range.end;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, FakeTextRange &range) {
    argument.beginStructure();
    argument >> range.start >> range.end;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const FakeRect &rect) {
    argument.beginStructure();
    argument << rect.x << rect.y << rect.width << rect.height;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, FakeRect &rect) {
    argument.beginStructure();
    argument >> rect.x >> rect.y >> rect.width >> rect.height;
    argument.endStructure();
    return argument;
}

FakeAtspiService::FakeAtspiService(const QString &busAddress, QObject *parent)
    : QDBusVirtualObject(parent)
    , m_address(busAddress)
    , m_bus(QDBusConnection::connectToBus(busAddress, QStringLiteral("fake_atspi")))
    , m_appBus(QDBusConnection::connectToBus(busAddress, QStringLiteral("fake_atspi_app")))
    , m_paths {QString::fromLatin1(kTextPath), QString::fromLatin1(kRootPath)} {
    qDBusRegisterMetaType<FakeAccessibleRef>();
    qDBusRegisterMetaType<QList<FakeAccessibleRef>>();
    qDBusRegisterMetaType<FakeTextRange>();
    qDBusRegisterMetaType<FakeRect>();

    if (!m_bus.isConnected() || !m_appBus.isConnected()) {
        m_error = QStringLiteral("cannot connect the fake service to %1").arg(busAddress);
        return;
    }
    if (!m_bus.registerService(QStringLiteral("org.a11y.Bus"))) {
        m_error = QStringLiteral("cannot own org.a11y.Bus");
        return;
    }
    if (!m_bus.registerService(QStringLiteral("org.a11y.atspi.Registry"))) {
        m_error = QStringLiteral("cannot own org.a11y.atspi.Registry");
        return;
    }
    for (const QString &path : {QString::fromLatin1(kRegistryPath), QString::fromLatin1(kBusPath),
             QString::fromLatin1(kRootPath)}) {
        if (!m_bus.registerVirtualObject(path, this, QDBusConnection::SubPath)) {
            m_error = QStringLiteral("cannot export a virtual object at %1").arg(path);
            return;
        }
    }
    for (const QString &path : m_paths) {
        if (!m_appBus.registerVirtualObject(path, this, QDBusConnection::SubPath)) {
            m_error = QStringLiteral("cannot export a virtual object at %1").arg(path);
            return;
        }
    }
    m_ready = true;
}

FakeAtspiService::~FakeAtspiService() {
    for (const QString &path : m_paths)
        m_appBus.unregisterObject(path);
    m_bus.unregisterObject(QString::fromLatin1(kRegistryPath));
    m_bus.unregisterObject(QString::fromLatin1(kBusPath));
    m_bus.unregisterObject(QString::fromLatin1(kRootPath));
    m_bus.unregisterService(QStringLiteral("org.a11y.atspi.Registry"));
    m_bus.unregisterService(QStringLiteral("org.a11y.Bus"));
    QDBusConnection::disconnectFromBus(QStringLiteral("fake_atspi"));
    QDBusConnection::disconnectFromBus(QStringLiteral("fake_atspi_app"));
}

void FakeAtspiService::setSelection(
    const QString &text, int start, int end, const QString &roleName) {
    if (m_roleName != roleName)
        m_textPath = QString::fromLatin1(kTextPath) + QLatin1Char('/')
            + QString::number(++m_objectVersion);
    m_text = text;
    m_start = start;
    m_end = end;
    m_hasSelection = true;
    m_roleName = roleName;
}

void FakeAtspiService::clearSelection() {
    m_start = 0;
    m_end = 0;
    m_hasSelection = false;
}

bool FakeAtspiService::emitSelectionChanged(QString *error) {
    QDBusMessage signal = QDBusMessage::createSignal(m_textPath,
        QStringLiteral("org.a11y.atspi.Event.Object"), QStringLiteral("TextSelectionChanged"));
    // AT-SPI event signature is (s, i, i, v, a{sv}): detail1, detail2, any_data, properties.
    // Emitting one "i" short makes dbind reject the signal with "invalid signature siva{sv}" —
    // which looks exactly like "no events arrive".
    signal.setArguments({QStringLiteral("unknown"), 0, 0,
        QVariant::fromValue(QDBusVariant(QVariant(0))), QVariantMap()});
    if (!m_appBus.send(signal)) {
        if (error)
            *error = QStringLiteral("signal could not be sent");
        return false;
    }
    return true;
}

QString FakeAtspiService::introspect(const QString &path) const {
    const bool textObject = path == m_textPath;
    QString xml = QStringLiteral("<node>");
    if (path == QLatin1String(kBusPath))
        xml += QStringLiteral("<interface name=\"org.a11y.Bus\"><method name=\"GetAddress\">"
                              "<arg type=\"s\" direction=\"out\"/></method></interface>");
    if (path == QLatin1String(kRegistryPath))
        xml += QStringLiteral(
            "<interface name=\"org.a11y.atspi.Event.Listener\">"
            "<method name=\"RegisterEvent\"><arg type=\"s\" direction=\"in\"/>"
            "</method><method name=\"DeregisterEvent\"><arg type=\"s\" direction=\"in\"/>"
            "</method></interface>"
            "<interface name=\"org.a11y.atspi.Registry\"><method name=\"GetDesktop\">"
            "<arg type=\"i\" direction=\"in\"/><arg type=\"(so)\" direction=\"out\"/>"
            "</method></interface>");
    if (path == m_rootPath)
        xml += QStringLiteral("<interface name=\"org.a11y.atspi.Application\">"
                              "<method name=\"GetApplicationBusAddress\">"
                              "<arg type=\"s\" direction=\"out\"/></method></interface>");
    if (textObject || path == m_rootPath)
        xml += QStringLiteral(
            "<interface name=\"org.a11y.atspi.Accessible\">"
            "<method name=\"GetName\"><arg type=\"s\" direction=\"out\"/></method>"
            "<method name=\"GetRoleName\"><arg type=\"s\" direction=\"out\"/></method>"
            "<method name=\"GetInterfaces\"><arg type=\"as\" direction=\"out\"/></method>"
            "<method name=\"GetApplication\"><arg type=\"(so)\" direction=\"out\"/>"
            "</method>"
            "<method name=\"GetChildCount\"><arg type=\"i\" direction=\"out\"/></method>"
            "</interface>");
    if (textObject)
        xml += QStringLiteral(
            "<interface name=\"org.a11y.atspi.Text\">"
            "<method name=\"GetNSelections\"><arg type=\"i\" direction=\"out\"/>"
            "</method><method name=\"GetSelection\"><arg type=\"i\" direction=\"in\"/>"
            "<arg type=\"(ii)\" direction=\"out\"/></method>"
            "<method name=\"GetText\"><arg type=\"i\" direction=\"in\"/>"
            "<arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"out\"/>"
            "</method><method name=\"GetCharacterCount\"><arg type=\"i\" direction=\"out\"/>"
            "</method><method name=\"GetRangeExtents\"><arg type=\"i\" direction=\"in\"/>"
            "<arg type=\"i\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/>"
            "<arg type=\"(iiii)\" direction=\"out\"/></method>"
            "<signal name=\"TextSelectionChanged\"><arg type=\"s\"/><arg type=\"i\"/>"
            "<arg type=\"v\"/><arg type=\"a{sv}\"/></signal></interface>");
    xml += QStringLiteral("<interface name=\"org.freedesktop.DBus.Properties\">"
                          "<method name=\"Get\"><arg type=\"s\" direction=\"in\"/>"
                          "<arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/>"
                          "</method></interface>");
    xml += QStringLiteral("</node>");
    return xml;
}

bool FakeAtspiService::handleMessage(
    const QDBusMessage &message, const QDBusConnection &connection) {
    QDBusMessage reply = dispatch(message);
    if (reply.type() == QDBusMessage::InvalidMessage)
        return false;  // let Qt answer with "unknown method"
    connection.send(reply);
    return true;
}

QDBusMessage FakeAtspiService::dispatch(const QDBusMessage &message) const {
    const QString interface = message.interface();
    const QString member = message.member();
    const QString path = message.path();

    if (interface == QLatin1String("org.freedesktop.DBus.Peer") && member == QLatin1String("Ping"))
        return message.createReply();

    if (interface == QLatin1String("org.freedesktop.DBus.Introspectable")
        && member == QLatin1String("Introspect"))
        return message.createReply(introspect(path));

    if (interface == QLatin1String("org.a11y.Bus") && member == QLatin1String("GetAddress"))
        return message.createReply(m_address);

    // at-spi2-core 2.44 sends RegisterEvent to the Registry interface, older/newer ones to
    // Event.Listener. Serve both: the harness must not encode one version's choice.
    if (interface == QLatin1String("org.a11y.atspi.Event.Listener")
        || interface == QLatin1String("org.a11y.atspi.Registry")) {
        if (member == QLatin1String("RegisterEvent")) {
            ++m_registerEventCalls;
            return message.createReply();
        }
        if (member == QLatin1String("DeregisterEvent")) {
            ++m_deregisterEventCalls;
            return message.createReply();
        }
    }

    if (interface == QLatin1String("org.a11y.atspi.Registry")
        && member == QLatin1String("GetDesktop"))
        return message.createReply(
            QVariant::fromValue(FakeAccessibleRef {m_appBus.baseService(), m_rootPath}));

    if (interface == QLatin1String("org.a11y.atspi.Application")
        && member == QLatin1String("GetApplicationBusAddress"))
        // This fake uses the shared accessibility bus and does not host a private peer bus.
        return message.createReply(QStringLiteral(""));

    if (interface == QLatin1String("org.a11y.atspi.Accessible")) {
        if (member == QLatin1String("GetRole")) {
            const AtspiRole role = path == m_rootPath          ? ATSPI_ROLE_APPLICATION
                : m_roleName == QLatin1String("password text") ? ATSPI_ROLE_PASSWORD_TEXT
                                                               : ATSPI_ROLE_TEXT;
            return message.createReply(QVariant::fromValue(uint(role)));
        }
        if (member == QLatin1String("GetChildren")) {
            const QList<FakeAccessibleRef> children = path == m_rootPath
                ? QList<FakeAccessibleRef> {{m_appBus.baseService(), m_textPath}}
                : QList<FakeAccessibleRef> {};
            return message.createReply(QVariant::fromValue(children));
        }
        if (member == QLatin1String("GetName"))
            return message.createReply(path == m_rootPath ? m_appName : m_roleName);
        if (member == QLatin1String("GetRoleName"))
            return message.createReply(m_roleName);
        if (member == QLatin1String("GetInterfaces"))
            return message.createReply(QStringList {QStringLiteral("org.a11y.atspi.Accessible"),
                QStringLiteral("org.a11y.atspi.Text")});
        if (member == QLatin1String("GetApplication"))
            return message.createReply(
                QVariant::fromValue(FakeAccessibleRef {m_appBus.baseService(), m_rootPath}));
        if (member == QLatin1String("GetChildCount"))
            return message.createReply(0);
    }

    if (interface == QLatin1String("org.a11y.atspi.Text")) {
        if (member == QLatin1String("GetNSelections")) {
            return message.createReply(m_hasSelection ? 1 : 0);
        }
        if (member == QLatin1String("GetSelection")) {
            // Two out arguments, not a struct: dbind rejects a struct reply with
            // 'returned signature (ii); expected ii'.
            return message.createReply(QVariantList {m_start, m_end});
        }
        if (member == QLatin1String("GetText")) {
            const QVariantList arguments = message.arguments();
            const int from = arguments.value(0).toInt();
            const int to = arguments.value(1).toInt();
            return message.createReply(textSlice(from, to));
        }
        if (member == QLatin1String("GetCharacterCount"))
            return message.createReply(m_text.size());
        if (member == QLatin1String("GetRangeExtents"))
            return message.createReply(QVariantList {120, 60, 200, 24});  // x, y, width, height
    }

    if (interface == QLatin1String("org.freedesktop.DBus.Properties")
        && member == QLatin1String("Get")) {
        const QString property = message.arguments().value(1).toString();
        if (property == QLatin1String("Name"))
            return message.createReply(
                QVariant::fromValue(QDBusVariant(path == m_rootPath ? m_appName : m_roleName)));
        if (property == QLatin1String("RoleName"))
            return message.createReply(QVariant::fromValue(QDBusVariant(m_roleName)));
        if (property == QLatin1String("Interfaces"))
            return message.createReply(QVariant::fromValue(QDBusVariant(
                QVariant(QStringList {QStringLiteral("Accessible"), QStringLiteral("Text")}))));
        if (property == QLatin1String("ChildCount"))
            return message.createReply(QVariant::fromValue(QDBusVariant(QVariant(0))));
    }

    return {};
}

QString FakeAtspiService::textSlice(int from, int to) const {
    const int start = qBound(0, from, m_text.size());
    const int end = qBound(0, to, m_text.size());
    return end <= start ? QString() : m_text.mid(start, end - start);
}
