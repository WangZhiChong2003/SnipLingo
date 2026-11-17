// In-memory store: the only credential backend that exists in this build.
#pragma once

#include <QHash>

#include "secrets/secret_store.h"

namespace secrets {

class SessionOnlyStore : public SecretStore {
public:
    [[nodiscard]] AvailabilityReport availability() const override;
    core::Result<QString> lookup(const QString &key) override;
    core::Result<void> store(const QString &key, const QString &secret) override;
    core::Result<void> remove(const QString &key) override;

    // Cleared on destruction and on explicit request (application shutdown).
    void clear();

    [[nodiscard]] int size() const { return m_secrets.size(); }

private:
    QHash<QString, QString> m_secrets;
};

}  // namespace secrets
