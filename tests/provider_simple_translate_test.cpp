// Keyless engines: request shape, response parsing, error mapping and cancellation.
// Everything runs against the local stub server, so no test depends on a third-party service.
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QtTest>

#include "fakes/stub_http_server.h"
#include "provider/simple_translate_provider.h"

using provider::SimpleApi;
using provider::SimpleTranslateProvider;
using provider::TranslateRequest;

namespace {

struct RecordingSink : provider::Sink {
    QString streamed;
    int deltas = 0;
    bool finished = false;
    bool failed = false;
    core::Error error;

    void onDelta(quint64, QStringView delta) override {
        ++deltas;
        streamed += delta.toString();
    }

    void onFinished(quint64) override { finished = true; }

    void onFailed(quint64, core::Error failure) override {
        failed = true;
        error = failure;
    }
};

settings::ProviderConfig configFor(SimpleApi api, quint16 port) {
    const QString type = api == SimpleApi::MyMemory ? QStringLiteral("mymemory")
        : api == SimpleApi::LibreTranslate          ? QStringLiteral("libretranslate")
                                                    : QStringLiteral("google-free");
    settings::ProviderConfig config = provider::simplePreset(type);
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    return config;
}

TranslateRequest request(const QString &text) {
    TranslateRequest request;
    request.generation = 7;
    request.text = text;
    request.sourceLanguage = QStringLiteral("en");
    request.targetLanguage = QStringLiteral("zh-Hans");
    return request;
}

}  // namespace

class ProviderSimpleTranslateTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void languageTagsAreEndpointSpecific();
    void presetsAreKeyless();
    void myMemorySendsADocumentedRequest();
    void myMemoryQuotaInA200IsRateLimited();
    void libreTranslatePostsJson();
    void googleFreeJoinsSentences();
    void malformedAnswersAreRejected();
    void httpStatusesDecideTheError();
    void cancellationReportsCancelled();
    void bingFetchesATokenThenTranslates();
    void bingReusesTheToken();
    void automaticLanguageUsesTheProviderCapability();
    void libreTranslateSendsAnOptionalKeyInTheBody();
    void myMemoryEnforcesItsByteLimit();
    void localServiceBypassesTheApplicationProxy();

private:
    void respond(const QByteArray &body, int status = 200);

    StubHttpServer m_server;
};

void ProviderSimpleTranslateTest::init() {
    // QTcpServer refuses a second listen(), so the socket is opened once per test binary.
    if (m_server.port() == 0)
        QVERIFY(m_server.listen());
    m_server.setResponse(StubHttpServer::Response {});
}

void ProviderSimpleTranslateTest::cleanup() {
    m_server.setResponse(StubHttpServer::Response {});
}

void ProviderSimpleTranslateTest::respond(const QByteArray &body, int status) {
    StubHttpServer::Response response;
    response.statusLine = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + " OK";
    response.chunks = {body};
    m_server.setResponse(response);
}

void ProviderSimpleTranslateTest::languageTagsAreEndpointSpecific() {
    // Full BCP-47 tags are not what these endpoints accept.
    QCOMPARE(provider::simpleLanguageTag(SimpleApi::MyMemory, QStringLiteral("zh-Hans")),
        QStringLiteral("zh-CN"));
    QCOMPARE(provider::simpleLanguageTag(SimpleApi::GoogleFree, QStringLiteral("zh-Hant")),
        QStringLiteral("zh-TW"));
    QCOMPARE(provider::simpleLanguageTag(SimpleApi::LibreTranslate, QStringLiteral("zh-Hans")),
        QStringLiteral("zh"));
    QCOMPARE(provider::simpleLanguageTag(SimpleApi::MyMemory, QStringLiteral("en")),
        QStringLiteral("en"));
}

void ProviderSimpleTranslateTest::presetsAreKeyless() {
    for (const QString &type : {QStringLiteral("mymemory"), QStringLiteral("libretranslate"),
             QStringLiteral("google-free")}) {
        SimpleApi api {};
        QVERIFY(provider::simpleApiForType(type, &api));
        const settings::ProviderConfig preset = provider::simplePreset(type);
        QCOMPARE(preset.type, type);
        QVERIFY2(preset.apiKeyRef.isEmpty(), "a keyless preset must not ask for a credential");
        QVERIFY(!preset.baseUrl.isEmpty());
        QVERIFY(!preset.streaming);
    }
    SimpleApi unused {};
    QVERIFY(!provider::simpleApiForType(QStringLiteral("openai-compatible"), &unused));
}

void ProviderSimpleTranslateTest::myMemorySendsADocumentedRequest() {
    respond(R"({"responseData":{"translatedText":"你好"},"responseStatus":200})");
    SimpleTranslateProvider engine(configFor(SimpleApi::MyMemory, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::MyMemory, {});
    RecordingSink sink;
    engine.translate(request(QStringLiteral("hello")), sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 3000);
    QCOMPARE(sink.streamed, QStringLiteral("你好"));
    QCOMPARE(sink.deltas, 1);  // one shot, no fake streaming
    QVERIFY(!sink.failed);

    const QByteArray line = m_server.requestLine();
    QVERIFY(line.startsWith("GET /get?"));
    QVERIFY(line.contains("q=hello"));
    QVERIFY(line.contains("langpair=en%7Czh-CN") || line.contains("langpair=en|zh-CN"));
}

void ProviderSimpleTranslateTest::myMemoryQuotaInA200IsRateLimited() {
    respond(R"({"responseData":{"translatedText":"MYMEMORY WARNING"},"responseStatus":429,
                "responseDetails":"MYMEMORY WARNING: YOU USED ALL AVAILABLE FREE TRANSLATIONS"})");
    SimpleTranslateProvider engine(configFor(SimpleApi::MyMemory, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::MyMemory, {});
    RecordingSink sink;
    engine.translate(request(QStringLiteral("hello")), sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.failed, 3000);
    QCOMPARE(sink.error.code, core::ErrorCode::ProviderRateLimited);
}

void ProviderSimpleTranslateTest::automaticLanguageUsesTheProviderCapability() {
    TranslateRequest automatic = request(QStringLiteral("日本語です"));
    automatic.sourceLanguage = QStringLiteral("auto");
    SimpleTranslateProvider google(configFor(SimpleApi::GoogleFree, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::GoogleFree, {});
    QVERIFY(google.buildUrl(automatic).query().contains(QStringLiteral("sl=auto")));

    automatic.text = QStringLiteral("hello");
    SimpleTranslateProvider memory(configFor(SimpleApi::MyMemory, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::MyMemory, {});
    QVERIFY(memory.buildUrl(automatic).query().contains(QStringLiteral("langpair=en")));
    automatic.sourceLanguage = QStringLiteral("ja");
    QVERIFY(memory.buildUrl(automatic).query().contains(QStringLiteral("langpair=ja")));
}

void ProviderSimpleTranslateTest::libreTranslateSendsAnOptionalKeyInTheBody() {
    respond(R"({"translatedText":"你好"})");
    SimpleTranslateProvider engine(configFor(SimpleApi::LibreTranslate, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::LibreTranslate,
        []() -> core::Result<QString> { return QStringLiteral("test-key"); });
    RecordingSink sink;
    engine.translate(request(QStringLiteral("hello")), sink);
    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 3000);
    const QJsonObject body = QJsonDocument::fromJson(m_server.requestBody()).object();
    QCOMPARE(body.value(QStringLiteral("api_key")).toString(), QStringLiteral("test-key"));
    QVERIFY(!m_server.requestHeaders().contains("authorization"));
}

void ProviderSimpleTranslateTest::myMemoryEnforcesItsByteLimit() {
    SimpleTranslateProvider engine(configFor(SimpleApi::MyMemory, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::MyMemory, {});
    RecordingSink sink;
    engine.translate(request(QString(167, QChar(0x4f60))), sink);
    QVERIFY(sink.failed);
    QCOMPARE(sink.error.code, core::ErrorCode::SelectionTooLarge);
}

void ProviderSimpleTranslateTest::localServiceBypassesTheApplicationProxy() {
    struct RestoreProxy {
        QNetworkProxy previous = QNetworkProxy::applicationProxy();

        ~RestoreProxy() { QNetworkProxy::setApplicationProxy(previous); }
    } restore;

    QNetworkProxy::setApplicationProxy(
        QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 1));
    respond(R"({"translatedText":"你好"})");
    SimpleTranslateProvider engine(configFor(SimpleApi::LibreTranslate, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::LibreTranslate, {});
    RecordingSink sink;
    engine.translate(request(QStringLiteral("hello")), sink);
    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 3000);
    QCOMPARE(sink.streamed, QStringLiteral("你好"));
}

void ProviderSimpleTranslateTest::libreTranslatePostsJson() {
    respond(R"({"translatedText":"你好世界"})");
    SimpleTranslateProvider engine(configFor(SimpleApi::LibreTranslate, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::LibreTranslate, {});
    RecordingSink sink;
    engine.translate(request(QStringLiteral("hello world")), sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 3000);
    QCOMPARE(sink.streamed, QStringLiteral("你好世界"));
    QVERIFY(m_server.requestLine().startsWith("POST /translate"));

    const QJsonObject body = QJsonDocument::fromJson(m_server.requestBody()).object();
    QCOMPARE(body.value(QStringLiteral("q")).toString(), QStringLiteral("hello world"));
    QCOMPARE(body.value(QStringLiteral("source")).toString(), QStringLiteral("en"));
    QCOMPARE(body.value(QStringLiteral("target")).toString(), QStringLiteral("zh"));
}

void ProviderSimpleTranslateTest::googleFreeJoinsSentences() {
    respond(R"([[["你好，","Hello, ",null,null,1],["世界"," world",null,null,1]],null,"en"])");
    SimpleTranslateProvider engine(configFor(SimpleApi::GoogleFree, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::GoogleFree, {});
    RecordingSink sink;
    engine.translate(request(QStringLiteral("Hello, world")), sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 3000);
    QCOMPARE(sink.streamed, QStringLiteral("你好，世界"));
    QVERIFY(m_server.requestLine().contains("client=gtx"));
    QVERIFY(m_server.requestLine().contains("tl=zh-CN"));
}

void ProviderSimpleTranslateTest::malformedAnswersAreRejected() {
    respond("this is not json");
    SimpleTranslateProvider engine(configFor(SimpleApi::MyMemory, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::MyMemory, {});
    RecordingSink sink;
    engine.translate(request(QStringLiteral("hello")), sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.failed, 3000);
    QCOMPARE(sink.error.code, core::ErrorCode::ProviderBadResponse);
    QVERIFY(sink.error.detail.contains(QStringLiteral("JSON")));
}

void ProviderSimpleTranslateTest::httpStatusesDecideTheError() {
    const QList<QPair<int, core::ErrorCode>> cases {
        {401, core::ErrorCode::ProviderAuthFailed},
        {403, core::ErrorCode::ProviderAuthFailed},
        {429, core::ErrorCode::ProviderRateLimited},
        {500, core::ErrorCode::ProviderUnavailable},
        {404, core::ErrorCode::ProviderBadResponse},
    };
    for (const auto &[status, expected] : cases) {
        respond("{}", status);
        SimpleTranslateProvider engine(configFor(SimpleApi::LibreTranslate, m_server.port()),
            settings::ProviderSettings {}, SimpleApi::LibreTranslate, {});
        RecordingSink sink;
        engine.translate(request(QStringLiteral("hello")), sink);
        QTRY_VERIFY_WITH_TIMEOUT(sink.failed, 3000);
        QCOMPARE(sink.error.code, expected);
    }
}

void ProviderSimpleTranslateTest::cancellationReportsCancelled() {
    StubHttpServer::Response slow;
    slow.statusLine = QByteArrayLiteral("HTTP/1.1 200 OK");
    slow.chunks = {R"({"translatedText":"你好"})"};
    slow.neverRespond = true;
    m_server.setResponse(slow);

    SimpleTranslateProvider engine(configFor(SimpleApi::LibreTranslate, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::LibreTranslate, {});
    RecordingSink sink;
    engine.translate(request(QStringLiteral("hello")), sink);
    engine.cancel(7);

    QVERIFY(sink.failed);
    QCOMPARE(sink.error.code, core::ErrorCode::ProviderCancelled);
    QVERIFY(!sink.finished);
}

void ProviderSimpleTranslateTest::bingFetchesATokenThenTranslates() {
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(m_server.port());
    StubHttpServer::Response token;
    token.statusLine = QByteArrayLiteral("HTTP/1.1 200 OK");
    token.contentType = QByteArrayLiteral("text/plain");
    token.chunks = {QByteArrayLiteral("fake-edge-token")};
    StubHttpServer::Response translate;
    translate.statusLine = QByteArrayLiteral("HTTP/1.1 200 OK");
    translate.chunks = {R"([{"translations":[{"text":"你好","to":"zh-Hans"}]}])"};
    m_server.setSequentialResponses({token, translate});
    const int baseline = m_server.requestCount();

    settings::ProviderConfig config = provider::simplePreset(QStringLiteral("bing-free"));
    config.baseUrl = base;
    SimpleTranslateProvider engine(config, settings::ProviderSettings {}, SimpleApi::BingFree, {},
        nullptr, base + QStringLiteral("/translate/auth"));

    RecordingSink sink;
    engine.translate(request(QStringLiteral("hello")), sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 5000);
    QCOMPARE(sink.streamed, QStringLiteral("你好"));
    QCOMPARE(m_server.requestCount() - baseline, 2);
    QVERIFY2(
        m_server.requestLine().startsWith("POST /translate"), m_server.requestLine().constData());
    QVERIFY(m_server.requestLine().contains("api-version=3.0"));
    QVERIFY(m_server.requestLine().contains("to=zh-Hans"));
    QCOMPARE(m_server.requestHeaders().value("authorization"),
        QByteArrayLiteral("Bearer fake-edge-token"));

    const QJsonArray body = QJsonDocument::fromJson(m_server.requestBody()).array();
    QCOMPARE(
        body.at(0).toObject().value(QStringLiteral("Text")).toString(), QStringLiteral("hello"));
}

void ProviderSimpleTranslateTest::bingReusesTheToken() {
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(m_server.port());
    StubHttpServer::Response token;
    token.chunks = {QByteArrayLiteral("token-one")};
    StubHttpServer::Response translated;
    translated.chunks = {R"([{"translations":[{"text":"你好"}]}])"};
    m_server.setSequentialResponses({token, translated, translated});
    const int baseline = m_server.requestCount();

    settings::ProviderConfig config = provider::simplePreset(QStringLiteral("bing-free"));
    config.baseUrl = base;
    SimpleTranslateProvider engine(config, settings::ProviderSettings {}, SimpleApi::BingFree, {},
        nullptr, base + QStringLiteral("/translate/auth"));

    RecordingSink first;
    engine.translate(request(QStringLiteral("one")), first);
    QTRY_VERIFY_WITH_TIMEOUT(first.finished, 5000);

    RecordingSink second;
    engine.translate(request(QStringLiteral("two")), second);
    QTRY_VERIFY_WITH_TIMEOUT(second.finished, 5000);

    // Token + two translations: the token is not fetched again.
    QCOMPARE(m_server.requestCount() - baseline, 3);
    QCOMPARE(second.streamed, QStringLiteral("你好"));
}

QTEST_GUILESS_MAIN(ProviderSimpleTranslateTest)
#include "provider_simple_translate_test.moc"
