// One-shot engines: request shape, response parsing, error mapping and cancellation.
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
        : api == SimpleApi::DeepL                   ? QStringLiteral("deepl")
        : api == SimpleApi::BingFree                ? QStringLiteral("bing-free")
        : api == SimpleApi::Youdao                  ? QStringLiteral("youdao")
        : api == SimpleApi::HuoshanWeb              ? QStringLiteral("huoshanweb")
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
    void deepLUsesItsOfficialApi();
    void deepLRequiresAKeyAndReportsQuota();
    void googleFreeJoinsSentences();
    void malformedAnswersAreRejected();
    void httpStatusesDecideTheError();
    void cancellationReportsCancelled();
    void bingUsesTheWebEndpoint();
    void youdaoUsesTheDemoEndpoint();
    void huoshanWebUsesTheWebEndpoint();
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
             QStringLiteral("google-free"), QStringLiteral("bing-free"), QStringLiteral("youdao"),
             QStringLiteral("huoshanweb")}) {
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

void ProviderSimpleTranslateTest::deepLUsesItsOfficialApi() {
    respond(R"({"translations":[{"detected_source_language":"EN","text":"你好"}]})");
    SimpleTranslateProvider engine(configFor(SimpleApi::DeepL, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::DeepL,
        []() -> core::Result<QString> { return QStringLiteral("test-key"); });
    TranslateRequest automatic = request(QStringLiteral("hello"));
    automatic.sourceLanguage = QStringLiteral("auto");
    RecordingSink sink;
    engine.translate(automatic, sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 3000);
    QVERIFY(m_server.requestLine().startsWith("POST /v2/translate "));
    QCOMPARE(
        m_server.requestHeaders().value("authorization"), QByteArray("DeepL-Auth-Key test-key"));
    const QJsonObject body = QJsonDocument::fromJson(m_server.requestBody()).object();
    QCOMPARE(body.value(QStringLiteral("text")).toArray(), QJsonArray {QStringLiteral("hello")});
    QCOMPARE(body.value(QStringLiteral("target_lang")).toString(), QStringLiteral("ZH-HANS"));
    QVERIFY(!body.contains(QStringLiteral("source_lang")));
    QCOMPARE(sink.streamed, QStringLiteral("你好"));

    automatic.sourceLanguage = QStringLiteral("zh-Hans");
    automatic.targetLanguage = QStringLiteral("en");
    const QJsonObject reverse = QJsonDocument::fromJson(engine.buildBody(automatic)).object();
    QCOMPARE(reverse.value(QStringLiteral("source_lang")).toString(), QStringLiteral("ZH"));
    QCOMPARE(reverse.value(QStringLiteral("target_lang")).toString(), QStringLiteral("EN-US"));
}

void ProviderSimpleTranslateTest::deepLRequiresAKeyAndReportsQuota() {
    SimpleTranslateProvider missing(configFor(SimpleApi::DeepL, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::DeepL, {});
    RecordingSink missingKey;
    const int before = m_server.requestCount();
    missing.translate(request(QStringLiteral("hello")), missingKey);
    QVERIFY(missingKey.failed);
    QCOMPARE(missingKey.error.code, core::ErrorCode::ProviderNotConfigured);
    QCOMPARE(m_server.requestCount(), before);

    respond(R"({"message":"quota"})", 456);
    SimpleTranslateProvider keyed(configFor(SimpleApi::DeepL, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::DeepL,
        []() -> core::Result<QString> { return QStringLiteral("test-key"); });
    RecordingSink quota;
    keyed.translate(request(QStringLiteral("hello")), quota);
    QTRY_VERIFY_WITH_TIMEOUT(quota.failed, 3000);
    QCOMPARE(quota.error.code, core::ErrorCode::ProviderRateLimited);
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

void ProviderSimpleTranslateTest::bingUsesTheWebEndpoint() {
    respond(R"([{"translations":[{"text":"你好","to":"zh-Hans"}]}])");
    SimpleTranslateProvider engine(configFor(SimpleApi::BingFree, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::BingFree, {});

    RecordingSink sink;
    engine.translate(request(QStringLiteral("hello")), sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 3000);
    QCOMPARE(sink.streamed, QStringLiteral("你好"));
    QVERIFY(m_server.requestLine().startsWith("POST /translate/translatetext?from=en&to=zh-Hans"));
    QVERIFY(m_server.requestLine().contains("isEnterpriseClient=false"));
    const QJsonArray body = QJsonDocument::fromJson(m_server.requestBody()).array();
    QCOMPARE(body.at(0).toString(), QStringLiteral("hello"));
}

void ProviderSimpleTranslateTest::youdaoUsesTheDemoEndpoint() {
    respond(R"({"errorCode":"0","translation":["你好"]})");
    SimpleTranslateProvider engine(configFor(SimpleApi::Youdao, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::Youdao, {});

    RecordingSink sink;
    engine.translate(request(QStringLiteral("hello")), sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 3000);
    QCOMPARE(sink.streamed, QStringLiteral("你好"));
    QVERIFY(m_server.requestLine().startsWith("POST /trans"));
    QCOMPARE(m_server.requestBody(), QByteArrayLiteral("from=en&to=zh-CHS&q=hello"));
}

void ProviderSimpleTranslateTest::huoshanWebUsesTheWebEndpoint() {
    respond(R"({"translation":"你好","base_resp":{"status_code":0}})");
    SimpleTranslateProvider engine(configFor(SimpleApi::HuoshanWeb, m_server.port()),
        settings::ProviderSettings {}, SimpleApi::HuoshanWeb, {});

    RecordingSink sink;
    engine.translate(request(QStringLiteral("hello")), sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 3000);
    QCOMPARE(sink.streamed, QStringLiteral("你好"));
    QVERIFY(m_server.requestLine().startsWith("POST /crx/translate/v1/"));
    const QJsonObject body = QJsonDocument::fromJson(m_server.requestBody()).object();
    QCOMPARE(body.value(QStringLiteral("source_language")).toString(), QStringLiteral("en"));
    QCOMPARE(body.value(QStringLiteral("target_language")).toString(), QStringLiteral("zh"));
    QCOMPARE(body.value(QStringLiteral("text")).toString(), QStringLiteral("hello"));
}

QTEST_GUILESS_MAIN(ProviderSimpleTranslateTest)
#include "provider_simple_translate_test.moc"
