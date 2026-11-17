// One backend per process, running its I/O on its own thread (docs/design/04-selection.md §4).
//
// Contract highlights that the coordinator relies on:
//   * subscribe() never emits an event for a selection that already existed;
//   * resolveLatest() is asynchronous and reports through one callback, at most once per seq;
//   * cancelPending() may suppress the callback entirely;
//   * no event and no error message ever carries selected text.
#pragma once

#include "core/result.h"

#include <QObject>

#include <functional>

#include "selection/types.h"

namespace selection {

using ResolveCallback = std::function<void(quint64 seq, core::Result<SelectionText>)>;

class SelectionBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~SelectionBackend() override = default;

    [[nodiscard]] virtual BackendProbe probe() = 0;  // no user-visible side effects, no text access
    virtual core::Result<void> subscribe() = 0;
    virtual void unsubscribe() = 0;
    virtual void resolveLatest(quint64 seq, ResolveLimits limits, ResolveCallback callback) = 0;
    virtual void cancelPending() = 0;

    [[nodiscard]] virtual BackendState state() const = 0;
    [[nodiscard]] virtual QString backendId() const = 0;

signals:
    void eventObserved(const selection::SelectionEvent &event);
    void stateChanged(selection::BackendState state, const QString &reason);
    void probeChanged(const selection::BackendProbe &probe);
};

}  // namespace selection

Q_DECLARE_METATYPE(selection::BackendState)
Q_DECLARE_METATYPE(selection::SelectionEvent)
