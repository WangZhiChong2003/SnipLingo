// Private D-Bus daemon for tests: TCP transport with anonymous auth, so it also works where UNIX
// sockets are unavailable (sandboxes, restricted CI containers) and where a normal session bus must
// not be touched.
#pragma once

#include <QFile>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>

class PrivateTestBus {
public:
    bool start();
    void stop();

    [[nodiscard]] QString address() const { return m_address; }

    [[nodiscard]] QString error() const { return m_error; }

private:
    QProcess m_daemon;
    QTemporaryDir m_workspace;
    QString m_address;
    QString m_error;
};

inline bool PrivateTestBus::start() {
    if (!m_workspace.isValid()) {
        m_error = QStringLiteral("no temporary directory");
        return false;
    }
    const QString configPath = m_workspace.filePath(QStringLiteral("testbus.conf"));
    QFile config(configPath);
    if (!config.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_error = QStringLiteral("cannot write the bus configuration");
        return false;
    }
    config.write(
        "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
        " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
        "<busconfig>\n"
        "  <type>session</type>\n"
        "  <listen>tcp:host=127.0.0.1,port=0</listen>\n"
        "  <auth>ANONYMOUS</auth>\n"
        "  <allow_anonymous/>\n"
        "  <policy context=\"default\">\n"
        "    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n"
        "    <allow eavesdrop=\"true\"/>\n"
        "    <allow own=\"*\"/>\n"
        "    <allow user=\"*\"/>\n"
        "  </policy>\n"
        "</busconfig>\n");
    config.close();

    m_daemon.setProgram(QStringLiteral("dbus-daemon"));
    m_daemon.setArguments({QStringLiteral("--config-file=") + configPath,
        QStringLiteral("--nofork"), QStringLiteral("--print-address")});
    m_daemon.start();
    if (!m_daemon.waitForStarted(5000)) {
        m_error = m_daemon.errorString();
        return false;
    }
    if (!m_daemon.waitForReadyRead(5000)) {
        m_error = QStringLiteral("dbus-daemon printed no address");
        return false;
    }
    m_address = QString::fromUtf8(m_daemon.readLine()).trimmed();
    if (!m_address.startsWith(QStringLiteral("tcp:"))) {
        m_error = QStringLiteral("unexpected bus address: %1").arg(m_address);
        return false;
    }
    return true;
}

inline void PrivateTestBus::stop() {
    if (m_daemon.state() != QProcess::NotRunning) {
        m_daemon.terminate();
        m_daemon.waitForFinished(3000);
    }
}
