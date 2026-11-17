// XDG paths: environment handling, defaults and private directory creation.
#include "core/paths.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class PathsTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void honoursXdgEnvironment();
    void settingsAndLogFilesLiveUnderTheirBaseDirectories();
    void createsDirectoriesWithOwnerOnlyPermissions();
    void testOverrideWinsUntilCleared();
    void fallsBackToHomeWithTheApplicationDirectory();

private:
    QTemporaryDir m_root;
    QString m_config;
    QString m_home;
};

void PathsTest::init() {
    QVERIFY(m_root.isValid());
    m_config = m_root.filePath(QStringLiteral("config"));
    m_home = m_root.filePath(QStringLiteral("home"));
    qputenv("XDG_CONFIG_HOME", m_config.toLocal8Bit());
    qputenv("XDG_DATA_HOME", m_root.filePath(QStringLiteral("data")).toLocal8Bit());
    qputenv("XDG_STATE_HOME", m_root.filePath(QStringLiteral("state")).toLocal8Bit());
    qputenv("XDG_CACHE_HOME", m_root.filePath(QStringLiteral("cache")).toLocal8Bit());
    qputenv("HOME", m_home.toLocal8Bit());
}

void PathsTest::cleanup() {
    core::XdgPaths::clearTestOverride();
    qunsetenv("XDG_CONFIG_HOME");
    qunsetenv("XDG_DATA_HOME");
    qunsetenv("XDG_STATE_HOME");
    qunsetenv("XDG_CACHE_HOME");
}

void PathsTest::honoursXdgEnvironment() {
    const core::XdgPaths paths = core::XdgPaths::resolve();
    QCOMPARE(paths.configDir(), m_config + QStringLiteral("/sniplyngo"));
    QCOMPARE(
        paths.dataDir(), m_root.filePath(QStringLiteral("data")) + QStringLiteral("/sniplyngo"));
    QCOMPARE(
        paths.stateDir(), m_root.filePath(QStringLiteral("state")) + QStringLiteral("/sniplyngo"));
    QCOMPARE(
        paths.cacheDir(), m_root.filePath(QStringLiteral("cache")) + QStringLiteral("/sniplyngo"));
}

void PathsTest::settingsAndLogFilesLiveUnderTheirBaseDirectories() {
    const core::XdgPaths paths = core::XdgPaths::resolve();
    QCOMPARE(paths.settingsFile(), paths.configDir() + QStringLiteral("/settings.json"));
    QCOMPARE(paths.logFile(), paths.stateDir() + QStringLiteral("/logs/sniplyngo.log"));
}

void PathsTest::createsDirectoriesWithOwnerOnlyPermissions() {
    const core::XdgPaths paths = core::XdgPaths::resolve();
    const auto ensured = paths.ensureDirectories();
    QVERIFY2(ensured.ok(), qPrintable(ensured.ok() ? QString() : ensured.error().detail));

    for (const QString &directory : {paths.configDir(), paths.dataDir(), paths.stateDir()}) {
        const QFileInfo info(directory);
        QVERIFY2(info.isDir(), qPrintable(directory));
        const QFileDevice::Permissions permissions = info.permissions();
        QVERIFY2(!permissions.testFlag(QFileDevice::ReadGroup), qPrintable(directory));
        QVERIFY2(!permissions.testFlag(QFileDevice::WriteGroup), qPrintable(directory));
        QVERIFY2(!permissions.testFlag(QFileDevice::ReadOther), qPrintable(directory));
        QVERIFY2(!permissions.testFlag(QFileDevice::WriteOther), qPrintable(directory));
    }

    // Idempotent: a second call must not fail on existing directories.
    QVERIFY(paths.ensureDirectories().ok());
}

void PathsTest::testOverrideWinsUntilCleared() {
    const core::XdgPaths fromEnvironment = core::XdgPaths::resolve();
    QVERIFY(fromEnvironment.configDir().startsWith(m_config));

    core::XdgPaths overridePaths = fromEnvironment;
    core::XdgPaths::overrideForTests(overridePaths);
    QCOMPARE(core::XdgPaths::resolve().settingsFile(), fromEnvironment.settingsFile());

    qputenv("XDG_CONFIG_HOME", m_root.filePath(QStringLiteral("other")).toLocal8Bit());
    QCOMPARE(
        core::XdgPaths::resolve().configDir(), overridePaths.configDir());  // override still wins

    core::XdgPaths::clearTestOverride();
    QVERIFY(core::XdgPaths::resolve().configDir().contains(QStringLiteral("other")));
}

void PathsTest::fallsBackToHomeWithTheApplicationDirectory() {
    // Every XDG variable unset: the fallback must still append the application directory, otherwise
    // settings and logs would land in the user's shared ~/.config and ~/.local/state.
    for (const char *variable :
        {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"})
        qunsetenv(variable);

    const core::XdgPaths paths = core::XdgPaths::resolve();
    QCOMPARE(paths.configDir(), m_home + QStringLiteral("/.config/sniplyngo"));
    QCOMPARE(paths.dataDir(), m_home + QStringLiteral("/.local/share/sniplyngo"));
    QCOMPARE(paths.stateDir(), m_home + QStringLiteral("/.local/state/sniplyngo"));
    QCOMPARE(paths.cacheDir(), m_home + QStringLiteral("/.cache/sniplyngo"));
    QCOMPARE(paths.settingsFile(), paths.configDir() + QStringLiteral("/settings.json"));
}

QTEST_GUILESS_MAIN(PathsTest)
#include "core_paths_test.moc"
