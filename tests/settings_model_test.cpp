// Settings model: defaults, validation/clamping, provider rules, fingerprint and the consent truth
// table.
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

#include "settings/settings.h"

using namespace settings;

namespace {

ProviderConfig remoteProvider() {
    ProviderConfig provider;
    provider.id = QStringLiteral("openai-main");
    provider.label = QStringLiteral("OpenAI");
    provider.baseUrl = QStringLiteral("https://api.openai.com/v1");
    provider.model = QStringLiteral("gpt-4o-mini");
    provider.apiKeyRef = QStringLiteral("provider/openai-main/apiKey");
    return provider;
}

ProviderConfig localProvider() {
    ProviderConfig provider;
    provider.id = QStringLiteral("ollama");
    provider.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
    provider.model = QStringLiteral("qwen2.5");
    return provider;
}

Settings withProvider(const ProviderConfig &provider) {
    Settings settings;
    settings.provider.providers = {provider};
    settings.provider.activeId = provider.id;
    settings.language.target = QStringLiteral("zh-Hans");
    return settings;
}

}  // namespace

class SettingsModelTest : public QObject {
    Q_OBJECT

private slots:
    void defaultsAreOffAndRequireExplicitConsent();
    void validationClampsBoundedFields();
    void validationResetsUnknownBackend();
    void validationResetsUnknownUiLanguage();
    void plainHttpIsRejectedForRemoteEndpointsButAllowedForLoopback();
    void invalidProviderEntriesAreDropped();
    void fingerprintIgnoresCosmeticUrlDifferences();
    void fingerprintChangesWithDestination();
    void consentMustBeAValidHash();
    void mayAutoSendTruthTable();
    void invalidLanguageTagsAreCleared();
    void jsonRoundTripPreservesKnownFields();
};

void SettingsModelTest::defaultsAreOffAndRequireExplicitConsent() {
    Settings settings;
    QCOMPARE(settings.version, 1);
    QVERIFY(!settings.selection.automaticEnabled);
    QVERIFY(!settings.selection.paused);
    QVERIFY(!settings.selection.showResultImmediately);
    QVERIFY(!settings.selection.xwaylandPopup);
    QCOMPARE(settings.selection.backend, QStringLiteral("auto"));
    QVERIFY(!settings.mayAutoSend());
    QVERIFY(settings.activeProvider() == nullptr);
    QCOMPARE(settings.ui.language, QStringLiteral("system"));
}

void SettingsModelTest::validationClampsBoundedFields() {
    Settings settings;
    settings.selection.stabilityDelayMs = 5;
    settings.selection.maxSelectionChars = 999999;
    settings.selection.resolveTimeoutMs = 1;
    settings.ui.scale = 9.0;
    settings.ui.fontScale = 9.0;
    settings.provider.firstByteTimeoutMs = 100;
    settings.provider.totalTimeoutMs = 50;

    QStringList issues;
    QVERIFY(validate(settings, &issues));

    QCOMPARE(settings.selection.stabilityDelayMs, 100);
    QCOMPARE(settings.selection.maxSelectionChars, 20000);
    QCOMPARE(settings.selection.resolveTimeoutMs, 200);
    QCOMPARE(settings.ui.scale, 1.5);
    QCOMPARE(settings.ui.fontScale, 1.75);
    QCOMPARE(settings.provider.firstByteTimeoutMs, 1000);
    QCOMPARE(settings.provider.totalTimeoutMs, 1000);  // raised to at least firstByte
    QVERIFY(issues.contains(QStringLiteral("selection.stabilityDelayMs")));
    QVERIFY(issues.contains(QStringLiteral("ui.scale")));
    QVERIFY(issues.contains(QStringLiteral("ui.fontScale")));
    QVERIFY(issues.contains(QStringLiteral("provider.totalTimeoutMs")));
}

void SettingsModelTest::validationResetsUnknownBackend() {
    Settings settings;
    settings.selection.backend = QStringLiteral("telepathy");
    QStringList issues;
    validate(settings, &issues);
    QCOMPARE(settings.selection.backend, QStringLiteral("auto"));
    QVERIFY(issues.contains(QStringLiteral("selection.backend")));
}

void SettingsModelTest::validationResetsUnknownUiLanguage() {
    Settings settings;
    settings.ui.language = QStringLiteral("klingon");
    QStringList issues;
    validate(settings, &issues);
    QCOMPARE(settings.ui.language, QStringLiteral("system"));
    QVERIFY(issues.contains(QStringLiteral("ui.language")));
}

void SettingsModelTest::plainHttpIsRejectedForRemoteEndpointsButAllowedForLoopback() {
    Settings settings;
    ProviderConfig remote = remoteProvider();
    remote.baseUrl = QStringLiteral("http://example.com/v1");
    ProviderConfig loopback = localProvider();
    settings.provider.providers = {remote, loopback};
    settings.provider.activeId = remote.id;

    QStringList issues;
    validate(settings, &issues);

    QCOMPARE(settings.provider.providers.size(), 1);
    QCOMPARE(settings.provider.providers.first().id, loopback.id);
    QVERIFY(settings.provider.activeId.isEmpty());
    QVERIFY(issues.contains(QStringLiteral("provider.baseUrl(http-remote)")));
}

void SettingsModelTest::invalidProviderEntriesAreDropped() {
    Settings settings;
    ProviderConfig noScheme = remoteProvider();
    noScheme.id = QStringLiteral("no-scheme");
    noScheme.baseUrl = QStringLiteral("api.openai.com/v1");
    ProviderConfig badId = remoteProvider();
    badId.id = QStringLiteral("Bad_Id");
    ProviderConfig query = remoteProvider();
    query.id = QStringLiteral("with-query");
    query.baseUrl = QStringLiteral("https://example.com/v1?key=1");
    settings.provider.providers = {remoteProvider(), noScheme, badId, query, remoteProvider()};

    QStringList issues;
    validate(settings, &issues);

    QCOMPARE(settings.provider.providers.size(), 1);
    QCOMPARE(settings.provider.providers.first().id, QStringLiteral("openai-main"));
    QVERIFY(issues.contains(QStringLiteral("provider.id")));
    QVERIFY(issues.contains(QStringLiteral("provider.baseUrl")));
}

void SettingsModelTest::fingerprintIgnoresCosmeticUrlDifferences() {
    ProviderConfig a = remoteProvider();
    ProviderConfig b = remoteProvider();
    b.baseUrl
        = QStringLiteral("https://API.OpenAI.com/v1/");  // case + trailing slash + default port
    QCOMPARE(a.fingerprint(), b.fingerprint());

    ProviderConfig c = remoteProvider();
    c.baseUrl = QStringLiteral("https://api.openai.com:443/v1");
    QCOMPARE(a.fingerprint(), c.fingerprint());
}

void SettingsModelTest::fingerprintChangesWithDestination() {
    const ProviderConfig base = remoteProvider();
    ProviderConfig otherHost = base;
    otherHost.baseUrl = QStringLiteral("https://api.example.com/v1");
    ProviderConfig otherModel = base;
    otherModel.model = QStringLiteral("gpt-4o");
    ProviderConfig otherPath = base;
    otherPath.baseUrl = QStringLiteral("https://api.openai.com/v2");

    QVERIFY(base.fingerprint() != otherHost.fingerprint());
    QVERIFY(base.fingerprint() != otherModel.fingerprint());
    QVERIFY(base.fingerprint() != otherPath.fingerprint());
}

void SettingsModelTest::consentMustBeAValidHash() {
    Settings settings = withProvider(remoteProvider());
    settings.consent.fingerprint = QStringLiteral("not-a-hash");
    QStringList issues;
    QVERIFY(validate(settings, &issues));
    QVERIFY(!settings.consent.valid());
    QVERIFY(issues.contains(QStringLiteral("consent.fingerprint")));
}

void SettingsModelTest::mayAutoSendTruthTable() {
    const ProviderConfig remote = remoteProvider();
    const ProviderConfig local = localProvider();

    struct Row {
        const char *name;
        bool enabled;
        bool paused;
        bool useLocal;
        bool grantConsent;
        bool expected;
    };

    const Row rows[] = {
        {"disabled", false, false, false, true, false},
        {"paused", true, true, false, true, false},
        {"remote without consent", true, false, false, false, false},
        {"remote with consent", true, false, false, true, true},
        {"local without consent", true, false, true, false, true},
        {"local paused", true, true, true, false, false},
    };

    for (const Row &row : rows) {
        const ProviderConfig provider = row.useLocal ? local : remote;
        Settings settings = withProvider(provider);
        settings.selection.automaticEnabled = row.enabled;
        settings.selection.paused = row.paused;
        if (row.grantConsent) {
            settings.consent.fingerprint = provider.fingerprint();
            settings.consent.destinationLabel = provider.destinationLabel();
        }
        QVERIFY2(settings.mayAutoSend() == row.expected, row.name);
    }

    // Consent for one destination must not authorise another.
    Settings changed = withProvider(remote);
    changed.selection.automaticEnabled = true;
    changed.consent.fingerprint = remote.fingerprint();
    QVERIFY(changed.mayAutoSend());
    changed.provider.providers.first().baseUrl = QStringLiteral("https://evil.example.com/v1");
    QVERIFY(!changed.mayAutoSend());
}

void SettingsModelTest::invalidLanguageTagsAreCleared() {
    Settings settings;
    settings.language.source = QStringLiteral("not a tag!");
    settings.language.target = QStringLiteral("!!");
    QStringList issues;
    validate(settings, &issues);
    QCOMPARE(settings.language.source, QStringLiteral("auto"));
    QVERIFY(settings.language.target.isEmpty());
    QVERIFY(issues.contains(QStringLiteral("language.target")));
}

void SettingsModelTest::jsonRoundTripPreservesKnownFields() {
    Settings settings = withProvider(remoteProvider());
    settings.selection.automaticEnabled = true;
    settings.selection.showResultImmediately = true;
    settings.selection.xwaylandPopup = true;
    settings.language.source = QStringLiteral("en");
    settings.consent.fingerprint = remoteProvider().fingerprint();
    settings.ui.theme = QStringLiteral("dark");
    settings.ui.language = QStringLiteral("zh-Hans");

    const LoadResult loaded = loadFromDocument(toJson(settings), true);
    QCOMPARE(loaded.outcome, Outcome::Loaded);
    QVERIFY(loaded.settings.selection.automaticEnabled);
    QVERIFY(loaded.settings.selection.showResultImmediately);
    QVERIFY(loaded.settings.selection.xwaylandPopup);
    QCOMPARE(loaded.settings.language.source, QStringLiteral("en"));
    QCOMPARE(loaded.settings.provider.providers.size(), 1);
    QCOMPARE(loaded.settings.provider.activeId, QStringLiteral("openai-main"));
    QCOMPARE(loaded.settings.consent.fingerprint, remoteProvider().fingerprint());
    QCOMPARE(loaded.settings.ui.theme, QStringLiteral("dark"));
    QCOMPARE(loaded.settings.ui.language, QStringLiteral("zh-Hans"));
    QVERIFY(loaded.issues.isEmpty());
}

QTEST_GUILESS_MAIN(SettingsModelTest)
#include "settings_model_test.moc"
