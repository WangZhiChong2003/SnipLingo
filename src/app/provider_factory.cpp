#include "app/provider_factory.h"

#include "provider/openai_provider.h"
#include "provider/simple_translate_provider.h"

namespace app {

std::unique_ptr<provider::Provider> createProvider(const settings::ProviderConfig &config,
    const settings::ProviderSettings &timeouts, secrets::SecretStore *secrets) {
    const QString keyRef = config.apiKeyRef;
    const bool local = config.isLocal();
    provider::ApiKeySource apiKey = [secrets, keyRef, local]() -> core::Result<QString> {
        if (keyRef.isEmpty())
            return QStringLiteral("");  // keyless engines need no credential
        const core::Result<QString> key = secrets->lookup(keyRef);
        if (!key.ok() && local && key.error().code == core::ErrorCode::SecretNotFound)
            return QStringLiteral("");
        return key;
    };

    provider::SimpleApi simpleApi {};
    if (provider::simpleApiForType(config.type, &simpleApi)) {
        return std::make_unique<provider::SimpleTranslateProvider>(
            config, timeouts, simpleApi, std::move(apiKey));
    }
    return std::make_unique<provider::OpenAiProvider>(config, timeouts, std::move(apiKey));
}

}  // namespace app
