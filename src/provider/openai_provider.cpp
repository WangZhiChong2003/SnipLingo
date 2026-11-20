#include "provider/openai_provider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QUrl>

#include "provider/prompt_builder.h"

namespace provider {
namespace {

constexpr int kMaxResponseBytes = 64 * 1024;
constexpr int kMinFirstByteMs = 1000;

core::Error makeError(core::ErrorCode code, const QString &detail) {
    return core::Error {code, detail};
}

QString contentFromDeltaPayload(const QByteArray &payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return {};
    const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return {};
    const QJsonObject choice = choices.first().toObject();
    const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
    return delta.value(QStringLiteral("content")).toString();
}

}  // namespace

OpenAiProvider::OpenAiProvider(settings::ProviderConfig config, settings::ProviderSettings timeouts,
    ApiKeySource apiKeySource, QObject *parent)
    : Provider(parent)
    , m_config(std::move(config))
    , m_timeouts(std::move(timeouts))
    , m_apiKeySource(std::move(apiKeySource))
    , m_parser([this](const QByteArray &payload) { consumePayload(payload); }) {
    if (m_config.isLoopback())
        m_network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    m_firstByteTimer.setParent(this);
    m_totalTimer.setParent(this);
}

Capabilities OpenAiProvider::capabilities() const {
    Capabilities capabilities;
    capabilities.streaming = m_config.streaming;
    capabilities.usesNetwork = !m_config.isLocal();
    return capabilities;
}

QByteArray OpenAiProvider::buildRequestDocument(const TranslateRequest &request) const {
    QJsonObject systemMessage;
    systemMessage[QStringLiteral("role")] = QStringLiteral("system");
    systemMessage[QStringLiteral("content")] = prompt::systemPrompt(request.targetLanguage);

    QJsonObject userMessage;
    userMessage[QStringLiteral("role")] = QStringLiteral("user");
    userMessage[QStringLiteral("content")] = request.text;

    QJsonArray messages;
    messages.append(systemMessage);
    messages.append(userMessage);

    QJsonObject body;
    // An empty model is omitted: some local servers reject an unknown model name.
    if (!m_config.model.isEmpty())
        body[QStringLiteral("model")] = m_config.model;
    body[QStringLiteral("temperature")] = 0;
    body[QStringLiteral("stream")] = request.streaming;
    body[QStringLiteral("messages")] = messages;

    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void OpenAiProvider::translate(const TranslateRequest &request, Sink &sink) {
    Q_ASSERT(m_reply == nullptr);  // one request at a time is the coordinator's contract

    m_sink = &sink;
    m_generation = request.generation;
    m_streaming = request.streaming && m_config.streaming;
    m_cancelled = false;
    m_firstByteSeen = false;
    m_streamedSomething = false;
    m_rawBody.clear();
    m_accumulated.clear();
    m_parser.reset();

    if (m_apiKeySource) {
        const core::Result<QString> key = m_apiKeySource();
        if (!key.ok()) {
            emitFailure(core::ErrorCode::ProviderNotConfigured, key.error().detail);
            return;
        }
        m_apiKey = key.ok() ? key.value() : QString();
    }

    QUrl url(m_config.baseUrl);
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/chat/completions"));

    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(
        QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setRawHeader("Accept", m_streaming ? "text/event-stream" : "application/json");
    if (!m_apiKey.isEmpty())
        networkRequest.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    // Redirects are refused: following one could deliver the credential to another origin.
    networkRequest.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    networkRequest.setTransferTimeout(qMax(kMinFirstByteMs, m_timeouts.totalTimeoutMs));

    const QByteArray body = buildRequestDocument(request);
    armTimeouts();

    m_reply = m_network.post(networkRequest, body);
    connect(m_reply, &QNetworkReply::readyRead, this, &OpenAiProvider::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &OpenAiProvider::onFinished);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &OpenAiProvider::onReplyError);
}

void OpenAiProvider::cancel(quint64 generation) {
    if (!m_reply || generation != m_generation)
        return;
    m_cancelled = true;
    m_reply->abort();
}

void OpenAiProvider::armTimeouts() {
    const int firstByteMs = qMax(m_timeouts.firstByteTimeoutMs, kMinFirstByteMs);
    m_firstByteTimer.start(firstByteMs, [this] {
        emitFailure(core::ErrorCode::ProviderTimeout,
            QStringLiteral("no response within %1 ms").arg(m_timeouts.firstByteTimeoutMs));
    });
    m_totalTimer.start(m_timeouts.totalTimeoutMs, [this] {
        emitFailure(core::ErrorCode::ProviderTimeout,
            QStringLiteral("total deadline of %1 ms exceeded").arg(m_timeouts.totalTimeoutMs));
    });
}

void OpenAiProvider::disarmTimeouts() {
    m_firstByteTimer.cancel();
    m_totalTimer.cancel();
}

void OpenAiProvider::onReadyRead() {
    if (!m_reply)
        return;
    m_firstByteSeen = true;
    m_firstByteTimer.cancel();

    const QByteArray chunk = m_reply->readAll();
    const QString contentType
        = m_reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    const bool looksStreaming = m_streaming && contentType.contains(QLatin1String("event-stream"));

    if (!looksStreaming) {
        m_rawBody.append(chunk);
        if (m_rawBody.size() > kMaxResponseBytes)
            emitFailure(
                core::ErrorCode::ProviderBadResponse, QStringLiteral("response exceeds size cap"));
        return;
    }

    m_streamedSomething = true;
    m_parser.feed(chunk);
}

void OpenAiProvider::consumePayload(const QByteArray &payload) {
    if (payload == "[DONE]") {
        // Completion is handled by onFinished(); the sentinel only ends the stream.
        return;
    }
    const QString delta = contentFromDeltaPayload(payload);
    if (delta.isEmpty())
        return;
    m_accumulated.append(delta);
    if (m_accumulated.size() > kMaxResponseBytes / 2) {
        emitFailure(
            core::ErrorCode::ProviderBadResponse, QStringLiteral("streamed text exceeds size cap"));
        return;
    }
    if (m_sink)
        m_sink->onDelta(
            m_generation, QStringView(m_accumulated).mid(m_accumulated.size() - delta.size()));
}

void OpenAiProvider::finishWithBody() {
    const QJsonDocument document = QJsonDocument::fromJson(m_rawBody);
    if (!document.isObject()) {
        emitFailure(
            core::ErrorCode::ProviderBadResponse, QStringLiteral("response is not a JSON object"));
        return;
    }
    const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        emitFailure(
            core::ErrorCode::ProviderBadResponse, QStringLiteral("response has no choices"));
        return;
    }
    const QString content = choices.first()
                                .toObject()
                                .value(QStringLiteral("message"))
                                .toObject()
                                .value(QStringLiteral("content"))
                                .toString();
    if (content.isEmpty()) {
        emitFailure(
            core::ErrorCode::ProviderBadResponse, QStringLiteral("response content is empty"));
        return;
    }
    m_accumulated = content;
    if (m_sink)
        m_sink->onDelta(m_generation, QStringView(m_accumulated));
    disarmTimeouts();
    m_reply->deleteLater();
    m_reply = nullptr;
    m_sink->onFinished(m_generation);
    m_accumulated.clear();
    m_apiKey.clear();
}

bool OpenAiProvider::failForHttpStatus(int status) {
    // One mapping for both paths: Qt reports 4xx/5xx through errorOccurred *and* finished, and the
    // status is the authority. Mapping only by QNetworkReply::NetworkError produced wrong codes for
    // 429 and 500 in the first test run.
    if (status < 400)
        return false;
    if (status == 401 || status == 403) {
        emitFailure(core::ErrorCode::ProviderAuthFailed, QStringLiteral("HTTP %1").arg(status));
        return true;
    }
    if (status == 429) {
        const QByteArray retryAfter = m_reply ? m_reply->rawHeader("Retry-After") : QByteArray();
        emitFailure(core::ErrorCode::ProviderRateLimited,
            retryAfter.isEmpty()
                ? QStringLiteral("HTTP 429")
                : QStringLiteral("HTTP 429, Retry-After %1").arg(QString::fromLatin1(retryAfter)));
        return true;
    }
    if (status >= 500) {
        emitFailure(core::ErrorCode::ProviderUnavailable, QStringLiteral("HTTP %1").arg(status));
        return true;
    }
    emitFailure(core::ErrorCode::ProviderBadResponse, QStringLiteral("HTTP %1").arg(status));
    return true;
}

void OpenAiProvider::onReplyError(QNetworkReply::NetworkError code) {
    if (!m_reply)
        return;
    if (m_cancelled || code == QNetworkReply::OperationCanceledError) {
        emitFailure(core::ErrorCode::ProviderCancelled, QStringLiteral("cancelled"));
        return;
    }
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (failForHttpStatus(status))
        return;
    if (code == QNetworkReply::TimeoutError) {
        emitFailure(core::ErrorCode::ProviderTimeout, m_reply->errorString());
        return;
    }
    if (code == QNetworkReply::ProtocolInvalidOperationError
        || code == QNetworkReply::ProtocolUnknownError || code == QNetworkReply::ProtocolFailure) {
        emitFailure(core::ErrorCode::ProviderBadResponse, m_reply->errorString());
        return;
    }
    emitFailure(core::ErrorCode::ProviderNetwork, m_reply->errorString());
}

void OpenAiProvider::onFinished() {
    if (!m_reply)
        return;
    disarmTimeouts();

    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (m_cancelled) {
        m_reply->deleteLater();
        m_reply = nullptr;
        m_sink->onFailed(m_generation,
            makeError(core::ErrorCode::ProviderCancelled, QStringLiteral("cancelled")));
        return;
    }
    if (status >= 300 && status < 400) {
        // Manual redirect policy: reaching this branch means the server tried to redirect, which
        // could deliver the credential to another origin.
        m_reply->deleteLater();
        m_reply = nullptr;
        emitFailure(core::ErrorCode::ProviderBadResponse,
            QStringLiteral("unexpected redirect (HTTP %1)").arg(status));
        return;
    }
    if (failForHttpStatus(status))
        return;

    if (m_reply->error() != QNetworkReply::NoError) {
        const QString message = m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        emitFailure(core::ErrorCode::ProviderNetwork, message);
        return;
    }

    if (m_streamedSomething) {
        m_parser.finish();
        if (m_accumulated.isEmpty()) {
            m_reply->deleteLater();
            m_reply = nullptr;
            emitFailure(
                core::ErrorCode::ProviderBadResponse, QStringLiteral("stream produced no text"));
            return;
        }
        m_reply->deleteLater();
        m_reply = nullptr;
        m_sink->onFinished(m_generation);
        m_accumulated.clear();
        m_apiKey.clear();
        return;
    }

    finishWithBody();
}

void OpenAiProvider::emitFailure(core::ErrorCode code, const QString &detail) {
    disarmTimeouts();
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_apiKey.clear();
    m_accumulated.clear();
    if (m_sink)
        m_sink->onFailed(m_generation, makeError(code, detail));
}

}  // namespace provider
