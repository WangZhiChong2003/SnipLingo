// Selection acquisition vocabulary (docs/design/04-selection.md §4).
//
// A SelectionEvent is deliberately cheap and carries no text: text is only materialised by an
// explicit resolveLatest() call from the coordinator, after the stability delay.
#pragma once

#include <QDateTime>
#include <QRect>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace selection {

struct SelectionSource {
    QString appName;  // accessible application name; may be empty
    QString backendId;  // "atspi" | "x11-primary" | "wlr-data-control"
    // Deliberately absent: window titles, process paths, source pids.
};

struct SelectionEvent {
    quint64 seq = 0;  // per-backend monotonic; 0 is invalid
    SelectionSource source;
    QDateTime observedAt;
    std::optional<QRect>
        anchorLogical;  // screen coordinates in logical pixels, when the backend knows them
};

struct ResolveLimits {
    int maxChars = 4000;
    int timeoutMs = 1500;
};

struct SelectionText {
    QString text;  // trimmed by the backend; internal whitespace untouched
    bool truncatedBySource = false;
    std::optional<QRect> anchorLogical;
};

struct BackendProbe {
    bool interfacePresent = false;  // the protocol/interface exists in this session
    bool accessGranted = false;  // this process may use it
    QString identifier;
    QString humanName;
    QString reason;  // non-sensitive explanation
};

enum class BackendState { Uninitialised, Subscribing, Running, Stopped, Denied, Failed };

}  // namespace selection
