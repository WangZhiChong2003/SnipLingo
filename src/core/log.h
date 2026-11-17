// Logging with a structural guarantee: there is no overload that accepts user text.
// Callers pass counts, codes and backend identifiers only (docs/design/01-core.md §4).
#pragma once

#include "core/error.h"

#include <QString>

#include <string_view>

namespace core::log {

enum class Level { Debug, Info, Warning, Error };

// Level from SNIPLYNGO_LOG unless overridden; optional file sink rotates at 1 MiB × 3.
void init(Level minimum, const QString &optionalLogFile = {});
void shutdown();

void write(Level level, std::string_view module, const QString &message);
void writeError(std::string_view module, const Error &error);

[[nodiscard]] bool isEnabled(Level level);

}  // namespace core::log
