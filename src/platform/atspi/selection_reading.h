// Pure decisions for the AT-SPI backend: what to ignore, and how to compose a reported selection.
// Kept free of libatspi so it can be tested without an accessibility bus.
#pragma once

#include "core/result.h"

#include <QRect>
#include <QString>
#include <QStringList>

#include <optional>

#include "selection/types.h"

namespace platform::atspi {

struct ReadPolicy {
    QString ownAppName;  // events raised by our own application are ignored
    int hardCap = 20000;  // absolute ceiling before any per-request limit is applied
};

// Password fields and other protected controls must never become events.
bool isIgnorableRole(const QString &roleName);

// True when the event came from this application (prevents self-observation loops).
bool isOwnApplication(const QString &appName, const QString &ownAppName);

// Joins per-range text, trims it and applies the size rules. Never truncates silently.
core::Result<selection::SelectionText> composeSelection(
    const QStringList &rangeTexts, std::optional<QRect> anchor, const ReadPolicy &policy);

}  // namespace platform::atspi
