// AT-SPI selection backend (docs/design/04-selection.md §7.2, option B chosen by ADR 0006).
//
// libatspi's event loop runs on a dedicated thread. Events retain only the accessible object; text
// is read after the coordinator's stability delay so transient empty ranges do not lose a
// selection.
#pragma once

#include "core/result.h"
#include "platform/atspi/selection_reading.h"

#include <QMutex>
#include <QSemaphore>
#include <QThread>

#include <atomic>
#include <functional>
#include <optional>

#include "selection/selection_backend.h"

class AtspiBackend : public selection::SelectionBackend {
    Q_OBJECT
public:
    explicit AtspiBackend(QObject *parent = nullptr);
    ~AtspiBackend() override;

    [[nodiscard]] selection::BackendProbe probe() override;
    core::Result<void> subscribe() override;
    void unsubscribe() override;
    void resolveLatest(
        quint64 seq, selection::ResolveLimits limits, selection::ResolveCallback callback) override;
    void cancelPending() override;

    [[nodiscard]] selection::BackendState state() const override { return m_state; }

    [[nodiscard]] QString backendId() const override { return QStringLiteral("atspi"); }

    // Application name used to ignore our own events; defaults to
    // QCoreApplication::applicationName().
    void setOwnAppName(const QString &name);

    // Internal: called from the libatspi event thread. Public only because the C callback needs it.
    void handleAccessibleEvent(void *atspiEvent);

private:
    void workerLoop();
    // Runs `fn` inside the libatspi event loop (the only thread allowed to touch libatspi).
    bool runInEventLoop(std::function<void()> fn, bool waitForCompletion);
    bool registerListener(QString *reason);
    void deregisterListener();
    void publishSelection(const QString &appName, void *accessible);
    core::Result<selection::SelectionText> readSelection(
        void *accessible, selection::ResolveLimits limits);

    QThread *m_thread = nullptr;
    void *m_listener = nullptr;  // AtspiEventListener*, owned by the worker thread
    QSemaphore m_ready;
    // Events already in flight are ignored as soon as observation is paused.
    std::atomic<bool> m_observing {false};
    selection::BackendState m_state = selection::BackendState::Uninitialised;

    QMutex m_cacheMutex;
    std::optional<quint64> m_cachedSeq;
    void *m_cachedSource = nullptr;  // AtspiAccessible*, a retained GObject reference

    quint64 m_nextSeq = 1;  // worker thread only
    platform::atspi::ReadPolicy m_policy;
};
