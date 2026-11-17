// Shared helpers for the M0 selection probe. Throwaway code: no production logic lives here.
#pragma once

#include <QByteArray>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace probe {

// Stable short digest of user text, so repeated selections can be correlated
// without ever printing or storing the content.
inline QString digest8(const QString &text)
{
    const QByteArray hex = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hex.left(8));
}

// One JSON object per line, appended. Used to compare runs offline.
class Recorder
{
public:
    explicit Recorder(const QString &path)
    {
        if (path.isEmpty())
            return;
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::Append | QIODevice::Text))
            std::fprintf(stderr, "warning: cannot write %s\n", qPrintable(path));
    }

    bool enabled() const { return m_file.isOpen(); }

    void write(const QJsonObject &object)
    {
        if (!enabled())
            return;
        m_file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        m_file.write("\n");
        m_file.flush();
    }

private:
    QFile m_file;
};

class Stats
{
public:
    void add(double value) { m_values.push_back(value); }
    int count() const { return int(m_values.size()); }

    double percentile(double p) const
    {
        if (m_values.empty())
            return 0.0;
        std::vector<double> sorted = m_values;
        std::sort(sorted.begin(), sorted.end());
        const auto index = size_t(std::llround(p / 100.0 * double(sorted.size() - 1)));
        return sorted[std::min(index, sorted.size() - 1)];
    }

    double min() const { return m_values.empty() ? 0.0 : *std::min_element(m_values.begin(), m_values.end()); }
    double max() const { return m_values.empty() ? 0.0 : *std::max_element(m_values.begin(), m_values.end()); }

private:
    std::vector<double> m_values;
};

class Stopwatch
{
public:
    Stopwatch() { m_timer.start(); }
    double ms() const { return double(m_timer.nsecsElapsed()) / 1.0e6; }

private:
    QElapsedTimer m_timer;
};

// Resolves the accessibility bus address. Empty string plus a reason when unreachable.
// Used as a pre-flight check: calling atspi_init() against a missing bus aborts the process inside
// at-spi2-core's dbind, which a diagnostic tool must never do.
inline QString accessibilityBusAddress(QString *reason)
{
    if (!QDBusConnection::sessionBus().isConnected()) {
        *reason = QStringLiteral("session bus not reachable (dbus socket unavailable?)");
        return {};
    }
    const QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.a11y.Bus"),
                                                             QStringLiteral("/org/a11y/bus"),
                                                             QStringLiteral("org.a11y.Bus"),
                                                             QStringLiteral("GetAddress"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        *reason = reply.errorMessage().isEmpty() ? QStringLiteral("org.a11y.Bus.GetAddress failed")
                                                 : reply.errorMessage();
        return {};
    }
    return reply.arguments().first().toString();
}

inline void heading(const char *title)
{
    std::printf("\n== %s ==\n", title);
    std::fflush(stdout);
}

inline void line(const QString &text)
{
    std::printf("%s\n", qPrintable(text));
    std::fflush(stdout);
}

} // namespace probe
