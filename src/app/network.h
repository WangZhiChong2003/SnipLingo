// Proxy configuration for every outbound request (docs/design/06-provider.md §7).
//
// Qt does not read the system proxy unless asked, so a machine behind a proxy silently fails to
// reach any engine. This applies the configured mode once per process and reports what is in
// effect.
#pragma once

#include <QString>

#include "settings/settings.h"

namespace app {

void applyNetworkSettings(const settings::Settings &settings);

// Human-readable description of the proxy actually in effect, for diagnostics.
[[nodiscard]] QString describeProxy();

}  // namespace app
