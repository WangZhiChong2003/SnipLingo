#include "app/engine_cli.h"

#include "app/network.h"
#include "app/probe.h"
#include "app/provider_factory.h"
#include "core/log.h"
#include "core/paths.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>
#include <memory>

#include "provider/simple_translate_provider.h"
#include "secrets/secret_store.h"
#include "settings/settings_store.h"

namespace app {
namespace {

constexpr int kTestTimeoutMs = 20000;
constexpr const char *kProbeWord = "hello";

struct Startup {
    core::XdgPaths paths;
    std::unique_ptr<settings::SettingsStore> settings;
    std::unique_ptr<secrets::SecretStore> secrets;

    bool prepare() {
        paths = core::XdgPaths::resolve();
        if (const core::Result<void> ensured = paths.ensureDirectories(); !ensured.ok()) {
            std::fprintf(stderr, "cannot create application directories: %s\n",
                qPrintable(ensured.error().detail));
            return false;
        }
        settings = std::make_unique<settings::SettingsStore>(paths);
        const settings::LoadResult loaded = settings->load();
        applyNetworkSettings(loaded.settings);
        secrets = secrets::createSecretStore(probeSecretService());
        for (const QString &issue : loaded.issues) {
            std::fprintf(stderr, "note: settings issue in %s\n", qPrintable(issue));
        }
        return true;
    }
};

// Collects one translation attempt and converts it into a printable verdict.
struct ProbeSink : provider::Sink {
    QString text;
    bool finished = false;
    bool failed = false;
    core::Error error;

    void onDelta(quint64, QStringView delta) override { text += delta.toString(); }

    void onFinished(quint64) override { finished = true; }

    void onFailed(quint64, core::Error failure) override {
        failed = true;
        error = failure;
    }
};

QString describe(const settings::ProviderConfig &config) {
    if (config.isLocal())
        return QStringLiteral("local");
    QString kind = config.host();
    if (config.type == QLatin1String("deepl") || !config.apiKeyRef.isEmpty())
        kind += QStringLiteral(", uses an API key");
    else if (provider::SimpleApi api {}; provider::simpleApiForType(config.type, &api))
        kind += QStringLiteral(", keyless");
    return kind;
}

bool runProbe(provider::Provider &engine, const QString &text, const QString &source,
    const QString &target, QString *translated, core::Error *error) {
    provider::TranslateRequest request;
    request.generation = 1;
    request.text = text;
    request.sourceLanguage = source;
    request.targetLanguage = target;
    request.streaming = engine.capabilities().streaming;

    ProbeSink sink;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);

    // The provider reports through the sink; wrap it so the loop stops on either outcome.
    struct StopOnFinal : provider::Sink {
        ProbeSink *inner;
        QEventLoop *loop;

        void onDelta(quint64 generation, QStringView delta) override {
            inner->onDelta(generation, delta);
        }

        void onFinished(quint64 generation) override {
            inner->onFinished(generation);
            loop->quit();
        }

        void onFailed(quint64 generation, core::Error failure) override {
            inner->onFailed(generation, failure);
            loop->quit();
        }
    } stop;

    stop.inner = &sink;
    stop.loop = &loop;

    deadline.start(kTestTimeoutMs);
    engine.translate(request, stop);
    if (!sink.finished && !sink.failed)
        loop.exec();

    if (sink.finished && !sink.text.isEmpty()) {
        if (translated != nullptr)
            *translated = sink.text;
        return true;
    }
    if (error != nullptr) {
        *error = sink.failed ? sink.error
                             : core::Error {core::ErrorCode::ProviderTimeout,
                                   QStringLiteral("no answer within %1 ms").arg(kTestTimeoutMs)};
    }
    if (!sink.finished && !sink.failed)
        engine.cancel(request.generation);
    return false;
}

}  // namespace

int engineList(const CommandLine &commandLine) {
    core::log::init(core::log::Level::Info);
    Startup startup;
    if (!startup.prepare())
        return 1;
    const settings::Settings &current = startup.settings->current();

    if (commandLine.json) {
        QJsonArray engines;
        for (const settings::ProviderConfig &config : current.provider.providers) {
            QJsonObject entry;
            entry.insert(QStringLiteral("id"), config.id);
            entry.insert(QStringLiteral("type"), config.type);
            entry.insert(QStringLiteral("label"), config.label);
            entry.insert(QStringLiteral("baseUrl"), config.baseUrl);
            entry.insert(QStringLiteral("active"), config.id == current.provider.activeId);
            entry.insert(QStringLiteral("local"), config.isLocal());
            engines.append(entry);
        }
        QJsonObject root;
        root.insert(QStringLiteral("activeId"), current.provider.activeId);
        root.insert(QStringLiteral("targetLanguage"), current.language.target);
        root.insert(QStringLiteral("engines"), engines);
        const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
        std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
        return 0;
    }

    if (current.provider.providers.isEmpty()) {
        std::printf("no engines configured\n\n");
        std::printf("add one without any key, for example:\n");
        std::printf("  sniplyngo engine use google-free\n");
        std::printf("  sniplyngo engine use bing-free\n");
        std::printf("  sniplyngo engine use youdao\n");
        std::printf("  sniplyngo engine use huoshanweb\n");
        std::printf("  sniplyngo engine use mymemory\n");
        std::printf("  sniplyngo engine use libretranslate\n");
        return 1;
    }
    for (const settings::ProviderConfig &config : current.provider.providers) {
        std::printf("%s %-16s %-28s %s\n", config.id == current.provider.activeId ? "*" : " ",
            qPrintable(config.id), qPrintable(describe(config)), qPrintable(config.label));
    }
    std::printf("\ntarget language: %s\n",
        current.language.target.isEmpty() ? "(none — set one: sniplyngo engine target zh-Hans)"
                                          : qPrintable(current.language.target));
    return 0;
}

int engineUse(const QString &id) {
    core::log::init(core::log::Level::Info);
    Startup startup;
    if (!startup.prepare())
        return 1;

    settings::Settings next = startup.settings->current();
    QString selectedId = id;
    bool found = false;
    for (const settings::ProviderConfig &config : next.provider.providers) {
        if (config.id == id)
            found = true;
    }

    if (!found) {
        provider::SimpleApi api {};
        if (!provider::simpleApiForType(id, &api)) {
            std::fprintf(stderr, "unknown engine: %s\n", qPrintable(id));
            std::fprintf(stderr,
                "known presets: google-free, bing-free, youdao, huoshanweb, mymemory, "
                "libretranslate, deepl\n");
            return 1;
        }
        settings::ProviderConfig preset = provider::simplePreset(id);
        QString unique = preset.id;
        int suffix = 2;
        for (const settings::ProviderConfig &config : next.provider.providers) {
            if (config.id == unique) {
                unique = QStringLiteral("%1-%2").arg(preset.id).arg(suffix++);
                break;
            }
        }
        preset.id = unique;
        selectedId = unique;
        next.provider.providers.append(preset);
    }

    next.provider.activeId = selectedId;
    if (const core::Result<void> saved = startup.settings->save(next); !saved.ok()) {
        std::fprintf(stderr, "%s\n", qPrintable(saved.error().detail));
        return 1;
    }
    std::printf("active engine: %s\n", qPrintable(selectedId));
    if (next.language.target.isEmpty())
        std::printf("note: no target language yet; run: sniplyngo engine target zh-Hans\n");
    return 0;
}

int engineAdd(const QString &id, const QString &baseUrl, const QString &model) {
    core::log::init(core::log::Level::Info);
    Startup startup;
    if (!startup.prepare())
        return 1;

    settings::Settings next = startup.settings->current();
    settings::ProviderConfig config;
    if (provider::SimpleApi api {}; provider::simpleApiForType(id, &api)) {
        // Adding a preset by name with a custom URL is how a self-hosted instance is configured.
        config = provider::simplePreset(id);
        config.baseUrl = baseUrl;
    } else {
        config.id = id;
        config.label = id;
        config.baseUrl = baseUrl;
        config.model = model;
        config.apiKeyRef = QStringLiteral("provider/%1/apiKey").arg(id);
    }
    bool updated = false;
    for (settings::ProviderConfig &existing : next.provider.providers) {
        if (existing.id != id)
            continue;
        if (existing.type != config.type || config.type == QLatin1String("openai-compatible")) {
            std::fprintf(stderr, "an engine with id %s already exists\n", qPrintable(id));
            return 1;
        }
        existing.baseUrl = baseUrl;
        updated = true;
        break;
    }
    if (!updated)
        next.provider.providers.append(config);
    next.provider.activeId = id;

    QStringList issues;
    settings::Settings checked = next;
    settings::validate(checked, &issues);
    if (issues.contains(QStringLiteral("provider.id"))
        || issues.contains(QStringLiteral("provider.baseUrl"))
        || issues.contains(QStringLiteral("provider.baseUrl(http-remote)"))) {
        std::fprintf(stderr, "invalid engine id or URL (use HTTPS, or HTTP for a local address)\n");
        return 1;
    }
    if (const core::Result<void> saved = startup.settings->save(next); !saved.ok()) {
        std::fprintf(stderr, "%s\n", qPrintable(saved.error().detail));
        return 1;
    }
    bool kept = false;
    for (const settings::ProviderConfig &stored : startup.settings->current().provider.providers)
        kept = kept || stored.id == id;
    if (!kept) {
        std::fprintf(stderr,
            "the engine was rejected: check the id (a-z, 0-9, dash) and the base URL "
            "(https, or http only for a loopback host)\n");
        return 1;
    }
    std::printf("active engine: %s (%s, model %s)\n", qPrintable(id), qPrintable(baseUrl),
        qPrintable(model));
    return 0;
}

int engineSetTarget(const QString &code) {
    core::log::init(core::log::Level::Info);
    Startup startup;
    if (!startup.prepare())
        return 1;

    settings::Settings next = startup.settings->current();
    next.language.target = code;
    QStringList issues;
    settings::validate(next, &issues);
    if (next.language.target.isEmpty()) {
        std::fprintf(stderr, "unsupported language code: %s\n", qPrintable(code));
        std::fprintf(stderr, "examples: en, zh-Hans, zh-Hant, ja, de, fr\n");
        return 1;
    }
    if (const core::Result<void> saved = startup.settings->save(next); !saved.ok()) {
        std::fprintf(stderr, "%s\n", qPrintable(saved.error().detail));
        return 1;
    }
    std::printf("target language: %s\n", qPrintable(next.language.target));
    return 0;
}

int proxyCommand(const QString &mode, const QString &url) {
    core::log::init(core::log::Level::Info);
    Startup startup;
    if (!startup.prepare())
        return 1;

    settings::Settings next = startup.settings->current();
    if (mode.isEmpty()) {
        std::printf("mode: %s\n%s\n", qPrintable(next.network.proxy), qPrintable(describeProxy()));
        return 0;
    }

    next.network.proxy = mode;
    next.network.proxyUrl = mode == QLatin1String("manual") ? url : QString();
    if (const core::Result<void> saved = startup.settings->save(next); !saved.ok()) {
        std::fprintf(stderr, "%s\n", qPrintable(saved.error().detail));
        return 1;
    }
    if (startup.settings->current().network.proxy != mode) {
        std::fprintf(
            stderr, "the proxy setting was rejected: use http://, https:// or socks5://\n");
        return 1;
    }
    applyNetworkSettings(startup.settings->current());
    std::printf("mode: %s\n%s\n", qPrintable(mode), qPrintable(describeProxy()));
    return 0;
}

int engineTest(const QString &id, bool all) {
    core::log::init(core::log::Level::Info);
    Startup startup;
    if (!startup.prepare())
        return 1;
    settings::Settings current = startup.settings->current();
    const QString target
        = current.language.target.isEmpty() ? QStringLiteral("zh-Hans") : current.language.target;
    const bool englishTarget = target == QLatin1String("en");
    const QString probeText
        = englishTarget ? QStringLiteral("你好") : QString::fromLatin1(kProbeWord);
    const QString probeSource = englishTarget ? QStringLiteral("zh-Hans") : QStringLiteral("en");

    std::printf("proxy: %s\n",
        qPrintable(describeProxy()));  // a silent direct connection is the usual cause of
                                       // "everything fails"

    QList<settings::ProviderConfig> candidates;
    if (all) {
        for (const settings::ProviderConfig &config : current.provider.providers)
            candidates.append(config);
        // Test configured endpoints first; use the preset only when that type is not configured.
        for (const QString &type : {QStringLiteral("google-free"), QStringLiteral("bing-free"),
                 QStringLiteral("youdao"), QStringLiteral("huoshanweb"), QStringLiteral("mymemory"),
                 QStringLiteral("libretranslate")}) {
            bool configured = false;
            for (const settings::ProviderConfig &config : current.provider.providers)
                configured = configured || config.type == type;
            if (!configured)
                candidates.append(provider::simplePreset(type));
        }
    } else if (id.isEmpty()) {
        const settings::ProviderConfig *active = current.activeProvider();
        if (active == nullptr) {
            std::fprintf(stderr, "no active engine; run: sniplyngo engine use google-free\n");
            return 1;
        }
        candidates.append(*active);
    } else {
        for (const settings::ProviderConfig &config : current.provider.providers) {
            if (config.id == id)
                candidates.append(config);
        }
        if (candidates.isEmpty()) {
            provider::SimpleApi api {};
            if (!provider::simpleApiForType(id, &api)) {
                std::fprintf(stderr, "unknown engine: %s\n", qPrintable(id));
                return 1;
            }
            candidates.append(provider::simplePreset(id));
        }
    }

    int failures = 0;
    QList<QString> seen;
    for (const settings::ProviderConfig &config : candidates) {
        const QString identity = config.baseUrl + QLatin1Char('|') + config.type + QLatin1Char('|')
            + config.model + QLatin1Char('|') + config.apiKeyRef;
        if (seen.contains(identity))
            continue;
        seen.append(identity);

        std::unique_ptr<provider::Provider> engine
            = createProvider(config, current.provider, startup.secrets.get());
        QString translated;
        core::Error error;
        const bool ok = runProbe(*engine, probeText, probeSource, target, &translated, &error);
        if (ok) {
            std::printf("ok    %-14s %s -> %s\n", qPrintable(config.id), qPrintable(probeText),
                qPrintable(translated));
        } else {
            ++failures;
            std::printf("fail  %-14s %s: %s\n", qPrintable(config.id), core::stableName(error.code),
                qPrintable(error.detail));
        }
        std::fflush(stdout);
    }
    return failures > 0 ? 1 : 0;
}

}  // namespace app
