#include "settings/settings.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace settings {
namespace {

constexpr int kSchemaVersion = 1;
constexpr int kMaxStabilityDelayMs = 2000;
constexpr int kMinStabilityDelayMs = 100;
constexpr int kMaxSelectionChars = 20000;
constexpr int kMinSelectionChars = 200;
constexpr int kMaxResolveTimeoutMs = 5000;
constexpr int kMinResolveTimeoutMs = 200;

int clamped(int value, int minimum, int maximum, const char *field, QStringList *issues) {
    const int result = std::clamp(value, minimum, maximum);
    if (result != value && issues)
        issues->append(QString::fromLatin1(field));
    return result;
}

int defaultPort(const QString &scheme) {
    return scheme == QLatin1String("https") ? 443 : 80;
}

bool isHexSha256(const QString &value) {
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(value).hasMatch();
}

bool isValidLanguageTag(const QString &tag) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z]{2,8}(-[A-Za-z0-9]{1,8})*$"));
    return pattern.match(tag).hasMatch();
}

bool isValidProviderId(const QString &id) {
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9-]{1,32}$"));
    return pattern.match(id).hasMatch();
}

QJsonObject providerToJson(const ProviderConfig &provider) {
    QJsonObject object;
    object[QStringLiteral("id")] = provider.id;
    object[QStringLiteral("type")] = provider.type;
    object[QStringLiteral("label")] = provider.label;
    object[QStringLiteral("baseUrl")] = provider.baseUrl;
    object[QStringLiteral("model")] = provider.model;
    object[QStringLiteral("apiKeyRef")] = provider.apiKeyRef;
    object[QStringLiteral("streaming")] = provider.streaming;
    object[QStringLiteral("markedLocal")] = provider.markedLocal;
    return object;
}

ProviderConfig providerFromJson(const QJsonObject &object) {
    ProviderConfig provider;
    provider.id = object.value(QStringLiteral("id")).toString();
    provider.type = object.value(QStringLiteral("type")).toString(provider.type);
    provider.label = object.value(QStringLiteral("label")).toString();
    provider.baseUrl = object.value(QStringLiteral("baseUrl")).toString();
    provider.model = object.value(QStringLiteral("model")).toString();
    provider.apiKeyRef = object.value(QStringLiteral("apiKeyRef")).toString();
    provider.streaming = object.value(QStringLiteral("streaming")).toBool(true);
    provider.markedLocal = object.value(QStringLiteral("markedLocal")).toBool(false);
    return provider;
}

// Recursively overlays `known` onto `original`, so keys added by a newer schema survive a save.
QJsonObject overlay(QJsonObject original, const QJsonObject &known) {
    for (auto it = known.constBegin(); it != known.constEnd(); ++it) {
        const QJsonValue value = it.value();
        if (value.isObject() && original.value(it.key()).isObject())
            original[it.key()] = overlay(original.value(it.key()).toObject(), value.toObject());
        else
            original[it.key()] = value;
    }
    return original;
}

}  // namespace

// ---------------------------------------------------------------- ProviderConfig

QString ProviderConfig::host() const {
    return QUrl(baseUrl).host().toLower();
}

bool ProviderConfig::isLoopback() const {
    const QString name = host();
    if (name.isEmpty())
        return markedLocal;
    return name == QLatin1String("localhost") || name == QLatin1String("::1")
        || name.startsWith(QLatin1String("127."));
}

QString ProviderConfig::destinationLabel() const {
    const QString name = host();
    if (name.isEmpty())
        return label.isEmpty() ? QStringLiteral("<unset>") : label;
    if (model.isEmpty())
        return name;
    return QStringLiteral("%1 · %2").arg(name, model);
}

QString ProviderConfig::fingerprint() const {
    const QUrl url(baseUrl);
    const QString scheme = url.scheme().toLower();
    const int port = url.port(defaultPort(scheme));
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')) && path.size() > 1)
        path.chop(1);
    if (path == QLatin1String("/"))
        path.clear();

    const QString normalized
        = QStringLiteral("%1|%2|%3|%4|%5|%6").arg(type, scheme, host()).arg(port).arg(path, model);
    return QString::fromLatin1(
        QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha256).toHex());
}

// ---------------------------------------------------------------- Settings

const ProviderConfig *Settings::activeProvider() const {
    if (provider.activeId.isEmpty())
        return nullptr;
    for (const ProviderConfig &candidate : provider.providers) {
        if (candidate.id == provider.activeId)
            return &candidate;
    }
    return nullptr;
}

bool Settings::mayAutoSend() const {
    if (!selection.automaticEnabled || selection.paused)
        return false;
    const ProviderConfig *active = activeProvider();
    if (!active)
        return false;
    if (active->isLocal())
        return true;
    return consent.valid() && consent.fingerprint == active->fingerprint();
}

// ---------------------------------------------------------------- serialization

QJsonObject toJson(const Settings &settings) {
    QJsonObject selection;
    selection[QStringLiteral("automaticEnabled")] = settings.selection.automaticEnabled;
    selection[QStringLiteral("paused")] = settings.selection.paused;
    selection[QStringLiteral("showResultImmediately")] = settings.selection.showResultImmediately;
    selection[QStringLiteral("xwaylandPopup")] = settings.selection.xwaylandPopup;
    selection[QStringLiteral("backend")] = settings.selection.backend;
    selection[QStringLiteral("stabilityDelayMs")] = settings.selection.stabilityDelayMs;
    selection[QStringLiteral("maxSelectionChars")] = settings.selection.maxSelectionChars;
    selection[QStringLiteral("resolveTimeoutMs")] = settings.selection.resolveTimeoutMs;

    QJsonArray providers;
    for (const ProviderConfig &provider : settings.provider.providers)
        providers.append(providerToJson(provider));

    QJsonObject provider;
    provider[QStringLiteral("activeId")] = settings.provider.activeId;
    provider[QStringLiteral("firstByteTimeoutMs")] = settings.provider.firstByteTimeoutMs;
    provider[QStringLiteral("totalTimeoutMs")] = settings.provider.totalTimeoutMs;
    provider[QStringLiteral("providers")] = providers;

    QJsonObject language;
    language[QStringLiteral("source")] = settings.language.source;
    language[QStringLiteral("target")] = settings.language.target;

    QJsonObject consent;
    consent[QStringLiteral("fingerprint")] = settings.consent.fingerprint;
    consent[QStringLiteral("destinationLabel")] = settings.consent.destinationLabel;
    consent[QStringLiteral("grantedAt")] = settings.consent.grantedAt;

    QJsonObject network;
    network[QStringLiteral("proxy")] = settings.network.proxy;
    network[QStringLiteral("proxyUrl")] = settings.network.proxyUrl;

    QJsonObject ui;
    ui[QStringLiteral("theme")] = settings.ui.theme;
    ui[QStringLiteral("language")] = settings.ui.language;
    ui[QStringLiteral("scale")] = settings.ui.scale;
    ui[QStringLiteral("fontScale")] = settings.ui.fontScale;
    ui[QStringLiteral("reduceMotion")] = settings.ui.reduceMotion;

    QJsonObject root;
    root[QStringLiteral("version")] = kSchemaVersion;
    root[QStringLiteral("selection")] = selection;
    root[QStringLiteral("provider")] = provider;
    root[QStringLiteral("language")] = language;
    root[QStringLiteral("consent")] = consent;
    root[QStringLiteral("network")] = network;
    root[QStringLiteral("ui")] = ui;
    return root;
}

QJsonObject mergePreservingUnknown(const QJsonObject &original, const QJsonObject &known) {
    return overlay(original, known);
}

// ---------------------------------------------------------------- validation

bool validate(Settings &settings, QStringList *issues) {
    bool usable = true;

    const QStringList backends {QStringLiteral("auto"), QStringLiteral("atspi"),
        QStringLiteral("x11-primary"), QStringLiteral("wlr-data-control")};
    if (!backends.contains(settings.selection.backend)) {
        if (issues)
            issues->append(QStringLiteral("selection.backend"));
        settings.selection.backend = QStringLiteral("auto");
        usable = false;
    }

    settings.selection.stabilityDelayMs = clamped(settings.selection.stabilityDelayMs,
        kMinStabilityDelayMs, kMaxStabilityDelayMs, "selection.stabilityDelayMs", issues);
    settings.selection.maxSelectionChars = clamped(settings.selection.maxSelectionChars,
        kMinSelectionChars, kMaxSelectionChars, "selection.maxSelectionChars", issues);
    settings.selection.resolveTimeoutMs = clamped(settings.selection.resolveTimeoutMs,
        kMinResolveTimeoutMs, kMaxResolveTimeoutMs, "selection.resolveTimeoutMs", issues);

    if (!settings.language.target.isEmpty() && !isValidLanguageTag(settings.language.target)) {
        if (issues)
            issues->append(QStringLiteral("language.target"));
        settings.language.target.clear();
        usable = false;
    }
    if (!settings.language.source.isEmpty() && settings.language.source != QLatin1String("auto")
        && !isValidLanguageTag(settings.language.source)) {
        if (issues)
            issues->append(QStringLiteral("language.source"));
        settings.language.source = QStringLiteral("auto");
    }

    settings.provider.firstByteTimeoutMs = clamped(
        settings.provider.firstByteTimeoutMs, 1000, 120000, "provider.firstByteTimeoutMs", issues);
    settings.provider.totalTimeoutMs = clamped(
        settings.provider.totalTimeoutMs, 1000, 300000, "provider.totalTimeoutMs", issues);
    if (settings.provider.totalTimeoutMs < settings.provider.firstByteTimeoutMs) {
        if (issues)
            issues->append(QStringLiteral("provider.totalTimeoutMs"));
        settings.provider.totalTimeoutMs = settings.provider.firstByteTimeoutMs;
    }

    QStringList keptIds;
    QList<ProviderConfig> kept;
    for (const ProviderConfig &input : settings.provider.providers) {
        ProviderConfig provider = input;
        if (!isValidProviderId(provider.id) || keptIds.contains(provider.id)) {
            if (issues)
                issues->append(QStringLiteral("provider.id"));
            continue;
        }
        const QUrl url(provider.baseUrl);
        const bool schemeOk
            = url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https");
        const bool cleanUrl = url.isValid() && !url.host().isEmpty() && url.query().isEmpty()
            && url.fragment().isEmpty();
        if (!schemeOk || !cleanUrl) {
            if (issues)
                issues->append(QStringLiteral("provider.baseUrl"));
            continue;
        }
        if (url.scheme() == QLatin1String("http") && !provider.isLoopback()) {
            // A plain-http remote endpoint would put the API key on the wire in clear text.
            if (issues)
                issues->append(QStringLiteral("provider.baseUrl(http-remote)"));
            continue;
        }
        keptIds.append(provider.id);
        kept.append(provider);
    }
    settings.provider.providers = kept;
    if (!settings.provider.activeId.isEmpty() && !keptIds.contains(settings.provider.activeId))
        settings.provider.activeId.clear();

    const QStringList uiLanguages {
        QStringLiteral("system"), QStringLiteral("en"), QStringLiteral("zh-Hans")};
    if (!uiLanguages.contains(settings.ui.language)) {
        if (issues)
            issues->append(QStringLiteral("ui.language"));
        settings.ui.language = QStringLiteral("system");
    }

    if (settings.ui.scale < 0.75 || settings.ui.scale > 1.5) {
        settings.ui.scale = std::clamp(settings.ui.scale, 0.75, 1.5);
        if (issues)
            issues->append(QStringLiteral("ui.scale"));
    }
    if (settings.ui.fontScale < 0.75 || settings.ui.fontScale > 1.75) {
        settings.ui.fontScale = std::clamp(settings.ui.fontScale, 0.75, 1.75);
        if (issues)
            issues->append(QStringLiteral("ui.fontScale"));
    }

    const QStringList proxyModes {
        QStringLiteral("system"), QStringLiteral("manual"), QStringLiteral("none")};
    if (!proxyModes.contains(settings.network.proxy)) {
        if (issues)
            issues->append(QStringLiteral("network.proxy"));
        settings.network.proxy = QStringLiteral("system");
        settings.network.proxyUrl.clear();
    }
    if (settings.network.proxy == QLatin1String("manual")) {
        const QUrl url(settings.network.proxyUrl);
        const bool usableProxy = url.isValid() && !url.host().isEmpty()
            && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")
                || url.scheme() == QLatin1String("socks5"));
        if (!usableProxy) {
            if (issues)
                issues->append(QStringLiteral("network.proxyUrl"));
            settings.network.proxy = QStringLiteral("system");
            settings.network.proxyUrl.clear();
        }
    } else {
        settings.network.proxyUrl.clear();
    }

    if (!settings.consent.fingerprint.isEmpty() && !isHexSha256(settings.consent.fingerprint)) {
        if (issues)
            issues->append(QStringLiteral("consent.fingerprint"));
        settings.consent = ConsentRecord {};
    }

    return usable;
}

// ---------------------------------------------------------------- loading

LoadResult loadFromDocument(const QJsonObject &document, bool documentWasReadable) {
    LoadResult result;
    result.baseDocument = document;

    if (!documentWasReadable || document.isEmpty()) {
        result.outcome = Outcome::LoadedWithDefaults;
        result.settings = Settings {};
        result.settings.language.target = defaultTargetLanguage();
        validate(result.settings, &result.issues);
        return result;
    }

    const int version = document.value(QStringLiteral("version")).toInt(kSchemaVersion);
    if (version != kSchemaVersion) {
        // Forward-only schema: an unknown version is treated as corrupt rather than partially
        // honoured.
        result.outcome = Outcome::Recovered;
        result.issues.append(QStringLiteral("version"));
        result.settings = Settings {};
        result.settings.language.target = defaultTargetLanguage();
        return result;
    }

    Settings settings;
    const QJsonObject selection = document.value(QStringLiteral("selection")).toObject();
    settings.selection.automaticEnabled = selection.value(QStringLiteral("automaticEnabled"))
                                              .toBool(settings.selection.automaticEnabled);
    settings.selection.paused
        = selection.value(QStringLiteral("paused")).toBool(settings.selection.paused);
    settings.selection.showResultImmediately
        = selection.value(QStringLiteral("showResultImmediately"))
              .toBool(settings.selection.showResultImmediately);
    settings.selection.xwaylandPopup
        = selection.value(QStringLiteral("xwaylandPopup")).toBool(settings.selection.xwaylandPopup);
    settings.selection.backend
        = selection.value(QStringLiteral("backend")).toString(settings.selection.backend);
    settings.selection.stabilityDelayMs = selection.value(QStringLiteral("stabilityDelayMs"))
                                              .toInt(settings.selection.stabilityDelayMs);
    settings.selection.maxSelectionChars = selection.value(QStringLiteral("maxSelectionChars"))
                                               .toInt(settings.selection.maxSelectionChars);
    settings.selection.resolveTimeoutMs = selection.value(QStringLiteral("resolveTimeoutMs"))
                                              .toInt(settings.selection.resolveTimeoutMs);

    const QJsonObject language = document.value(QStringLiteral("language")).toObject();
    settings.language.source
        = language.value(QStringLiteral("source")).toString(settings.language.source);
    settings.language.target
        = language.value(QStringLiteral("target")).toString(settings.language.target);
    if (settings.language.target.isEmpty())
        settings.language.target = defaultTargetLanguage();

    const QJsonObject provider = document.value(QStringLiteral("provider")).toObject();
    settings.provider.activeId = provider.value(QStringLiteral("activeId")).toString();
    settings.provider.firstByteTimeoutMs = provider.value(QStringLiteral("firstByteTimeoutMs"))
                                               .toInt(settings.provider.firstByteTimeoutMs);
    settings.provider.totalTimeoutMs
        = provider.value(QStringLiteral("totalTimeoutMs")).toInt(settings.provider.totalTimeoutMs);
    const QJsonArray providers = provider.value(QStringLiteral("providers")).toArray();
    for (const QJsonValue &value : providers) {
        if (value.isObject())
            settings.provider.providers.append(providerFromJson(value.toObject()));
    }

    const QJsonObject consent = document.value(QStringLiteral("consent")).toObject();
    settings.consent.fingerprint = consent.value(QStringLiteral("fingerprint")).toString();
    settings.consent.destinationLabel
        = consent.value(QStringLiteral("destinationLabel")).toString();
    settings.consent.grantedAt = consent.value(QStringLiteral("grantedAt")).toString();

    const QJsonObject network = document.value(QStringLiteral("network")).toObject();
    settings.network.proxy
        = network.value(QStringLiteral("proxy")).toString(settings.network.proxy);
    settings.network.proxyUrl = network.value(QStringLiteral("proxyUrl")).toString();

    const QJsonObject ui = document.value(QStringLiteral("ui")).toObject();
    settings.ui.theme = ui.value(QStringLiteral("theme")).toString(settings.ui.theme);
    settings.ui.language = ui.value(QStringLiteral("language")).toString(settings.ui.language);
    settings.ui.scale = ui.value(QStringLiteral("scale")).toDouble(settings.ui.scale);
    settings.ui.fontScale = ui.value(QStringLiteral("fontScale")).toDouble(settings.ui.fontScale);
    settings.ui.reduceMotion
        = ui.value(QStringLiteral("reduceMotion")).toBool(settings.ui.reduceMotion);

    result.outcome = Outcome::Loaded;
    validate(settings, &result.issues);
    result.settings = settings;
    return result;
}

QString defaultTargetLanguage() {
    const QString locale = QLocale::system().name();  // e.g. "zh_CN"
    const QString language = locale.section(QLatin1Char('_'), 0, 0).toLower();
    if (language == QLatin1String("zh"))
        return QStringLiteral("zh-Hans");
    if (language == QLatin1String("en") || language == QLatin1String("ja")
        || language == QLatin1String("ko") || language == QLatin1String("de")
        || language == QLatin1String("fr") || language == QLatin1String("es")
        || language == QLatin1String("ru") || language == QLatin1String("pt")
        || language == QLatin1String("it")) {
        return language;
    }
    return {};
}

}  // namespace settings
