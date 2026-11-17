// SettingsStore: atomic writes, private permissions, corrupt recovery, unknown-key preservation and
// the consent lifecycle (docs/design/02-settings.md §5–§7).
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include "settings/settings_store.h"

using namespace settings;

namespace {

ProviderConfig remoteProvider() {
    ProviderConfig provider;
    provider.id = QStringLiteral("openai-main");
    provider.baseUrl = QStringLiteral("https://api.openai.com/v1");
    provider.model = QStringLiteral("gpt-4o-mini");
    provider.apiKeyRef = QStringLiteral("provider/openai-main/apiKey");
    return provider;
}

}  // namespace

class SettingsStoreTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void missingFileYieldsDefaultsWithoutCreatingOne();
    void saveCreatesAPrivateFileAndRoundTrips();
    void unknownKeysFromANewerVersionSurviveASave();
    void corruptFileIsPreservedAndDefaultsAreUsed();
    void rejectedVersionIsTreatedAsCorrupt();
    void consentRequiresAConfiguredProvider();
    void grantAndRevokeConsentDriveAutoSend();
    void pausedStateIsPersisted();

private:
    std::unique_ptr<SettingsStore> newStore();
    QString settingsPath() const;
    QJsonObject readDocument() const;

    QTemporaryDir m_root;
    core::XdgPaths m_paths;
};

void SettingsStoreTest::init() {
    QVERIFY(m_root.isValid());
    qputenv("XDG_CONFIG_HOME", m_root.filePath(QStringLiteral("config")).toLocal8Bit());
    qputenv("XDG_DATA_HOME", m_root.filePath(QStringLiteral("data")).toLocal8Bit());
    qputenv("XDG_STATE_HOME", m_root.filePath(QStringLiteral("state")).toLocal8Bit());
    qputenv("XDG_CACHE_HOME", m_root.filePath(QStringLiteral("cache")).toLocal8Bit());
    qputenv("HOME", m_root.filePath(QStringLiteral("home")).toLocal8Bit());
    m_paths = core::XdgPaths::resolve();
}

void SettingsStoreTest::cleanup() {
    for (const char *variable :
        {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME", "HOME"})
        qunsetenv(variable);
}

std::unique_ptr<SettingsStore> SettingsStoreTest::newStore() {
    return std::make_unique<SettingsStore>(m_paths);
}

QString SettingsStoreTest::settingsPath() const {
    return m_paths.settingsFile();
}

QJsonObject SettingsStoreTest::readDocument() const {
    QFile file(settingsPath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void SettingsStoreTest::missingFileYieldsDefaultsWithoutCreatingOne() {
    auto store = newStore();
    const LoadResult result = store->load();

    QCOMPARE(result.outcome, Outcome::LoadedWithDefaults);
    QVERIFY(result.issues.isEmpty());
    QVERIFY(!QFileInfo::exists(settingsPath()));
    QVERIFY(!store->current().selection.automaticEnabled);
}

void SettingsStoreTest::saveCreatesAPrivateFileAndRoundTrips() {
    auto store = newStore();
    store->load();

    Settings next = store->current();
    next.language.target = QStringLiteral("en");
    next.selection.maxSelectionChars = 1234;
    QVERIFY(store->save(next).ok());

    const QFileInfo info(settingsPath());
    QVERIFY(info.exists());
    const QFileDevice::Permissions permissions = info.permissions();
    QVERIFY2(!permissions.testFlag(QFileDevice::ReadGroup), "group must not read settings");
    QVERIFY2(!permissions.testFlag(QFileDevice::ReadOther), "others must not read settings");
    QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));

    auto reopened = newStore();
    QCOMPARE(reopened->load().outcome, Outcome::Loaded);
    QCOMPARE(reopened->current().language.target, QStringLiteral("en"));
    QCOMPARE(reopened->current().selection.maxSelectionChars, 1234);
}

void SettingsStoreTest::unknownKeysFromANewerVersionSurviveASave() {
    // Simulate a document written by a future build: an unknown section and an unknown field inside
    // a known section must both survive our save unchanged.
    QJsonObject selection;
    selection[QStringLiteral("automaticEnabled")] = true;
    selection[QStringLiteral("futureField")] = 42;

    QJsonObject futureSection;
    futureSection[QStringLiteral("experiment")] = QStringLiteral("keep-me");

    QJsonObject document;
    document[QStringLiteral("version")] = 1;
    document[QStringLiteral("selection")] = selection;
    document[QStringLiteral("futureSection")] = futureSection;

    QDir().mkpath(QFileInfo(settingsPath()).absolutePath());
    QFile file(settingsPath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(document).toJson());
    file.close();

    auto store = newStore();
    QCOMPARE(store->load().outcome, Outcome::Loaded);

    Settings next = store->current();
    next.selection.paused = true;
    QVERIFY(store->save(next).ok());

    const QJsonObject saved = readDocument();
    QCOMPARE(saved.value(QStringLiteral("futureSection"))
                 .toObject()
                 .value(QStringLiteral("experiment"))
                 .toString(),
        QStringLiteral("keep-me"));
    QCOMPARE(saved.value(QStringLiteral("selection"))
                 .toObject()
                 .value(QStringLiteral("futureField"))
                 .toInt(),
        42);
    QCOMPARE(saved.value(QStringLiteral("selection"))
                 .toObject()
                 .value(QStringLiteral("paused"))
                 .toBool(),
        true);
}

void SettingsStoreTest::corruptFileIsPreservedAndDefaultsAreUsed() {
    QDir().mkpath(QFileInfo(settingsPath()).absolutePath());
    QFile file(settingsPath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ this is not json");
    file.close();

    auto store = newStore();
    const LoadResult result = store->load();

    QCOMPARE(result.outcome, Outcome::Recovered);
    QVERIFY(result.issues.contains(QStringLiteral("file")));

    const QStringList backups
        = QDir(QFileInfo(settingsPath()).absolutePath())
              .entryList({QStringLiteral("settings.json.corrupt-*")}, QDir::Files);
    QCOMPARE(backups.size(), 1);
    QVERIFY(!store->current().selection.automaticEnabled);
}

void SettingsStoreTest::rejectedVersionIsTreatedAsCorrupt() {
    QJsonObject document;
    document[QStringLiteral("version")] = 99;
    document[QStringLiteral("selection")]
        = QJsonObject {{QStringLiteral("automaticEnabled"), true}};

    QDir().mkpath(QFileInfo(settingsPath()).absolutePath());
    QFile file(settingsPath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(document).toJson());
    file.close();

    auto store = newStore();
    const LoadResult result = store->load();
    QCOMPARE(result.outcome, Outcome::Recovered);
    QVERIFY(result.issues.contains(QStringLiteral("version")));
    QVERIFY(!store->current().selection.automaticEnabled);
}

void SettingsStoreTest::consentRequiresAConfiguredProvider() {
    auto store = newStore();
    store->load();
    const auto granted = store->grantConsent();
    QVERIFY(!granted.ok());
    QCOMPARE(granted.error().code, core::ErrorCode::ProviderNotConfigured);
}

void SettingsStoreTest::grantAndRevokeConsentDriveAutoSend() {
    auto store = newStore();
    store->load();

    Settings next = store->current();
    next.provider.providers = {remoteProvider()};
    next.provider.activeId = QStringLiteral("openai-main");
    next.language.target = QStringLiteral("zh-Hans");
    next.selection.automaticEnabled = true;
    QVERIFY(store->save(next).ok());
    QVERIFY2(!store->current().mayAutoSend(), "remote destination must require consent");

    QVERIFY(store->grantConsent().ok());
    QVERIFY(store->current().consent.valid());
    QVERIFY(store->current().mayAutoSend());

    // Consent belongs to the destination: pointing the provider elsewhere invalidates it.
    Settings changed = store->current();
    changed.provider.providers.first().baseUrl = QStringLiteral("https://api.example.com/v1");
    QVERIFY(store->save(changed).ok());
    QVERIFY2(!store->current().mayAutoSend(), "changed destination must require consent again");

    QVERIFY(store->revokeConsent().ok());
    QVERIFY(!store->current().consent.valid());
    QVERIFY(!store->current().mayAutoSend());
}

void SettingsStoreTest::pausedStateIsPersisted() {
    auto store = newStore();
    store->load();
    QVERIFY(store->setAutomaticEnabled(true).ok());
    QVERIFY(store->setPaused(true).ok());

    auto reopened = newStore();
    QCOMPARE(reopened->load().outcome, Outcome::Loaded);
    QVERIFY(reopened->current().selection.automaticEnabled);
    QVERIFY2(reopened->current().selection.paused, "pausing must survive a restart");
    QVERIFY(!reopened->current().mayAutoSend());
}

QTEST_GUILESS_MAIN(SettingsStoreTest)
#include "settings_store_test.moc"
