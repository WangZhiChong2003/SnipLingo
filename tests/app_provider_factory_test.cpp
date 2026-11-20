// Exercise the real provider factory with an unauthenticated loopback server.
#include "app/provider_factory.h"
#include "app/swappable_provider.h"

#include <QNetworkProxy>
#include <QtTest>

#include "fakes/fake_provider.h"
#include "fakes/stub_http_server.h"
#include "secrets/secret_store.h"

namespace {
struct Sink : provider::Sink {
    QString text;
    bool finished = false;
    bool failed = false;

    void onDelta(quint64, QStringView delta) override { text += delta.toString(); }

    void onFinished(quint64) override { finished = true; }

    void onFailed(quint64, core::Error) override { failed = true; }
};
}  // namespace

class AppProviderFactoryTest : public QObject {
    Q_OBJECT
private slots:
    void localOpenAiServerNeedsNoKeyAndBypassesProxy();
    void replacingAProviderCancelsItsRequest();
};

void AppProviderFactoryTest::localOpenAiServerNeedsNoKeyAndBypassesProxy() {
    struct RestoreProxy {
        QNetworkProxy previous = QNetworkProxy::applicationProxy();

        ~RestoreProxy() { QNetworkProxy::setApplicationProxy(previous); }
    } restore;

    QNetworkProxy::setApplicationProxy(
        QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 1));
    StubHttpServer server;
    QVERIFY(server.listen());
    StubHttpServer::Response response;
    response.chunks = {R"({"choices":[{"message":{"content":"你好"}}]})"};
    server.setResponse(response);

    settings::ProviderConfig config;
    config.id = QStringLiteral("local");
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port());
    config.apiKeyRef = QStringLiteral("provider/local/apiKey");
    config.streaming = false;
    auto secrets = secrets::createSecretStore(secrets::ServiceProbe {});
    auto engine = app::createProvider(config, settings::ProviderSettings {}, secrets.get());
    provider::TranslateRequest request;
    request.generation = 1;
    request.text = QStringLiteral("hello");
    request.sourceLanguage = QStringLiteral("en");
    request.targetLanguage = QStringLiteral("zh-Hans");
    request.streaming = false;
    Sink sink;
    engine->translate(request, sink);
    QTRY_VERIFY_WITH_TIMEOUT(sink.finished || sink.failed, 3000);
    QVERIFY(!sink.failed);
    QCOMPARE(sink.text, QStringLiteral("你好"));
    QVERIFY(!server.requestHeaders().contains("authorization"));
}

void AppProviderFactoryTest::replacingAProviderCancelsItsRequest() {
    app::SwappableProvider engine;
    engine.set(std::make_unique<FakeProvider>());
    provider::TranslateRequest request;
    request.generation = 42;
    Sink sink;
    engine.translate(request, sink);
    engine.set(nullptr);
    QVERIFY(sink.failed);
    QVERIFY(!sink.finished);
}

QTEST_GUILESS_MAIN(AppProviderFactoryTest)
#include "app_provider_factory_test.moc"
