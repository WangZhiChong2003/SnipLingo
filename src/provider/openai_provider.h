// OpenAI-compatible chat-completions client.
#pragma once

#include "core/delay.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "provider/provider.h"
#include "provider/sse_parser.h"

namespace provider {

class OpenAiProvider : public Provider {
    Q_OBJECT
public:
    OpenAiProvider(settings::ProviderConfig config, settings::ProviderSettings timeouts,
        ApiKeySource apiKeySource, QObject *parent = nullptr);

    [[nodiscard]] QString id() const override { return QStringLiteral("openai-compatible"); }

    [[nodiscard]] Capabilities capabilities() const override;
    void translate(const TranslateRequest &request, Sink &sink) override;
    void cancel(quint64 generation) override;

    // Exposed for tests: the exact request body that would be sent for `request`.
    [[nodiscard]] QByteArray buildRequestDocument(const TranslateRequest &request) const;

private:
    void armTimeouts();
    void disarmTimeouts();
    void onReadyRead();
    void onFinished();
    void onReplyError(QNetworkReply::NetworkError code);
    void emitFailure(core::ErrorCode code, const QString &detail);
    // Maps an HTTP status to a failure; returns false when the status is not a failure.
    bool failForHttpStatus(int status);
    void consumePayload(const QByteArray &payload);
    void finishWithBody();

    settings::ProviderConfig m_config;
    settings::ProviderSettings m_timeouts;
    ApiKeySource m_apiKeySource;

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    Sink *m_sink = nullptr;
    SseParser m_parser;

    core::CancellableDelay m_firstByteTimer;
    core::CancellableDelay m_totalTimer;

    quint64 m_generation = 0;
    bool m_streaming = false;
    bool m_cancelled = false;
    bool m_firstByteSeen = false;
    bool m_streamedSomething = false;
    QByteArray m_rawBody;
    QString m_accumulated;
    QString m_apiKey;  // cleared after every request
};

}  // namespace provider
