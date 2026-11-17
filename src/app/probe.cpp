#include "app/probe.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QProcessEnvironment>

#ifdef SNIPLYNGO_HAVE_X11
#include "platform/x11/x11_backend.h"
#endif

namespace app {
namespace {

constexpr int kCallTimeoutMs = 2000;

int portalVersion(QDBusConnection &bus, const QString &interface) {
    QDBusMessage call
        = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    call.setArguments({interface, QStringLiteral("version")});
    const QDBusMessage reply = bus.call(call, QDBus::Block, kCallTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return -1;
    return reply.arguments().first().value<QDBusVariant>().variant().toInt();
}

bool serviceRegistered(QDBusConnection &bus, const QString &name) {
    QDBusConnectionInterface *interface = bus.interface();
    if (!interface)
        return false;
    const QDBusReply<bool> reply = interface->isServiceRegistered(name);
    return reply.isValid() && reply.value();
}

bool accessibilityBusReachable(QDBusConnection &bus) {
    const QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.a11y.Bus"),
        QStringLiteral("/org/a11y/bus"), QStringLiteral("org.a11y.Bus"),
        QStringLiteral("GetAddress"));
    const QDBusMessage reply = bus.call(call, QDBus::Block, kCallTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return false;
    return !reply.arguments().first().toString().isEmpty();
}

}  // namespace

secrets::ServiceProbe probeSecretService() {
    secrets::ServiceProbe probe;
    QDBusConnection bus = QDBusConnection::sessionBus();
    probe.sessionBusConnected = bus.isConnected();
    if (!probe.sessionBusConnected) {
        probe.reason = QStringLiteral("session bus unreachable");
        return probe;
    }
    probe.secretServiceRegistered
        = serviceRegistered(bus, QStringLiteral("org.freedesktop.secrets"));
    probe.reason = probe.secretServiceRegistered
        ? QStringLiteral(
            "org.freedesktop.secrets is present; persistent storage is not implemented yet")
        : QStringLiteral("org.freedesktop.secrets is not present in this session");
    return probe;
}

SessionFacts probeSessionFacts(const secrets::AvailabilityReport &secrets) {
    SessionFacts facts;
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    facts.sessionType = environment.value(QStringLiteral("XDG_SESSION_TYPE"));
    facts.desktop = environment.value(QStringLiteral("XDG_CURRENT_DESKTOP"));
    facts.waylandDisplay = environment.value(QStringLiteral("WAYLAND_DISPLAY"));
    facts.display = environment.value(QStringLiteral("DISPLAY"));
    facts.secrets = secrets;

#ifdef SNIPLYNGO_HAVE_X11
    if (!facts.display.isEmpty()) {
        X11Backend backend;
        facts.x11PrimaryAvailable = backend.probe().accessGranted;
    }
#endif

    QDBusConnection bus = QDBusConnection::sessionBus();
    facts.sessionBusConnected = bus.isConnected();
    if (!facts.sessionBusConnected)
        return facts;

    facts.accessibilityBusReachable = accessibilityBusReachable(bus);
    facts.secretServiceRegistered
        = serviceRegistered(bus, QStringLiteral("org.freedesktop.secrets"));

    for (const char *interface :
        {"org.freedesktop.portal.Screenshot", "org.freedesktop.portal.ScreenCast",
            "org.freedesktop.portal.GlobalShortcuts", "org.freedesktop.portal.Secret"}) {
        const QString name = QString::fromLatin1(interface);
        facts.portalVersions.append({name, portalVersion(bus, name)});
    }
    return facts;
}

}  // namespace app
