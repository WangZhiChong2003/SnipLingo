#include "provider/simple_translate_provider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QUrlQuery>

namespace provider {
namespace {

// Endpoints expect plain codes, not full BCP-47 tags.
QString regionCode(const QString &tag) {
    if (tag == QLatin1String("zh-Hans") || tag == QLatin1String("zh-CN"))
        return QStringLiteral("zh-CN");
    if (tag == QLatin1String("zh-Hant") || tag == QLatin1String("zh-TW"))
        return QStringLiteral("zh-TW");
    const QString language = tag.section(QLatin1Char('-'), 0, 0);
    return language.isEmpty() ? tag : language;
}

QString trimmedBase(const QString &baseUrl) {
    QString base = baseUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base;
}

}  // namespace

bool simpleApiForType(const QString &type, SimpleApi *api) {
    if (api == nullptr)
        return false;
    if (type == QLatin1String("mymemory")) {
        *api = SimpleApi::MyMemory;
        return true;
    }
    if (type == QLatin1String("libretranslate")) {
        *api = SimpleApi::LibreTranslate;
        return true;
    }
    if (type == QLatin1String("deepl")) {
        *api = SimpleApi::DeepL;
        return true;
    }
    if (type == QLatin1String("google-free")) {
        *api = SimpleApi::GoogleFree;
        return true;
    }
    if (type == QLatin1String("bing-free")) {
        *api = SimpleApi::BingFree;
        return true;
    }
    if (type == QLatin1String("youdao")) {
        *api = SimpleApi::Youdao;
        return true;
    }
    if (type == QLatin1String("huoshanweb")) {
        *api = SimpleApi::HuoshanWeb;
        return true;
    }
    return false;
}

settings::ProviderConfig simplePreset(const QString &type) {
    settings::ProviderConfig config;
    config.type = type;
    config.streaming = false;
    config.markedLocal = false;
    if (type == QLatin1String("mymemory")) {
        config.id = QStringLiteral("mymemory");
        config.label = QStringLiteral("MyMemory (free)");
        config.baseUrl = QStringLiteral("https://api.mymemory.translated.net");
    } else if (type == QLatin1String("libretranslate")) {
        config.id = QStringLiteral("libretranslate");
        config.label = QStringLiteral("LibreTranslate");
        config.baseUrl = QStringLiteral("http://127.0.0.1:5000");
    } else if (type == QLatin1String("deepl")) {
        config.id = QStringLiteral("deepl");
        config.label = QStringLiteral("DeepL API");
        config.baseUrl = QStringLiteral("https://api.deepl.com");
    } else if (type == QLatin1String("google-free")) {
        config.id = QStringLiteral("google-free");
        config.label = QStringLiteral("Google Translate");
        config.baseUrl = QStringLiteral("https://translate.googleapis.com");
    } else if (type == QLatin1String("bing-free")) {
        config.id = QStringLiteral("bing-free");
        config.label = QStringLiteral("Bing Translator");
        config.baseUrl = QStringLiteral("https://edge.microsoft.com");
    } else if (type == QLatin1String("youdao")) {
        config.id = QStringLiteral("youdao");
        config.label = QStringLiteral("Youdao");
        config.baseUrl = QStringLiteral("https://aidemo.youdao.com");
    } else if (type == QLatin1String("huoshanweb")) {
        config.id = QStringLiteral("huoshanweb");
        config.label = QStringLiteral("Volcengine Web");
        config.baseUrl = QStringLiteral("https://translate.volcengine.com");
    }
    return config;
}

QString simpleLanguageTag(SimpleApi api, const QString &tag) {
    if (tag.isEmpty() || tag == QLatin1String("auto"))
        return QStringLiteral("auto");
    if (api == SimpleApi::DeepL) {
        if (tag == QLatin1String("en"))
            return QStringLiteral("EN-US");
        if (tag == QLatin1String("pt"))
            return QStringLiteral("PT-PT");
        return tag.toUpper();
    }
    if (api == SimpleApi::LibreTranslate || api == SimpleApi::HuoshanWeb)
        return tag.section(QLatin1Char('-'), 0, 0);  // these services use base codes
    if (api == SimpleApi::BingFree)
        return tag;  // Bing accepts full BCP-47 tags such as zh-Hans
    if (api == SimpleApi::Youdao) {
        if (tag == QLatin1String("zh-Hans") || tag == QLatin1String("zh-CN"))
            return QStringLiteral("zh-CHS");
        if (tag == QLatin1String("zh-Hant") || tag == QLatin1String("zh-TW"))
            return QStringLiteral("zh-CHT");
        return tag.section(QLatin1Char('-'), 0, 0);
    }
    return regionCode(tag);
}

QString simpleSourceTag(SimpleApi api, const QString &tag, const QString &text) {
    if (api == SimpleApi::DeepL && !tag.isEmpty() && tag != QLatin1String("auto"))
        return tag.section(QLatin1Char('-'), 0, 0).toUpper();
    if (!tag.isEmpty() && tag != QLatin1String("auto"))
        return simpleLanguageTag(api, tag);
    if (api != SimpleApi::MyMemory && api != SimpleApi::BingFree)
        return QStringLiteral("auto");  // these services detect the language themselves
    // MyMemory and Bing require a source language. This limited guess covers English and Chinese;
    // users can select a source explicitly for other languages.
    for (const QChar character : text) {
        if (character.unicode() >= 0x4E00 && character.unicode() <= 0x9FFF)
            return api == SimpleApi::BingFree ? QStringLiteral("zh-Hans") : QStringLiteral("zh-CN");
    }
    return QStringLiteral("en");
}

SimpleTranslateProvider::SimpleTranslateProvider(settings::ProviderConfig config,
    settings::ProviderSettings timeouts, SimpleApi api, ApiKeySource apiKeySource, QObject *parent)
    : Provider(parent)
    , m_config(std::move(config))
    , m_timeouts(std::move(timeouts))
    , m_api(api)
    , m_apiKeySource(std::move(apiKeySource)) {
    m_totalTimer.setParent(this);
    if (m_config.isLoopback())
        m_network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
}

Capabilities SimpleTranslateProvider::capabilities() const {
    Capabilities caps;
    caps.streaming = false;
    caps.usesNetwork = !m_config.isLocal();
    caps.maxInputChars = m_api == SimpleApi::MyMemory ? 500 : 5000;
    return caps;
}

QUrl SimpleTranslateProvider::buildUrl(const TranslateRequest &request) const {
    const QString base = trimmedBase(m_config.baseUrl);
    QUrl url;
    QUrlQuery query;

    switch (m_api) {
    case SimpleApi::MyMemory: {
        url = QUrl(base + QStringLiteral("/get"));
        const QString source = simpleSourceTag(m_api, request.sourceLanguage, request.text);
        const QString target = simpleLanguageTag(m_api, request.targetLanguage);
        query.addQueryItem(QStringLiteral("q"), request.text);
        query.addQueryItem(QStringLiteral("langpair"), QStringLiteral("%1|%2").arg(source, target));
        break;
    }
    case SimpleApi::GoogleFree: {
        url = QUrl(base + QStringLiteral("/translate_a/single"));
        query.addQueryItem(QStringLiteral("client"), QStringLiteral("gtx"));
        query.addQueryItem(
            QStringLiteral("sl"), simpleSourceTag(m_api, request.sourceLanguage, request.text));
        query.addQueryItem(QStringLiteral("tl"), simpleLanguageTag(m_api, request.targetLanguage));
        query.addQueryItem(QStringLiteral("dt"), QStringLiteral("t"));
        query.addQueryItem(QStringLiteral("q"), request.text);
        break;
    }
    case SimpleApi::LibreTranslate:
        url = QUrl(base + QStringLiteral("/translate"));
        break;
    case SimpleApi::DeepL:
        url = QUrl(base + QStringLiteral("/v2/translate"));
        break;
    case SimpleApi::BingFree: {
        url = QUrl(base + QStringLiteral("/translate/translatetext"));
        query.addQueryItem(
            QStringLiteral("from"), simpleSourceTag(m_api, request.sourceLanguage, request.text));
        query.addQueryItem(QStringLiteral("to"), simpleLanguageTag(m_api, request.targetLanguage));
        query.addQueryItem(QStringLiteral("isEnterpriseClient"), QStringLiteral("false"));
        break;
    }
    case SimpleApi::Youdao:
        url = QUrl(base + QStringLiteral("/trans"));
        break;
    case SimpleApi::HuoshanWeb:
        url = QUrl(base + QStringLiteral("/crx/translate/v1/"));
        break;
    }
    if (!query.isEmpty())
        url.setQuery(query);
    return url;
}

QByteArray SimpleTranslateProvider::buildBody(const TranslateRequest &request) const {
    switch (m_api) {
    case SimpleApi::DeepL: {
        const QString source = simpleSourceTag(m_api, request.sourceLanguage, request.text);
        QJsonObject body {
            {QStringLiteral("text"), QJsonArray {request.text}},
            {QStringLiteral("target_lang"), simpleLanguageTag(m_api, request.targetLanguage)},
        };
        if (source != QLatin1String("auto"))
            body.insert(QStringLiteral("source_lang"), source);
        return QJsonDocument(body).toJson(QJsonDocument::Compact);
    }
    case SimpleApi::BingFree: {
        QJsonArray items;
        items.append(request.text);
        return QJsonDocument(items).toJson(QJsonDocument::Compact);
    }
    case SimpleApi::Youdao: {
        QUrlQuery form;
        form.addQueryItem(
            QStringLiteral("from"), simpleSourceTag(m_api, request.sourceLanguage, request.text));
        form.addQueryItem(QStringLiteral("to"), simpleLanguageTag(m_api, request.targetLanguage));
        form.addQueryItem(QStringLiteral("q"), request.text);
        return form.query(QUrl::FullyEncoded).toUtf8();
    }
    case SimpleApi::HuoshanWeb: {
        QJsonObject body;
        body.insert(QStringLiteral("source_language"),
            simpleSourceTag(m_api, request.sourceLanguage, request.text));
        body.insert(
            QStringLiteral("target_language"), simpleLanguageTag(m_api, request.targetLanguage));
        body.insert(QStringLiteral("text"), request.text);
        return QJsonDocument(body).toJson(QJsonDocument::Compact);
    }
    case SimpleApi::LibreTranslate: {
        QJsonObject body;
        body.insert(QStringLiteral("q"), request.text);
        body.insert(
            QStringLiteral("source"), simpleSourceTag(m_api, request.sourceLanguage, request.text));
        body.insert(QStringLiteral("target"), simpleLanguageTag(m_api, request.targetLanguage));
        body.insert(QStringLiteral("format"), QStringLiteral("text"));
        return QJsonDocument(body).toJson(QJsonDocument::Compact);
    }
    case SimpleApi::MyMemory:
    case SimpleApi::GoogleFree:
        return {};
    }
    return {};
}

void SimpleTranslateProvider::translate(const TranslateRequest &request, Sink &sink) {
    cancel(m_generation);
    m_sink = &sink;
    m_generation = request.generation;
    m_finished = false;
    m_request = request;

    // MyMemory documents a 500-byte UTF-8 limit, not a 500-character limit.
    if (m_api == SimpleApi::MyMemory && request.text.toUtf8().size() > 500) {
        fail(core::ErrorCode::SelectionTooLarge,
            QStringLiteral("MyMemory accepts at most 500 UTF-8 bytes per request"));
        return;
    }

    startTranslateRequest();
}

void SimpleTranslateProvider::startTranslateRequest() {
    QNetworkRequest networkRequest(buildUrl(m_request));
    networkRequest.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    const bool formBody = m_api == SimpleApi::Youdao;
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
        formBody ? QStringLiteral("application/x-www-form-urlencoded")
                 : QStringLiteral("application/json"));
    if (m_api == SimpleApi::BingFree) {
        // The Edge web endpoint expects a browser-like user agent.
        networkRequest.setRawHeader("User-Agent",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
            "Chrome/113.0.0.0 Safari/537.36 Edg/113.0.1774.42");
    }

    QByteArray body = buildBody(m_request);
    if (m_api == SimpleApi::DeepL) {
        const core::Result<QString> key = m_apiKeySource ? m_apiKeySource() : QString();
        if (!key.ok() || key.value().isEmpty()) {
            fail(core::ErrorCode::ProviderNotConfigured, QStringLiteral("add your DeepL API key"));
            return;
        }
        networkRequest.setRawHeader("Authorization", "DeepL-Auth-Key " + key.value().toUtf8());
    } else if (m_api == SimpleApi::LibreTranslate && m_apiKeySource) {
        const core::Result<QString> key = m_apiKeySource();
        if (key.ok() && !key.value().isEmpty()) {
            QJsonObject document = QJsonDocument::fromJson(body).object();
            document.insert(QStringLiteral("api_key"), key.value());
            body = QJsonDocument(document).toJson(QJsonDocument::Compact);
        }
    }

    if (m_api == SimpleApi::MyMemory || m_api == SimpleApi::GoogleFree)
        m_reply = m_network.get(networkRequest);
    else
        m_reply = m_network.post(networkRequest, body);

    connect(m_reply, &QNetworkReply::finished, this, &SimpleTranslateProvider::onFinished);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &SimpleTranslateProvider::onReplyError);

    m_totalTimer.start(m_timeouts.totalTimeoutMs, [this] {
        fail(core::ErrorCode::ProviderTimeout, QStringLiteral("no answer within the timeout"));
    });
}

void SimpleTranslateProvider::cancel(quint64 generation) {
    if (m_reply == nullptr || generation != m_generation)
        return;
    m_totalTimer.cancel();
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    if (reply->isRunning())
        reply->abort();
    reply->deleteLater();
    if (m_sink != nullptr && !m_finished) {
        Sink *sink = m_sink;
        m_sink = nullptr;
        m_finished = true;
        sink->onFailed(generation,
            core::Error {core::ErrorCode::ProviderCancelled, QStringLiteral("cancelled")});
    }
}

void SimpleTranslateProvider::fail(core::ErrorCode code, const QString &detail, bool abortReply) {
    if (m_finished || m_sink == nullptr)
        return;
    m_finished = true;
    m_totalTimer.cancel();
    Sink *sink = m_sink;
    m_sink = nullptr;
    if (m_reply != nullptr) {
        m_reply->disconnect(this);
        // A reply that already reported an error must not be aborted again: Qt warns and drops the
        // message.
        if (abortReply && m_reply->isRunning())
            m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    sink->onFailed(m_generation, core::Error {code, detail});
}

void SimpleTranslateProvider::onReplyError(QNetworkReply::NetworkError code) {
    // Qt reports 4xx/5xx through both errorOccurred and finished; the HTTP status is authoritative,
    // so a status-bearing reply is left to onFinished().
    if (m_reply == nullptr)
        return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400)
        return;
    fail(core::ErrorCode::ProviderNetwork, m_reply->errorString(), false);
    Q_UNUSED(code)
}

void SimpleTranslateProvider::onFinished() {
    if (m_reply == nullptr || m_finished)
        return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = m_reply->readAll();

    const QString step = QStringLiteral("translate request");
    const QString host = m_reply != nullptr ? m_reply->url().host() : QString();

    if (status == 401 || status == 403) {
        fail(core::ErrorCode::ProviderAuthFailed,
            QStringLiteral("%1 to %2: HTTP %3").arg(step, host).arg(status));
        return;
    }
    if (status == 429) {
        fail(core::ErrorCode::ProviderRateLimited,
            QStringLiteral("HTTP 429: this service is rate-limiting the request or its quota is "
                           "exhausted; try again later or choose another engine"));
        return;
    }
    if (m_api == SimpleApi::DeepL && status == 456) {
        fail(core::ErrorCode::ProviderRateLimited, QStringLiteral("DeepL quota exhausted"));
        return;
    }
    if (status >= 500) {
        fail(core::ErrorCode::ProviderUnavailable,
            QStringLiteral("%1 to %2: HTTP %3").arg(step, host).arg(status));
        return;
    }
    if (status >= 400) {
        fail(core::ErrorCode::ProviderBadResponse,
            QStringLiteral("%1 to %2: HTTP %3 (is that endpoint reachable through your proxy?)")
                .arg(step, host)
                .arg(status));
        return;
    }
    if (status < 200 || status >= 300) {
        fail(core::ErrorCode::ProviderBadResponse,
            QStringLiteral("%1 to %2 returned HTTP %3; check the configured endpoint")
                .arg(step, host)
                .arg(status));
        return;
    }

    const core::Result<QString> translated = parsePayload(payload);
    if (!translated.ok()) {
        fail(translated.error().code, translated.error().detail);
        return;
    }

    Sink *sink = m_sink;
    m_sink = nullptr;
    m_finished = true;
    m_totalTimer.cancel();
    m_reply->deleteLater();
    m_reply = nullptr;
    sink->onDelta(m_generation, QStringView(translated.value()));
    sink->onFinished(m_generation);
}

core::Result<QString> SimpleTranslateProvider::parsePayload(const QByteArray &payload) const {
    QJsonParseError parseError {};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        // A service may echo submitted text or credentials, so never include its body in errors.
        if (payload.trimmed().startsWith('<')) {
            return core::Error {core::ErrorCode::ProviderBadResponse,
                QStringLiteral(
                    "the endpoint served a web page instead of a translation API; check its URL")};
        }
        return core::Error {
            core::ErrorCode::ProviderBadResponse, QStringLiteral("the response is not valid JSON")};
    }

    QString text;
    switch (m_api) {
    case SimpleApi::MyMemory: {
        const QJsonObject root = document.object();
        if (root.value(QStringLiteral("responseStatus")).toInt(200) == 429)
            return core::Error {core::ErrorCode::ProviderRateLimited,
                QStringLiteral("MyMemory's free quota or rate limit has been reached")};
        const QJsonObject data = root.value(QStringLiteral("responseData")).toObject();
        text = data.value(QStringLiteral("translatedText")).toString();
        // MyMemory reports quota problems inside a 200 response.
        if (text.isEmpty()) {
            const QString details = root.value(QStringLiteral("responseDetails")).toString();
            if (details.contains(QLatin1String("QUOTA"), Qt::CaseInsensitive)
                || details.contains(QLatin1String("MYMEMORY WARNING"), Qt::CaseInsensitive)
                || details.contains(QLatin1String("FREE TRANSLATIONS"), Qt::CaseInsensitive))
                return core::Error {core::ErrorCode::ProviderRateLimited,
                    QStringLiteral("MyMemory's free quota or rate limit has been reached")};
        }
        break;
    }
    case SimpleApi::LibreTranslate: {
        const QJsonObject root = document.object();
        text = root.value(QStringLiteral("translatedText")).toString();
        if (text.isEmpty()) {
            const QString error = root.value(QStringLiteral("error")).toString();
            if (!error.isEmpty())
                return core::Error {core::ErrorCode::ProviderBadResponse,
                    QStringLiteral("LibreTranslate rejected the request; check its settings")};
        }
        break;
    }
    case SimpleApi::DeepL:
        text = document.object()
                   .value(QStringLiteral("translations"))
                   .toArray()
                   .at(0)
                   .toObject()
                   .value(QStringLiteral("text"))
                   .toString();
        break;
    case SimpleApi::BingFree: {
        const QJsonArray items = document.array();
        const QJsonArray translations
            = items.at(0).toObject().value(QStringLiteral("translations")).toArray();
        text = translations.at(0).toObject().value(QStringLiteral("text")).toString();
        if (text.isEmpty()) {
            const QString message = items.at(0)
                                        .toObject()
                                        .value(QStringLiteral("error"))
                                        .toObject()
                                        .value(QStringLiteral("message"))
                                        .toString();
            if (!message.isEmpty())
                return core::Error {core::ErrorCode::ProviderBadResponse,
                    QStringLiteral("Bing rejected the translation request")};
        }
        break;
    }
    case SimpleApi::Youdao: {
        const QJsonObject root = document.object();
        const QString errorCode = root.value(QStringLiteral("errorCode")).toString();
        if (!errorCode.isEmpty() && errorCode != QLatin1String("0"))
            return core::Error {core::ErrorCode::ProviderBadResponse,
                QStringLiteral("Youdao rejected the translation request")};
        const QJsonArray translation = root.value(QStringLiteral("translation")).toArray();
        QStringList lines;
        for (const QJsonValue &line : translation) {
            const QString piece = line.toString();
            if (!piece.isEmpty())
                lines.append(piece);
        }
        text = lines.join(QLatin1Char('\n'));
        break;
    }
    case SimpleApi::HuoshanWeb: {
        const QJsonObject root = document.object();
        text = root.value(QStringLiteral("translation")).toString();
        const int statusCode = root.value(QStringLiteral("base_resp"))
                                   .toObject()
                                   .value(QStringLiteral("status_code"))
                                   .toInt();
        if (statusCode != 0)
            return core::Error {core::ErrorCode::ProviderBadResponse,
                QStringLiteral("Volcengine rejected the translation request")};
        break;
    }
    case SimpleApi::GoogleFree: {
        // [[["翻译结果", "原文", ...], ...], ...]
        const QJsonArray sentences = document.array().at(0).toArray();
        for (const QJsonValue &sentence : sentences)
            text += sentence.toArray().at(0).toString();
        break;
    }
    }

    text = text.trimmed();
    if (text.isEmpty())
        return core::Error {core::ErrorCode::ProviderBadResponse,
            QStringLiteral("the engine returned no translation")};
    return text;
}

}  // namespace provider
