// Backend choice from probed facts, never from desktop names (docs/design/04-selection.md §7.4).
#pragma once

#include "core/result.h"

#include <QString>
#include <QStringList>

namespace selection {

struct SelectionSession {
    bool waylandSession = false;
    bool x11Session = false;
    bool x11PrimaryAvailable = false;
    bool accessibilityBusReachable = false;
    QStringList waylandDataControlGlobals;  // empty when no Wayland connection or no such global
};

class BackendSelector {
public:
    // `configured` is "auto" or an explicit backend id from settings. Never selects two backends.
    static core::Result<QString> choose(const SelectionSession &session, const QString &configured);

    // Human-readable reason for the report; must not contain user data.
    static QString explain(const SelectionSession &session, const QString &chosen);
};

}  // namespace selection
