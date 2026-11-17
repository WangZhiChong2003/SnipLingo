// Deterministic backend for coordinator tests: no threads, no D-Bus, explicit completion control.
#pragma once

#include "core/result.h"

#include <QDateTime>
#include <QHash>

#include <optional>

#include "selection/selection_backend.h"

class FakeSelectionBackend : public selection::SelectionBackend {
    Q_OBJECT
public:
    using selection::SelectionBackend::SelectionBackend;

    selection::BackendProbe probe() override {
        selection::BackendProbe probe;
        probe.interfacePresent = true;
        probe.accessGranted = true;
        probe.identifier = QStringLiteral("fake");
        probe.humanName = QStringLiteral("fake backend");
        return probe;
    }

    core::Result<void> subscribe() override {
        ++subscribeCalls;
        if (subscribeError)
            return *subscribeError;
        m_state = selection::BackendState::Running;
        emit stateChanged(m_state, QString());
        return {};
    }

    void unsubscribe() override {
        ++unsubscribeCalls;
        m_state = selection::BackendState::Stopped;
    }

    void resolveLatest(quint64 seq, selection::ResolveLimits limits,
        selection::ResolveCallback callback) override {
        ++resolveCalls;
        latestLimits = limits;
        m_pending.insert(seq, std::move(callback));
        if (autoResolve)
            completeResolve(seq, resolvedText);
    }

    void cancelPending() override {
        ++cancelCalls;
        // A real backend may still deliver an already-started callback; tests do that explicitly.
        m_pending.clear();
    }

    [[nodiscard]] selection::BackendState state() const override { return m_state; }

    [[nodiscard]] QString backendId() const override { return QStringLiteral("fake"); }

    // ---- test drivers ----
    void emitSelection(quint64 seq, const QString &appName = QStringLiteral("fake-app")) {
        selection::SelectionEvent event;
        event.seq = seq;
        event.source.appName = appName;
        event.source.backendId = QStringLiteral("fake");
        event.observedAt = QDateTime::currentDateTimeUtc();
        emit eventObserved(event);
    }

    void completeResolve(quint64 seq, const QString &text) {
        const auto pending = m_pending.find(seq);
        if (pending == m_pending.end())
            return;
        const selection::ResolveCallback callback = pending.value();
        m_pending.erase(pending);
        if (resolveError) {
            callback(seq, *resolveError);
            return;
        }
        selection::SelectionText value;
        value.text = text;
        callback(seq, value);
    }

    void completeResolveWithError(quint64 seq, core::ErrorCode code) {
        const auto pending = m_pending.find(seq);
        if (pending == m_pending.end())
            return;
        const selection::ResolveCallback callback = pending.value();
        m_pending.erase(pending);
        callback(seq, core::Error {code, QStringLiteral("fake")});
    }

    [[nodiscard]] bool hasPendingResolve() const { return !m_pending.isEmpty(); }

    [[nodiscard]] quint64 pendingSeq() const {
        return m_pending.isEmpty() ? 0 : m_pending.constBegin().key();
    }

    // ---- knobs ----
    bool autoResolve = false;
    QString resolvedText = QStringLiteral("fake selection");
    std::optional<core::Error> subscribeError;
    std::optional<core::Error> resolveError;

    // ---- observations ----
    int subscribeCalls = 0;
    int unsubscribeCalls = 0;
    int resolveCalls = 0;
    int cancelCalls = 0;
    selection::ResolveLimits latestLimits;

private:
    selection::BackendState m_state = selection::BackendState::Uninitialised;
    QHash<quint64, selection::ResolveCallback> m_pending;
};
