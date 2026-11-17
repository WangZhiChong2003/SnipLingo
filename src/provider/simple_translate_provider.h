// Keyless translation engines that speak a simple one-shot HTTP API rather than chat completions.
//
// One-shot HTTP services: MyMemory, LibreTranslate, Google gtx and a legacy Bing endpoint.
// Availability of public and unofficial services can change independently of the application.
#pragma once

#include "core/delay.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "provider/provider.h"

namespace provider {

enum class SimpleApi {
    MyMemory,
    LibreTranslate,
    GoogleFree,
    BingFree,  // edge.microsoft.com token + api-edge translate: no key, unofficial
};

// Maps a settings provider type ("mymemory", "libretranslate", "google-free") to an API flavour.
[[nodiscard]] bool simpleApiForType(const QString &type, SimpleApi *api);

// Default configuration for a preset, used by the settings UI. Empty model and no credential
// reference.
[[nodiscard]] settings::ProviderConfig simplePreset(const QString &type);

// Converts a BCP-47 tag from settings to the code an endpoint expects. Empty input means "auto".
[[nodiscard]] QString simpleLanguageTag(SimpleApi api, const QString &tag);

// MyMemory requires an explicit source; its auto mode guesses Chinese or English. The other
// providers perform their own language detection.
[[nodiscard]] QString simpleSourceTag(SimpleApi api, const QString &tag, const QString &text);

class SimpleTranslateProvider : public Provider {
    Q_OBJECT
public:
    SimpleTranslateProvider(settings::ProviderConfig config, settings::ProviderSettings timeouts,
        SimpleApi api, ApiKeySource apiKeySource, QObject *parent = nullptr,
        QString tokenUrl = QString());

    [[nodiscard]] QString id() const override { return QStringLiteral("simple-translate"); }

    [[nodiscard]] Capabilities capabilities() const override;
    void translate(const TranslateRequest &request, Sink &sink) override;
    void cancel(quint64 generation) override;

    // Exposed for tests: the exact URL and body that would be sent.
    [[nodiscard]] QUrl buildUrl(const TranslateRequest &request) const;
    [[nodiscard]] QByteArray buildBody(const TranslateRequest &request) const;

private:
    void onFinished();
    void onReplyError(QNetworkReply::NetworkError code);
    // Bing needs a short-lived token before the translation request.
    void startTokenRequest();
    void startTranslateRequest();
    [[nodiscard]] bool tokenValid() const;
    void fail(core::ErrorCode code, const QString &detail, bool abortReply = true);
    [[nodiscard]] core::Result<QString> parsePayload(const QByteArray &payload) const;

    settings::ProviderConfig m_config;
    settings::ProviderSettings m_timeouts;
    SimpleApi m_api = SimpleApi::MyMemory;
    ApiKeySource m_apiKeySource;

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    Sink *m_sink = nullptr;
    core::CancellableDelay m_totalTimer;
    quint64 m_generation = 0;
    bool m_finished = false;

    enum class Stage { Token, Translate };
    Stage m_stage = Stage::Translate;
    TranslateRequest m_request;
    QString m_tokenUrl;
    QString m_token;
    qint64 m_tokenExpiresAtMs = 0;
};

}  // namespace provider
