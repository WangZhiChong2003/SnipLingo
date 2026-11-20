#include "core/paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace core {
namespace {

QString baseFromEnvironment(const char *variable, QStandardPaths::StandardLocation fallback,
    const QString &home, const QString &relative) {
    const QString value
        = QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(variable));
    if (!value.isEmpty())
        return value + QStringLiteral("/sniplyngo");
    if (!home.isEmpty())
        return home + QLatin1Char('/') + relative + QStringLiteral("/sniplyngo");
    const QString standard = QStandardPaths::writableLocation(fallback);
    if (!standard.isEmpty())
        return standard + QStringLiteral("/sniplyngo");
    return QStringLiteral("/tmp/sniplyngo");
}

XdgPaths g_testOverride;
bool g_hasTestOverride = false;

}  // namespace

XdgPaths XdgPaths::resolve() {
    if (g_hasTestOverride)
        return g_testOverride;

    const QString home = QProcessEnvironment::systemEnvironment().value(QStringLiteral("HOME"));

    XdgPaths paths;
    paths.m_config = baseFromEnvironment(
        "XDG_CONFIG_HOME", QStandardPaths::AppConfigLocation, home, QStringLiteral(".config"));
    paths.m_data = baseFromEnvironment(
        "XDG_DATA_HOME", QStandardPaths::AppDataLocation, home, QStringLiteral(".local/share"));
    paths.m_state = baseFromEnvironment("XDG_STATE_HOME", QStandardPaths::AppLocalDataLocation,
        home, QStringLiteral(".local/state"));
    paths.m_cache = baseFromEnvironment(
        "XDG_CACHE_HOME", QStandardPaths::CacheLocation, home, QStringLiteral(".cache"));
    return paths;
}

void XdgPaths::overrideForTests(const XdgPaths &paths) {
    g_testOverride = paths;
    g_hasTestOverride = true;
}

void XdgPaths::clearTestOverride() {
    g_testOverride = XdgPaths {};
    g_hasTestOverride = false;
}

QString XdgPaths::settingsFile() const {
    return m_config + QStringLiteral("/settings.json");
}

QString XdgPaths::logFile() const {
    return m_state + QStringLiteral("/logs/sniplyngo.log");
}

Result<void> XdgPaths::ensureDirectories() const {
    const QStringList directories {m_config, m_data, m_state, m_state + QStringLiteral("/logs")};
    for (const QString &directory : directories) {
        if (QFileInfo::exists(directory))
            continue;
        if (!QDir().mkpath(directory))
            return Error {
                ErrorCode::Internal, QStringLiteral("cannot create directory: %1").arg(directory)};
        // Configuration may hold endpoint URLs and consent; keep it private to the user.
        QFile::setPermissions(
            directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    return {};
}

}  // namespace core
