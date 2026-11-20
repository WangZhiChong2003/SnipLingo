// Translation provider fake: the test decides when deltas, completion or failure arrive.
#pragma once

#include "core/result.h"

#include <QStringList>

#include "provider/provider.h"

class FakeProvider : public provider::Provider {
    Q_OBJECT
public:
    using provider::Provider::Provider;

    [[nodiscard]] QString id() const override { return QStringLiteral("fake"); }

    [[nodiscard]] provider::Capabilities capabilities() const override {
        provider::Capabilities caps;
        caps.streaming = streaming;
        caps.usesNetwork = usesNetwork;
        return caps;
    }

    void translate(const provider::TranslateRequest &request, provider::Sink &sink) override {
        ++translateCalls;
        requests.append(request);
        m_sink = &sink;
        m_generation = request.generation;
        m_inFlight = true;
    }

    void cancel(quint64 generation) override {
        ++cancelCalls;
        cancelledGenerations.append(generation);
        if (!m_inFlight || generation != m_generation)
            return;
        m_inFlight = false;
        if (emitCancelledFailure && m_sink)
            m_sink->onFailed(generation,
                core::Error {core::ErrorCode::ProviderCancelled, QStringLiteral("cancelled")});
        m_sink = nullptr;
    }

    // ---- test drivers ----
    void deliverDelta(const QString &delta) {
        if (m_sink)
            m_sink->onDelta(m_generation, QStringView(delta));
    }

    void deliverFinished() {
        if (!m_sink)
            return;
        m_inFlight = false;
        m_sink->onFinished(m_generation);
        m_sink = nullptr;
    }

    void deliverError(
        core::ErrorCode code, const QString &detail = QStringLiteral("fake failure")) {
        if (!m_sink)
            return;
        m_inFlight = false;
        m_sink->onFailed(m_generation, core::Error {code, detail});
        m_sink = nullptr;
    }

    [[nodiscard]] bool busy() const { return m_inFlight; }

    // ---- knobs ----
    bool streaming = true;
    bool usesNetwork = true;
    bool emitCancelledFailure = true;

    // ---- observations ----
    int translateCalls = 0;
    int cancelCalls = 0;
    QList<provider::TranslateRequest> requests;
    QList<quint64> cancelledGenerations;

private:
    provider::Sink *m_sink = nullptr;
    quint64 m_generation = 0;
    bool m_inFlight = false;
};
