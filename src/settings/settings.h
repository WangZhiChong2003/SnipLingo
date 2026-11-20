// Persisted preference document and consent record.
#pragma once

#include "core/result.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace settings {

struct ProviderConfig {
    QString id;
    QString type = QStringLiteral("openai-compatible");
    QString label;
    QString baseUrl;
    QString model;
    QString apiKeyRef;
    bool streaming = true;
    bool markedLocal = false;

    [[nodiscard]] QString host() const;
    [[nodiscard]] bool isLoopback() const;

    [[nodiscard]] bool isLocal() const { return isLoopback() || markedLocal; }

    [[nodiscard]] QString destinationLabel() const;
    // sha256 over type|scheme|host|port|path|model; changing any of them invalidates consent.
    [[nodiscard]] QString fingerprint() const;
};

struct SelectionSettings {
    bool automaticEnabled = false;
    bool paused = false;
    bool showResultImmediately = false;
    // Wayland clients cannot place a window next to a selection. Opting in runs the interface
    // through XWayland so the pointer-owned anchor can be used; the pointer may be stale over a
    // native Wayland source window, so this is not enabled by default.
    bool xwaylandPopup = false;
    QString backend = QStringLiteral("auto");
    int stabilityDelayMs = 350;
    int maxSelectionChars = 4000;
    int resolveTimeoutMs = 1500;
};

struct LanguageSettings {
    QString source = QStringLiteral("auto");
    QString target;
};

struct ProviderSettings {
    QList<ProviderConfig> providers;
    QString activeId;
    int firstByteTimeoutMs = 12000;
    int totalTimeoutMs = 30000;
};

struct NetworkSettings {
    // "system" honours the environment/system proxy, "manual" uses proxyUrl, "none" connects
    // directly.
    QString proxy = QStringLiteral("system");
    QString proxyUrl;

    [[nodiscard]] bool needsProxy() const { return proxy != QLatin1String("none"); }
};

struct ConsentRecord {
    QString fingerprint;
    QString destinationLabel;
    QString grantedAt;

    [[nodiscard]] bool valid() const { return !fingerprint.isEmpty(); }
};

struct UiSettings {
    QString theme = QStringLiteral("system");
    // Whole-interface zoom for the main window and the floating result.
    qreal scale = 1.0;
    // Translation and original text size multiplier inside the result cards.
    qreal fontScale = 1.0;
    bool reduceMotion = false;
};

struct Settings {
    int version = 1;
    SelectionSettings selection;
    ProviderSettings provider;
    LanguageSettings language;
    ConsentRecord consent;
    NetworkSettings network;
    UiSettings ui;

    [[nodiscard]] const ProviderConfig *activeProvider() const;
    // Consent is required only for automatic sending to a non-local destination.
    [[nodiscard]] bool mayAutoSend() const;
};

enum class Outcome { Loaded, LoadedWithDefaults, Recovered };

struct LoadResult {
    Outcome outcome = Outcome::LoadedWithDefaults;
    Settings settings;
    QStringList issues;  // field names only, never values
    QJsonObject baseDocument;  // parsed document, used to preserve unknown keys on save
};

// Serialization. `mergePreservingUnknown` keeps keys written by a newer version.
QJsonObject toJson(const Settings &settings);
QJsonObject mergePreservingUnknown(const QJsonObject &original, const QJsonObject &known);

// Parses, migrates and validates. Garbage input yields defaults plus a `SettingsCorrupt` issue.
LoadResult loadFromDocument(const QJsonObject &document, bool documentWasReadable);

// Clamps and drops invalid entries; appends field names to `issues`.
bool validate(Settings &settings, QStringList *issues);

// Desktop-locale suggestion; empty when no mapping exists (the UI must ask instead of guessing).
QString defaultTargetLanguage();

}  // namespace settings
