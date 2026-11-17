// Keyring client against a fake Secret Service on a private bus: this is what turns "credentials
// live in the desktop keyring" into a verified claim, and it now checks the wire types, not just
// the values.
#include <QDBusConnection>
#include <QLibrary>
#include <QProcess>
#include <QThread>
#include <QtTest>

#include <memory>

#include "fake_secret_service.h"
#include "secrets/secret_service_store.h"
#include "test_bus.h"

namespace {

// Temporary CI diagnostics: separate the libdbus layer, Qt's libdbus loading and Qt's own connect.
QString captured(const QString &program, const QStringList &arguments) {
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(8000))
        return QStringLiteral("<timeout>");
    return (QString::fromUtf8(process.readAllStandardOutput())
        + QString::fromUtf8(process.readAllStandardError()))
        .simplified();
}

// Calls libdbus directly so its own error text is visible; Qt throws it away.
QString libdbusOpen(const QString &address) {
    QLibrary lib(QStringLiteral("libdbus-1"), 3);
    if (!lib.load())
        return QStringLiteral("load failed: %1").arg(lib.errorString());
    using ErrorInitFn = void (*)(void *);
    using OpenFn = void *(*)(const char *, void *);
    auto init = reinterpret_cast<ErrorInitFn>(lib.resolve("dbus_error_init"));
    auto open = reinterpret_cast<OpenFn>(lib.resolve("dbus_connection_open_private"));
    if (!init || !open)
        return QStringLiteral("resolve failed: %1").arg(lib.errorString());
    alignas(16) unsigned char error[256] = {};
    init(error);
    void *connection = open(address.toUtf8().constData(), error);
    const auto *name = *reinterpret_cast<const char *const *>(error);
    const auto *message = *reinterpret_cast<const char *const *>(error + sizeof(void *));
    if (connection)
        return QStringLiteral("opened");
    return QStringLiteral("failed [%1] [%2]")
        .arg(QString::fromUtf8(name ? name : "?"), QString::fromUtf8(message ? message : "?"));
}

QString busDiagnostics(const QString &address, const QString &daemonLog) {
    return QStringLiteral("libdbus=[%1] dbus-send-bus=[%2] daemon-log=[%3]")
        .arg(libdbusOpen(address))
        .arg(captured(QStringLiteral("dbus-send"),
            {QStringLiteral("--bus=") + address, QStringLiteral("--print-reply"),
                QStringLiteral("--dest=org.freedesktop.DBus"),
                QStringLiteral("/org/freedesktop/DBus"),
                QStringLiteral("org.freedesktop.DBus.ListNames")}))
        .arg(daemonLog);
}

}  // namespace

class SecretsServiceLiveTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void availabilityReportsTheKeyring();
    void wireTypesMatchTheSecretServiceApi();
    void storeLookupRemoveRoundTrip();
    void storingAgainReplacesTheItem();
    void missingKeyIsNotFound();
    void promptedWriteIsReportedNotHidden();
    void lockedCollectionIsReportedNotHidden();
    void factoryHandsOutTheKeyringWhenTheServiceIsPresent();

private:
    template <typename Fn>
    void onFake(Fn fn) {
        const bool invoked = QMetaObject::invokeMethod(
            &m_fakeContext, std::move(fn), Qt::BlockingQueuedConnection);
        QVERIFY2(invoked, "could not reach the fake keyring thread");
    }

    [[nodiscard]] std::unique_ptr<secrets::SecretStore> makeStore() const;

    PrivateTestBus m_bus;
    QThread m_fakeThread;
    QObject m_fakeContext;
    FakeSecretService *m_service = nullptr;
};

void SecretsServiceLiveTest::initTestCase() {
    QVERIFY2(m_bus.start(), qPrintable(m_bus.error()));
    qputenv("DBUS_SESSION_BUS_ADDRESS", m_bus.address().toUtf8());

    m_fakeContext.moveToThread(&m_fakeThread);
    m_fakeThread.start();

    const QString address = m_bus.address();
    onFake([this, address] { m_service = new FakeSecretService(address); });
    bool ready = false;
    QString serviceError;
    onFake([this, &ready, &serviceError] {
        ready = m_service->ready();
        serviceError = m_service->lastError();
    });
    QVERIFY2(ready,
        qPrintable(QStringLiteral("the fake keyring is not ready: %1 [%2]")
                       .arg(serviceError, busDiagnostics(m_bus.address(), m_bus.daemonLog()))));
}

void SecretsServiceLiveTest::cleanupTestCase() {
    if (m_fakeThread.isRunning()) {
        QMetaObject::invokeMethod(
            &m_fakeContext,
            [this] {
                delete m_service;
                m_service = nullptr;
            },
            Qt::BlockingQueuedConnection);
        m_fakeThread.quit();
        m_fakeThread.wait(3000);
    }
    m_bus.stop();
}

std::unique_ptr<secrets::SecretStore> SecretsServiceLiveTest::makeStore() const {
    return std::make_unique<secrets::SecretServiceStore>(QDBusConnection::sessionBus(),
        QStringLiteral("org.freedesktop.secrets"), QStringLiteral("/org/freedesktop/secrets"));
}

void SecretsServiceLiveTest::availabilityReportsTheKeyring() {
    const auto store = makeStore();
    const secrets::AvailabilityReport report = store->availability();
    QCOMPARE(report.availability, secrets::Availability::Available);
    QCOMPARE(report.backend, QStringLiteral("secret-service"));
}

void SecretsServiceLiveTest::wireTypesMatchTheSecretServiceApi() {
    const auto store = makeStore();
    QVERIFY(store->store(QStringLiteral("provider/wire/apiKey"), QStringLiteral("sk-wire")).ok());
    QVERIFY(store->lookup(QStringLiteral("provider/wire/apiKey")).ok());

    QString searchSignature;
    QString createSignature;
    onFake([this, &searchSignature, &createSignature] {
        searchSignature = m_service->signatureOf(QStringLiteral("SearchItems"));
        createSignature = m_service->signatureOf(QStringLiteral("CreateItem"));
    });

    // Attributes are a{ss}; a QVariantMap would produce a{sv} and a real keyring would reject it.
    QCOMPARE(searchSignature, QStringLiteral("a{ss}"));
    QCOMPARE(createSignature, QStringLiteral("a{sv}(oayays)b"));
    QVERIFY(store->remove(QStringLiteral("provider/wire/apiKey")).ok());
}

void SecretsServiceLiveTest::storeLookupRemoveRoundTrip() {
    const auto store = makeStore();
    const QString key = QStringLiteral("provider/openai-main/apiKey");

    QVERIFY2(store->store(key, QStringLiteral("sk-round-trip")).ok(), "storing failed");

    int items = 0;
    QString stored;
    onFake([this, &items, &stored, key] {
        items = m_service->itemCount();
        stored = m_service->storedValue(key);
    });
    QCOMPARE(items, 1);
    QCOMPARE(stored, QStringLiteral("sk-round-trip"));

    const auto lookup = store->lookup(key);
    QVERIFY2(lookup.ok(), lookup.ok() ? "" : qPrintable(lookup.error().detail));
    QCOMPARE(lookup.value(), QStringLiteral("sk-round-trip"));

    QVERIFY(store->remove(key).ok());
    onFake([this, &items] { items = m_service->itemCount(); });
    QCOMPARE(items, 0);
    QCOMPARE(store->lookup(key).error().code, core::ErrorCode::SecretNotFound);
}

void SecretsServiceLiveTest::storingAgainReplacesTheItem() {
    const auto store = makeStore();
    const QString key = QStringLiteral("provider/ollama/apiKey");
    QVERIFY(store->store(key, QStringLiteral("first")).ok());
    QVERIFY(store->store(key, QStringLiteral("second")).ok());

    int items = 0;
    onFake([this, &items] { items = m_service->itemCount(); });
    QCOMPARE(items, 1);  // replace=true must not accumulate items
    QCOMPARE(store->lookup(key).value(), QStringLiteral("second"));
    QVERIFY(store->remove(key).ok());
}

void SecretsServiceLiveTest::missingKeyIsNotFound() {
    const auto store = makeStore();
    const auto lookup = store->lookup(QStringLiteral("provider/never-stored/apiKey"));
    QVERIFY(!lookup.ok());
    QCOMPARE(lookup.error().code, core::ErrorCode::SecretNotFound);
    QVERIFY2(!lookup.error().detail.contains(QStringLiteral("sk-")),
        "error details must never carry credential material");
}

void SecretsServiceLiveTest::promptedWriteIsReportedNotHidden() {
    onFake([this] { m_service->setRequirePromptForWrites(true); });
    const auto store = makeStore();
    const auto stored
        = store->store(QStringLiteral("provider/prompt/apiKey"), QStringLiteral("sk-x"));
    onFake([this] { m_service->setRequirePromptForWrites(false); });

    QVERIFY(!stored.ok());
    QCOMPARE(stored.error().code, core::ErrorCode::SecretStoreUnavailable);
    QVERIFY(stored.error().detail.contains(QStringLiteral("prompt")));
}

void SecretsServiceLiveTest::lockedCollectionIsReportedNotHidden() {
    onFake([this] {
        m_service->seed(QStringLiteral("provider/locked/apiKey"), QStringLiteral("sk-locked"));
        m_service->setLocked(true);
    });
    const auto store = makeStore();
    const auto lookup = store->lookup(QStringLiteral("provider/locked/apiKey"));
    onFake([this] { m_service->setLocked(false); });

    QVERIFY(!lookup.ok());
    QCOMPARE(lookup.error().code, core::ErrorCode::SecretStoreUnavailable);
    QVERIFY(lookup.error().detail.contains(QStringLiteral("locked")));
}

void SecretsServiceLiveTest::factoryHandsOutTheKeyringWhenTheServiceIsPresent() {
    secrets::ServiceProbe probe;
    probe.sessionBusConnected = true;
    probe.secretServiceRegistered = true;
    const std::unique_ptr<secrets::SecretStore> store = secrets::createSecretStore(probe);
    QVERIFY(store != nullptr);
    QCOMPARE(store->availability().availability, secrets::Availability::Available);
    QCOMPARE(store->availability().backend, QStringLiteral("secret-service"));
}

QTEST_GUILESS_MAIN(SecretsServiceLiveTest)
#include "secrets_service_live_test.moc"
