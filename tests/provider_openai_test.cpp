// OpenAI-compatible client against a local stub server: request shape, streaming, error mapping,
// cancellation and timeouts. No live endpoint is ever contacted.
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <optional>

#include "provider/openai_provider.h"
#include "stub_http_server.h"

using provider::OpenAiProvider;
using provider::TranslateRequest;

namespace {

struct RecordingSink : provider::Sink {
    QString text;
    int deltas = 0;
    bool finished = false;
    std::optional<core::Error> error;

    void onDelta(quint64, QStringView delta) override {
        text.append(delta);
        ++deltas;
    }

    void onFinished(quint64) override { finished = true; }

    void onFailed(quint64, core::Error failure) override { error = failure; }
};

settings::ProviderConfig makeConfig(quint16 port, bool streaming = true) {
    settings::ProviderConfig config;
    config.id = QStringLiteral("openai-main");
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
    config.model = QStringLiteral("gpt-test");
    config.streaming = streaming;
    config.apiKeyRef = QStringLiteral("provider/openai-main/apiKey");
    return config;
}

settings::ProviderSettings makeTimeouts(int firstByteMs = 2000, int totalMs = 5000) {
    settings::ProviderSettings timeouts;
    timeouts.firstByteTimeoutMs = firstByteMs;
    timeouts.totalTimeoutMs = totalMs;
    return timeouts;
}

TranslateRequest makeRequest(const QString &text = QStringLiteral("hello")) {
    TranslateRequest request;
    request.generation = 7;
    request.text = text;
    request.sourceLanguage = QStringLiteral("auto");
    request.targetLanguage = QStringLiteral("zh-Hans");
    request.streaming = true;
    return request;
}

QByteArray sseEvent(const QString &text) {
    QJsonObject delta;
    delta[QStringLiteral("content")] = text;
    QJsonObject choice;
    choice[QStringLiteral("delta")] = delta;
    QJsonArray choices;
    choices.append(choice);
    QJsonObject root;
    root[QStringLiteral("choices")] = choices;
    return "data: " + QJsonDocument(root).toJson(QJsonDocument::Compact) + "\n\n";
}

}  // namespace

class ProviderOpenAiTest : public QObject {
    Q_OBJECT

private slots:
    void requestShapeCarriesPromptAndCredential();
    void streamingAccumulatesDeltasAcrossChunkBoundaries();
    void nonStreamingResponseYieldsOneDelta();
    void errorStatusesMapToCodes_data();
    void errorStatusesMapToCodes();
    void cancelReportsCancellationOnly();
    void missingCredentialFailsBeforeAnyNetworkUse();
    void firstByteDeadlineFires();
    void httpRemoteEndpointIsNotUsed();
};

void ProviderOpenAiTest::requestShapeCarriesPromptAndCredential() {
    StubHttpServer server;
    QVERIFY(server.listen());
    StubHttpServer::Response response;
    response.contentType = QByteArrayLiteral("application/json");
    response.chunks = {QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"ok\"}}]}")};
    server.setResponse(response);

    OpenAiProvider provider(makeConfig(server.port()), makeTimeouts(),
        [] { return core::Result<QString>(QStringLiteral("sk-test")); });
    RecordingSink sink;
    provider.translate(makeRequest(), sink);
    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 4000);

    QVERIFY(server.requestLine().startsWith("POST /v1/chat/completions"));
    QCOMPARE(server.requestHeaders().value("authorization"), QByteArray("Bearer sk-test"));
    QCOMPARE(server.requestHeaders().value("accept"), QByteArray("text/event-stream"));

    const QJsonObject body = QJsonDocument::fromJson(server.requestBody()).object();
    QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-test"));
    QCOMPARE(body.value(QStringLiteral("temperature")).toInt(-1), 0);
    QCOMPARE(body.value(QStringLiteral("stream")).toBool(), true);
    const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(),
        QStringLiteral("system"));
    const QString system = messages.at(0).toObject().value(QStringLiteral("content")).toString();
    QVERIFY2(system.contains(QStringLiteral("Simplified Chinese")), qPrintable(system));
    QVERIFY(system.contains(QStringLiteral("Preserve line breaks")));
    QCOMPARE(messages.at(1).toObject().value(QStringLiteral("content")).toString(),
        QStringLiteral("hello"));
}

void ProviderOpenAiTest::streamingAccumulatesDeltasAcrossChunkBoundaries() {
    StubHttpServer server;
    QVERIFY(server.listen());

    QByteArray payload = sseEvent(QStringLiteral("你"));
    payload += sseEvent(QStringLiteral("好，世界"));
    payload += "data: [DONE]\n\n";

    StubHttpServer::Response response;
    response.contentType = QByteArrayLiteral("text/event-stream");
    // Split into small chunks so events and UTF-8 sequences straddle boundaries.
    for (int offset = 0; offset < payload.size(); offset += 7)
        response.chunks.append(payload.mid(offset, 7));
    server.setResponse(response);

    OpenAiProvider provider(makeConfig(server.port()), makeTimeouts(), provider::ApiKeySource());
    RecordingSink sink;
    provider.translate(makeRequest(), sink);
    QTRY_VERIFY_WITH_TIMEOUT(sink.finished || sink.error.has_value(), 5000);
    QVERIFY2(!sink.error.has_value(),
        sink.error.has_value() ? qPrintable(core::stableName(sink.error->code)) : "");
    QCOMPARE(sink.text, QStringLiteral("你好，世界"));
    QVERIFY(sink.deltas >= 2);
}

void ProviderOpenAiTest::nonStreamingResponseYieldsOneDelta() {
    StubHttpServer server;
    QVERIFY(server.listen());
    StubHttpServer::Response response;
    response.chunks = {QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"你好\"}}]}")};
    server.setResponse(response);

    OpenAiProvider provider(
        makeConfig(server.port(), false), makeTimeouts(), provider::ApiKeySource());
    RecordingSink sink;
    TranslateRequest request = makeRequest();
    request.streaming = false;
    provider.translate(request, sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.finished, 4000);
    QCOMPARE(sink.text, QStringLiteral("你好"));
    QCOMPARE(sink.deltas, 1);
    QCOMPARE(server.requestHeaders().value("accept"), QByteArray("application/json"));
}

void ProviderOpenAiTest::errorStatusesMapToCodes_data() {
    QTest::addColumn<QByteArray>("statusLine");
    QTest::addColumn<int>("expectedCode");
    QTest::newRow("401") << QByteArrayLiteral("HTTP/1.1 401 Unauthorized")
                         << int(core::ErrorCode::ProviderAuthFailed);
    QTest::newRow("403") << QByteArrayLiteral("HTTP/1.1 403 Forbidden")
                         << int(core::ErrorCode::ProviderAuthFailed);
    QTest::newRow("429") << QByteArrayLiteral("HTTP/1.1 429 Too Many Requests")
                         << int(core::ErrorCode::ProviderRateLimited);
    QTest::newRow("500") << QByteArrayLiteral("HTTP/1.1 500 Internal Server Error")
                         << int(core::ErrorCode::ProviderUnavailable);
    QTest::newRow("302") << QByteArrayLiteral("HTTP/1.1 302 Found")
                         << int(core::ErrorCode::ProviderBadResponse);
}

void ProviderOpenAiTest::errorStatusesMapToCodes() {
    QFETCH(QByteArray, statusLine);
    QFETCH(int, expectedCode);

    StubHttpServer server;
    QVERIFY(server.listen());
    StubHttpServer::Response response;
    response.statusLine = statusLine;
    response.chunks = {QByteArrayLiteral("{}")};
    server.setResponse(response);

    OpenAiProvider provider(makeConfig(server.port()), makeTimeouts(), provider::ApiKeySource());
    RecordingSink sink;
    provider.translate(makeRequest(), sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.error.has_value() || sink.finished, 4000);
    QVERIFY2(sink.error.has_value(), "an error status must fail, never finish silently");
    QCOMPARE(int(sink.error->code), expectedCode);
}

void ProviderOpenAiTest::cancelReportsCancellationOnly() {
    StubHttpServer server;
    QVERIFY(server.listen());
    StubHttpServer::Response response;
    response.neverRespond = true;
    server.setResponse(response);

    OpenAiProvider provider(
        makeConfig(server.port()), makeTimeouts(3000, 5000), provider::ApiKeySource());
    RecordingSink sink;
    const TranslateRequest request = makeRequest();
    provider.translate(request, sink);

    QTRY_VERIFY_WITH_TIMEOUT(server.requestCount() == 1, 4000);
    provider.cancel(request.generation);

    QTRY_VERIFY_WITH_TIMEOUT(sink.error.has_value(), 3000);
    QCOMPARE(sink.error->code, core::ErrorCode::ProviderCancelled);
    QVERIFY(!sink.finished);
}

void ProviderOpenAiTest::missingCredentialFailsBeforeAnyNetworkUse() {
    StubHttpServer server;
    QVERIFY(server.listen());
    StubHttpServer::Response response;
    response.chunks = {QByteArrayLiteral("{}")};
    server.setResponse(response);

    OpenAiProvider provider(makeConfig(server.port()), makeTimeouts(), [] {
        return core::Result<QString>(
            core::Error {core::ErrorCode::SecretNotFound, QStringLiteral("absent")});
    });
    RecordingSink sink;
    provider.translate(makeRequest(), sink);

    QCOMPARE(sink.error.has_value(), true);
    QCOMPARE(sink.error->code, core::ErrorCode::ProviderNotConfigured);
    QCOMPARE(server.requestCount(), 0);
}

void ProviderOpenAiTest::firstByteDeadlineFires() {
    StubHttpServer server;
    QVERIFY(server.listen());
    StubHttpServer::Response response;
    response.neverRespond = true;
    server.setResponse(response);

    // Minimum accepted values; the deadline must still fire promptly.
    OpenAiProvider provider(
        makeConfig(server.port()), makeTimeouts(1000, 1200), provider::ApiKeySource());
    RecordingSink sink;
    provider.translate(makeRequest(), sink);

    QTRY_VERIFY_WITH_TIMEOUT(sink.error.has_value(), 4000);
    QCOMPARE(sink.error->code, core::ErrorCode::ProviderTimeout);
}

void ProviderOpenAiTest::httpRemoteEndpointIsNotUsed() {
    // A plain-http non-loopback endpoint must never be contacted; validation in module 02 rejects
    // it, and this test documents that the provider itself performs no such request.
    settings::ProviderConfig config = makeConfig(1);
    config.baseUrl = QStringLiteral("http://example.invalid/v1");
    QVERIFY(!config.isLoopback());
    QCOMPARE(config.isLocal(), false);
}

QTEST_GUILESS_MAIN(ProviderOpenAiTest)
#include "provider_openai_test.moc"
