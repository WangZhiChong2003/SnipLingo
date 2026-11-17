#include "core/log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QProcessEnvironment>

#include <cstdio>

namespace core::log {
namespace {

constexpr qint64 kMaxFileBytes = 1024 * 1024;
constexpr int kKeptFiles = 3;

Level g_minimum = Level::Info;
QFile g_file;
QMutex g_mutex;

const char *levelTag(Level level) {
    switch (level) {
    case Level::Debug:
        return "debug";
    case Level::Info:
        return "info";
    case Level::Warning:
        return "warn";
    case Level::Error:
        return "error";
    }
    return "info";
}

Level levelFromName(const QString &name, Level fallback) {
    const QString value = name.trimmed().toLower();
    if (value == QLatin1String("debug"))
        return Level::Debug;
    if (value == QLatin1String("info"))
        return Level::Info;
    if (value == QLatin1String("warning") || value == QLatin1String("warn"))
        return Level::Warning;
    if (value == QLatin1String("error"))
        return Level::Error;
    return fallback;
}

void rotateIfNeeded() {
    if (!g_file.isOpen() || g_file.size() < kMaxFileBytes)
        return;

    const QString path = g_file.fileName();
    g_file.close();
    for (int index = kKeptFiles - 1; index >= 1; --index) {
        const QString from = QStringLiteral("%1.%2").arg(path).arg(index);
        const QString to = QStringLiteral("%1.%2").arg(path).arg(index + 1);
        if (QFile::exists(from)) {
            QFile::remove(to);
            QFile::rename(from, to);
        }
    }
    QFile::remove(path + QStringLiteral(".1"));
    QFile::rename(path, path + QStringLiteral(".1"));
    g_file.setFileName(path);
    if (!g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        std::fprintf(stderr, "warning: cannot open log file %s\n", qPrintable(path));
}

}  // namespace

void init(Level minimum, const QString &optionalLogFile) {
    const QString environmentLevel
        = QProcessEnvironment::systemEnvironment().value(QStringLiteral("SNIPLYNGO_LOG"));
    g_minimum = environmentLevel.isEmpty() ? minimum : levelFromName(environmentLevel, minimum);

    const QMutexLocker locker(&g_mutex);
    if (g_file.isOpen())
        g_file.close();
    if (optionalLogFile.isEmpty())
        return;

    QDir().mkpath(QFileInfo(optionalLogFile).absolutePath());
    g_file.setFileName(optionalLogFile);
    if (!g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        std::fprintf(stderr, "warning: cannot open log file %s\n", qPrintable(optionalLogFile));
}

void shutdown() {
    const QMutexLocker locker(&g_mutex);
    if (g_file.isOpen())
        g_file.close();
}

bool isEnabled(Level level) {
    return static_cast<int>(level) >= static_cast<int>(g_minimum);
}

void write(Level level, std::string_view module, const QString &message) {
    if (!isEnabled(level))
        return;

    const QString line = QStringLiteral("%1 %2 [%3] %4")
                             .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                                 QString::fromLatin1(levelTag(level)),
                                 QString::fromLatin1(module.data(), int(module.size())), message);

    const QMutexLocker locker(&g_mutex);
    std::fprintf(stderr, "%s\n", qPrintable(line));
    std::fflush(stderr);
    if (g_file.isOpen()) {
        rotateIfNeeded();
        if (g_file.isOpen()) {
            g_file.write(line.toUtf8());
            g_file.write("\n");
            g_file.flush();
        }
    }
}

void writeError(std::string_view module, const Error &error) {
    if (isSilent(error.code)) {
        write(Level::Debug, module,
            QStringLiteral("code=%1 detail=%2")
                .arg(QString::fromLatin1(stableName(error.code)), error.detail));
        return;
    }
    write(Level::Warning, module,
        QStringLiteral("code=%1 detail=%2")
            .arg(QString::fromLatin1(stableName(error.code)), error.detail));
}

}  // namespace core::log
