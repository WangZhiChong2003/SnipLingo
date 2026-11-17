// Credential storage boundary (docs/design/03-secrets.md).
//
// Synchronous on purpose: the concrete backends own their threading (a keyring prompt must never
// block the UI thread, and the application wraps calls accordingly). There is deliberately no
// plaintext file fallback — if no secure store works, the only alternative is memory for this
// session.
#pragma once

#include "core/result.h"

#include <QString>

#include <memory>

namespace secrets {

enum class Availability {
    Available,  // persistent OS-backed storage works
    SessionOnly,  // no persistent store: secrets live until the process exits
    Unavailable  // not even session storage
};

struct AvailabilityReport {
    Availability availability = Availability::Unavailable;
    QString backend;  // "session", "secret-service", "none"
    QString reason;  // non-sensitive explanation
};

class SecretStore {
public:
    virtual ~SecretStore() = default;

    [[nodiscard]] virtual AvailabilityReport availability() const = 0;
    virtual core::Result<QString> lookup(const QString &key) = 0;
    virtual core::Result<void> store(const QString &key, const QString &secret) = 0;
    virtual core::Result<void> remove(const QString &key) = 0;
};

// Key ids look like "provider/openai-main/apiKey": lowercase segments, no free-form text.
[[nodiscard]] bool isValidKeyId(const QString &key);

// What the session offers, in terms the application can report without touching any credential.
struct ServiceProbe {
    bool sessionBusConnected = false;
    bool secretServiceRegistered = false;  // org.freedesktop.secrets present on the session bus
    QString reason;
};

// Picks the best store this build can offer. A registered secret service is reported in the reason
// string but does not upgrade the store yet: the persistent backend is not implemented (03 §5.1),
// and pretending otherwise would make the capability report lie.
std::unique_ptr<SecretStore> createSecretStore(const ServiceProbe &probe);

}  // namespace secrets
