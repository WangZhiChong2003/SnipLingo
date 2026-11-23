// X11 PRIMARY selection backend: XFixes ownership notifications plus on-demand conversion.
// Unlike the AT-SPI backend this one owns its event loop, so text is read only when the coordinator
// asks.
#pragma once

#include "core/result.h"
#include "platform/x11/x11_protocol.h"

#include <QMutex>
#include <QThread>

#include <atomic>
#include <deque>
#include <functional>

#include "selection/selection_backend.h"

struct xcb_connection_t;

class X11Backend : public selection::SelectionBackend {
    Q_OBJECT
public:
    explicit X11Backend(QObject *parent = nullptr);
    ~X11Backend() override;

    [[nodiscard]] selection::BackendProbe probe() override;
    core::Result<void> subscribe() override;
    void unsubscribe() override;
    void resolveLatest(
        quint64 seq, selection::ResolveLimits limits, selection::ResolveCallback callback) override;
    void cancelPending() override;

    [[nodiscard]] selection::BackendState state() const override { return m_state.load(); }

    [[nodiscard]] QString backendId() const override { return QStringLiteral("x11-primary"); }

private:
    struct Command;

    void workerLoop();
    void reportFailure(selection::BackendState state, const QString &reason);
    void postCommand(std::function<void()> command);
    void drainCommands();
    void handleXfixesEvent(const void *event);
    void performResolve(
        quint64 seq, selection::ResolveLimits limits, selection::ResolveCallback callback);

    QThread *m_thread = nullptr;
    std::atomic<bool> m_stopRequested {false};
    std::atomic<selection::BackendState> m_state {selection::BackendState::Uninitialised};
    QMutex m_stateMutex;
    QString m_failureReason;

    QMutex m_queueMutex;
    std::deque<std::function<void()>> m_commands;
    int m_wakeFd = -1;
    int m_wakeReadFd = -1;

    void *m_connection = nullptr;  // xcb_connection_t*, owned by the worker thread
    uint32_t m_rootWindow = 0;
    // Atoms are plain integers so the header stays free of xcb.
    uint32_t m_atomPrimary = 0;
    uint32_t m_atomUtf8 = 0;
    uint32_t m_atomIncr = 0;
    uint32_t m_atomProperty = 0;
    uint32_t m_atomNetWmPid = 0;
    uint8_t m_xfixesEventBase = 0;  // worker thread only
    quint64 m_nextSeq = 1;  // worker thread only
    std::atomic<quint64> m_lastEmittedSeq {0};
    uint32_t m_lastOwner = 0;  // worker thread only
    uint32_t m_lastOwnerPid = 0;  // worker thread only
};
