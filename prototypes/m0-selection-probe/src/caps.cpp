// Capability report: what this session actually exposes.
// Deliberately reports "absent"/"unreachable" instead of guessing from desktop names.
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <cstdio>
#include <cstdlib>

#include "common.h"

namespace {

QString env(const char *name) {
    const QString value = QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
    return value.isEmpty() ? QStringLiteral("<unset>") : value;
}

// Returns the accessibility bus address, or an empty string with a reason.
QString accessibilityBusAddress(QString *reason) {
    if (!QDBusConnection::sessionBus().isConnected()) {
        *reason = QStringLiteral("session bus not reachable (dbus socket unavailable?)");
        return {};
    }
    const QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.a11y.Bus"),
        QStringLiteral("/org/a11y/bus"), QStringLiteral("org.a11y.Bus"),
        QStringLiteral("GetAddress"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        *reason = reply.errorMessage().isEmpty() ? QStringLiteral("org.a11y.Bus.GetAddress failed")
                                                 : reply.errorMessage();
        return {};
    }
    return reply.arguments().first().toString();
}

// Reads the portal interface version; -1 when the interface is not exported.
int portalVersion(const QString &interface) {
    if (!QDBusConnection::sessionBus().isConnected())
        return -1;
    QDBusMessage call
        = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    call.setArguments({interface, QStringLiteral("version")});
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return -1;
    return reply.arguments().first().value<QDBusVariant>().variant().toInt();
}

QString toolPresence(const char *name) {
    const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
    return path.isEmpty() ? QStringLiteral("missing") : path;
}

}  // namespace

int runCaps(bool asJson) {
    QString a11yReason;
    const QString a11yAddress = accessibilityBusAddress(&a11yReason);

    QJsonObject a11y;
    a11y[QStringLiteral("sessionBus")] = QDBusConnection::sessionBus().isConnected();
    a11y[QStringLiteral("addressPresent")] = !a11yAddress.isEmpty();
    a11y[QStringLiteral("reason")] = a11yReason;
    // Never print the address itself in reports; whether it is reachable is what matters.

    QJsonObject portals;
    for (const char *iface :
        {"org.freedesktop.portal.Screenshot", "org.freedesktop.portal.ScreenCast",
            "org.freedesktop.portal.GlobalShortcuts", "org.freedesktop.portal.Secret"}) {
        portals[QString::fromLatin1(iface)] = portalVersion(QString::fromLatin1(iface));
    }

    QJsonObject session;
    session[QStringLiteral("XDG_SESSION_TYPE")] = env("XDG_SESSION_TYPE");
    session[QStringLiteral("XDG_CURRENT_DESKTOP")] = env("XDG_CURRENT_DESKTOP");
    session[QStringLiteral("WAYLAND_DISPLAY")] = env("WAYLAND_DISPLAY");
    session[QStringLiteral("DISPLAY")] = env("DISPLAY");
    session[QStringLiteral("XDG_RUNTIME_DIR")] = env("XDG_RUNTIME_DIR");

    QJsonObject tools;
    for (const char *name : {"wl-paste", "xclip", "xsel", "dotool", "ydotool", "grim", "slurp"})
        tools[QString::fromLatin1(name)] = toolPresence(name);

    QJsonObject root;
    root[QStringLiteral("session")] = session;
    root[QStringLiteral("accessibilityBus")] = a11y;
    root[QStringLiteral("portalVersions")] = portals;
    root[QStringLiteral("tools")] = tools;

    if (asJson) {
        std::printf("%s\n", QJsonDocument(root).toJson(QJsonDocument::Indented).constData());
        return (QDBusConnection::sessionBus().isConnected() && !a11yAddress.isEmpty()) ? 0 : 2;
    }

    probe::heading("session");
    probe::line(QStringLiteral("XDG_SESSION_TYPE   = %1")
                    .arg(session[QStringLiteral("XDG_SESSION_TYPE")].toString()));
    probe::line(QStringLiteral("XDG_CURRENT_DESKTOP= %1")
                    .arg(session[QStringLiteral("XDG_CURRENT_DESKTOP")].toString()));
    probe::line(QStringLiteral("WAYLAND_DISPLAY    = %1")
                    .arg(session[QStringLiteral("WAYLAND_DISPLAY")].toString()));
    probe::line(QStringLiteral("DISPLAY            = %1")
                    .arg(session[QStringLiteral("DISPLAY")].toString()));

    probe::heading("accessibility bus (AT-SPI candidate)");
    probe::line(QStringLiteral("session bus reachable : %1")
                    .arg(a11y[QStringLiteral("sessionBus")].toBool() ? "yes" : "NO"));
    probe::line(QStringLiteral("a11y bus address      : %1")
                    .arg(a11y[QStringLiteral("addressPresent")].toBool() ? "present" : "ABSENT"));
    if (!a11yReason.isEmpty())
        probe::line(QStringLiteral("reason                : %1").arg(a11yReason));

    probe::heading("portal interface versions (-1 = not exported)");
    for (auto it = portals.begin(); it != portals.end(); ++it)
        probe::line(QStringLiteral("%1 : %2").arg(it.key(), QString::number(it.value().toInt())));

    probe::heading("optional tools");
    for (auto it = tools.begin(); it != tools.end(); ++it)
        probe::line(
            QStringLiteral("%1 : %2").arg(it.key().leftJustified(10), it.value().toString()));

    probe::heading("interpretation");
    probe::line(
        QStringLiteral("Protocol presence is not coverage: run 'x11 selftest', 'atspi list' and"));
    probe::line(QStringLiteral(
        "'atspi watch' on this session to find out what actually emits selections."));
    return (a11y[QStringLiteral("sessionBus")].toBool()
               && a11y[QStringLiteral("addressPresent")].toBool())
        ? 0
        : 2;
}
