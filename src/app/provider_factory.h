// Provider construction shared by the runtime and the tests (docs/design/06-provider.md §4).
#pragma once

#include <memory>

#include "provider/provider.h"
#include "secrets/secret_store.h"
#include "settings/settings.h"

namespace app {

// Builds the engine described by `config`: an OpenAI-compatible client, or one of the keyless
// one-shot HTTP engines. Returns null when the configured type is unknown.
[[nodiscard]] std::unique_ptr<provider::Provider> createProvider(
    const settings::ProviderConfig &config, const settings::ProviderSettings &timeouts,
    secrets::SecretStore *secrets);

}  // namespace app
