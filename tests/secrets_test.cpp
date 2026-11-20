// Credential storage: key-id rules, session-only behaviour and the honest factory decision.
#include <QtTest>

#include "secrets/secret_store.h"
#include "secrets/session_store.h"

using namespace secrets;

class SecretsTest : public QObject {
    Q_OBJECT

private slots:
    void keyIdValidation_data();
    void keyIdValidation();
    void sessionStoreRoundTrip();
    void sessionStoreRejectsInvalidInput();
    void missingSecretIsNotAnErrorValue();
    void clearForgetsEverything();
    void availabilityIsHonest();
    void factoryFallsBackToSessionOnlyWhenBusIsUnavailable();
};

void SecretsTest::keyIdValidation_data() {
    QTest::addColumn<QString>("key");
    QTest::addColumn<bool>("valid");
    QTest::newRow("canonical") << QStringLiteral("provider/openai-main/apiKey") << true;
    QTest::newRow("two segments") << QStringLiteral("provider/openai") << true;
    QTest::newRow("single segment") << QStringLiteral("openai") << false;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("documented shape") << QStringLiteral("provider/openai-main/apiKey") << true;
    QTest::newRow("camelCase suffix") << QStringLiteral("provider/x/apiKey") << true;
    QTest::newRow("space") << QStringLiteral("provider/open ai/apiKey") << false;
    QTest::newRow("empty segment") << QStringLiteral("provider//apiKey") << false;
    QTest::newRow("too long segment")
        << QStringLiteral("provider/%1/apiKey").arg(QString(80, QLatin1Char('a'))) << false;
    QTest::newRow("slash only") << QStringLiteral("provider/") << false;
}

void SecretsTest::keyIdValidation() {
    QFETCH(QString, key);
    QFETCH(bool, valid);
    QCOMPARE(isValidKeyId(key), valid);
}

void SecretsTest::sessionStoreRoundTrip() {
    SessionOnlyStore store;
    QVERIFY(
        store.store(QStringLiteral("provider/openai-main/apiKey"), QStringLiteral("sk-test")).ok());
    const auto found = store.lookup(QStringLiteral("provider/openai-main/apiKey"));
    QVERIFY(found.ok());
    QCOMPARE(found.value(), QStringLiteral("sk-test"));
    QCOMPARE(store.size(), 1);

    QVERIFY(store.remove(QStringLiteral("provider/openai-main/apiKey")).ok());
    QCOMPARE(store.lookup(QStringLiteral("provider/openai-main/apiKey")).error().code,
        core::ErrorCode::SecretNotFound);
}

void SecretsTest::sessionStoreRejectsInvalidInput() {
    SessionOnlyStore store;
    QCOMPARE(store.store(QStringLiteral("bad key"), QStringLiteral("x")).error().code,
        core::ErrorCode::SettingsInvalid);
    QCOMPARE(store.store(QStringLiteral("provider/x/apiKey"), QString()).error().code,
        core::ErrorCode::SettingsInvalid);
    QCOMPARE(
        store.lookup(QStringLiteral("bad key")).error().code, core::ErrorCode::SettingsInvalid);
}

void SecretsTest::missingSecretIsNotAnErrorValue() {
    SessionOnlyStore store;
    const auto missing = store.lookup(QStringLiteral("provider/none/apiKey"));
    QVERIFY(!missing.ok());
    QCOMPARE(missing.error().code, core::ErrorCode::SecretNotFound);
    // The failure detail must never contain credential material.
    QVERIFY(!missing.error().detail.contains(QStringLiteral("sk-")));
}

void SecretsTest::clearForgetsEverything() {
    SessionOnlyStore store;
    QVERIFY(store.store(QStringLiteral("provider/a/apiKey"), QStringLiteral("secret-a")).ok());
    QVERIFY(store.store(QStringLiteral("provider/b/apiKey"), QStringLiteral("secret-b")).ok());
    QCOMPARE(store.size(), 2);
    store.clear();
    QCOMPARE(store.size(), 0);
    QVERIFY(!store.lookup(QStringLiteral("provider/a/apiKey")).ok());
}

void SecretsTest::availabilityIsHonest() {
    SessionOnlyStore store;
    const AvailabilityReport report = store.availability();
    QCOMPARE(report.availability, Availability::SessionOnly);
    QCOMPARE(report.backend, QStringLiteral("session"));
    // The user must be able to understand the consequence without reading code.
    QVERIFY(report.reason.contains(QStringLiteral("memory")));
    QVERIFY(report.reason.contains(QStringLiteral("exits")));
}

void SecretsTest::factoryFallsBackToSessionOnlyWhenBusIsUnavailable() {
    ServiceProbe noBus;
    noBus.sessionBusConnected = false;
    noBus.reason = QStringLiteral("session bus unreachable");

    const std::unique_ptr<SecretStore> store = createSecretStore(noBus);
    QVERIFY(store != nullptr);
    QCOMPARE(store->availability().availability, Availability::SessionOnly);
    QVERIFY(store->availability().reason.contains(QStringLiteral("memory")));
}

QTEST_GUILESS_MAIN(SecretsTest)
#include "secrets_test.moc"
