// M0 option A: AT-SPI through raw QtDBus. Answers one question only: can a plain QtDBus client
// subscribe to object:text-selection-changed and receive events? Attribution of an event to its
// source application is NOT answered here, because QtDBus signal slots do not expose the D-Bus
// message sender (see the M0 findings in RESULTS.md).
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

#include <cstdio>

#include "common.h"

class DbusEventCounter : public QObject {
    Q_OBJECT
public:
    explicit DbusEventCounter(probe::Recorder *recorder)
        : m_recorder(recorder) { }

    int count() const { return m_count; }

    double firstEventMs() const { return m_firstEventMs; }

public slots:

    void onTextSelectionChanged(const QString &detail1, int detail2, const QDBusVariant &anyData,
        const QVariantMap &properties) {
        Q_UNUSED(anyData)
        ++m_count;
        const double at = m_watch.ms();
        if (m_count == 1)
            m_firstEventMs = at;
        probe::line(
            QStringLiteral("event=%1 t=%2ms detail1=%3 detail2=%4 properties=%5")
                .arg(m_count)
                .arg(at, 0, 'f', 1)
                .arg(detail1)
                .arg(detail2)
                .arg(properties.isEmpty() ? QStringLiteral("{}") : QStringLiteral("{...}")));
        if (m_recorder) {
            QJsonObject record;
            record[QStringLiteral("backend")] = QStringLiteral("atspi-qtdbus");
            record[QStringLiteral("seq")] = m_count;
            record[QStringLiteral("observe_ms")] = at;
            record[QStringLiteral("detail1")] = detail1;
            m_recorder->write(record);
        }
    }

private:
    probe::Recorder *m_recorder = nullptr;
    probe::Stopwatch m_watch;
    int m_count = 0;
    double m_firstEventMs = -1.0;
};

int runAtspiDbusEvents(int durationMs, const QString &outPath) {
    if (!QDBusConnection::sessionBus().isConnected()) {
        probe::line(QStringLiteral("qtdbus: session bus not reachable — cannot probe AT-SPI"));
        return 2;
    }

    const QDBusMessage addressCall = QDBusMessage::createMethodCall(QStringLiteral("org.a11y.Bus"),
        QStringLiteral("/org/a11y/bus"), QStringLiteral("org.a11y.Bus"),
        QStringLiteral("GetAddress"));
    const QDBusMessage addressReply
        = QDBusConnection::sessionBus().call(addressCall, QDBus::Block, 3000);
    if (addressReply.type() != QDBusMessage::ReplyMessage || addressReply.arguments().isEmpty()) {
        probe::line(QStringLiteral("qtdbus: org.a11y.Bus.GetAddress failed — %1")
                        .arg(addressReply.errorMessage()));
        return 2;
    }
    const QString address = addressReply.arguments().first().toString();

    QDBusConnection a11y
        = QDBusConnection::connectToBus(address, QStringLiteral("snip_probe_a11y"));
    probe::line(QStringLiteral("qtdbus: accessibility bus connected = %1")
                    .arg(a11y.isConnected() ? "yes" : "NO"));
    if (!a11y.isConnected())
        return 2;

    QDBusMessage addMatch = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("AddMatch"));
    addMatch.setArguments(
        {QStringLiteral("type='signal',interface='org.a11y.atspi.Event.Object'")});
    const QDBusMessage addMatchReply = a11y.call(addMatch, QDBus::Block, 3000);
    probe::line(QStringLiteral("qtdbus: AddMatch = %1")
                    .arg(addMatchReply.type() == QDBusMessage::ReplyMessage
                            ? "ok"
                            : addMatchReply.errorMessage()));

    // Report the registry's own idea of the signature: the first real run showed `RegisterEvent` is
    // not uniformly (s, a{sv}) — at-spi2-core 2.44 exports the one-argument form.
    const QDBusMessage introspectCall = QDBusMessage::createMethodCall(
        QStringLiteral("org.a11y.atspi.Registry"), QStringLiteral("/org/a11y/atspi/registry"),
        QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
    const QDBusMessage introspectReply = a11y.call(introspectCall, QDBus::Block, 3000);
    if (introspectReply.type() == QDBusMessage::ReplyMessage) {
        const QString xml = introspectReply.arguments().value(0).toString();
        const int at = int(xml.indexOf(QStringLiteral("name=\"RegisterEvent\"")));
        probe::line(at >= 0 ? QStringLiteral("qtdbus: registry declares %1")
                                  .arg(xml.mid(qMax(0, at - 60), 200).simplified())
                            : QStringLiteral("qtdbus: registry does not declare RegisterEvent"));
    }

    auto registerOnce = [&a11y](const QVariantList &arguments, QString *message) {
        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.a11y.atspi.Registry"), QStringLiteral("/org/a11y/atspi/registry"),
            QStringLiteral("org.a11y.atspi.Event.Listener"), QStringLiteral("RegisterEvent"));
        call.setArguments(arguments);
        const QDBusMessage reply = a11y.call(call, QDBus::Block, 3000);
        const bool ok = reply.type() == QDBusMessage::ReplyMessage;
        if (!ok)
            *message = reply.errorMessage();
        return ok;
    };

    QString registerError;
    QString registerForm;
    bool registered = false;
    if (registerOnce(
            {QStringLiteral("object:text-selection-changed"), QVariant::fromValue(QVariantMap())},
            &registerError)) {
        registered = true;
        registerForm = QStringLiteral("(s, a{sv})");
    } else {
        QString oneArgError;
        if (registerOnce({QStringLiteral("object:text-selection-changed")}, &oneArgError)) {
            registered = true;
            registerForm = QStringLiteral("(s)");
            probe::line(
                QStringLiteral("qtdbus: the (s, a{sv}) form was rejected, the (s) form works"));
        } else {
            registerError += QStringLiteral(" | ") + oneArgError;
        }
    }
    probe::line(QStringLiteral("qtdbus: RegisterEvent %1 = %2%3")
                    .arg(registerForm.isEmpty() ? QStringLiteral("(both forms)") : registerForm,
                        registered ? "ok" : "FAILED",
                        registered ? QString() : QStringLiteral(" — ") + registerError));

    probe::Recorder recorder(outPath);
    DbusEventCounter counter(&recorder);
    const bool connected
        = a11y.connect(QString(), QString(), QStringLiteral("org.a11y.atspi.Event.Object"),
            QStringLiteral("TextSelectionChanged"), QStringLiteral("siiva{sv}"), &counter,
            SLOT(onTextSelectionChanged(QString, int, QDBusVariant, QVariantMap)));
    probe::line(QStringLiteral("qtdbus: signal connection (empty service) = %1")
                    .arg(connected ? "ok" : "FAILED"));

    probe::line(QStringLiteral("qtdbus: listening for %1 ms — select text in an application now")
                    .arg(durationMs));
    QTimer::singleShot(durationMs, QCoreApplication::instance(), &QCoreApplication::quit);

    probe::Stopwatch loopWatch;
    QCoreApplication::instance()->exec();

    probe::heading("qtdbus summary");
    probe::line(QStringLiteral("events received   : %1").arg(counter.count()));
    probe::line(QStringLiteral("first event after : %1 ms").arg(counter.firstEventMs(), 0, 'f', 1));
    probe::line(QStringLiteral("listen duration   : %1 ms").arg(loopWatch.ms(), 0, 'f', 1));
    probe::line(QStringLiteral(
        "limitation        : QtDBus slot invocation does not expose the D-Bus sender,"));
    probe::line(QStringLiteral(
        "                    so the source path/application cannot be resolved here."));
    return counter.count() > 0 ? 0 : 1;
}

#include "atspi_dbus.moc"
