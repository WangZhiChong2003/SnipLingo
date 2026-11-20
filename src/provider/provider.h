// Translation provider boundary.
//
// The provider never reads settings.json and never touches the secret store directly: the API key
// arrives through a callback the application wires up, which keeps this module testable without a
// keyring.
#pragma once

#include "core/error.h"

#include <QObject>
#include <QString>
#include <QStringView>

#include <functional>

#include "settings/settings.h"

namespace provider {

struct TranslateRequest {
    quint64 generation = 0;
    QString text;
    QString sourceLanguage;  // "auto" or BCP-47
    QString targetLanguage;  // BCP-47
    bool streaming = true;
};

struct Capabilities {
    bool streaming = true;
    bool usesNetwork = true;
    int maxInputChars = 8000;
};

class Sink {
public:
    virtual ~Sink() = default;
    // All callbacks are delivered on the thread that owns the provider (the UI thread).
    virtual void onDelta(quint64 generation, QStringView delta) = 0;
    virtual void onFinished(quint64 generation) = 0;
    virtual void onFailed(quint64 generation, core::Error error) = 0;
};

class Provider : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~Provider() override = default;

    [[nodiscard]] virtual QString id() const = 0;
    [[nodiscard]] virtual Capabilities capabilities() const = 0;
    virtual void translate(const TranslateRequest &request, Sink &sink) = 0;
    virtual void cancel(quint64 generation) = 0;
};

// Resolves the API key for the active provider; empty result means "no credential needed".
using ApiKeySource = std::function<core::Result<QString>()>;

}  // namespace provider
