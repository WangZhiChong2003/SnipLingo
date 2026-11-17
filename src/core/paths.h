// XDG base directories, resolved once at startup (docs/design/01-core.md §4).
#pragma once

#include "core/result.h"

#include <QString>

namespace core {

class XdgPaths {
public:
    static XdgPaths resolve();

    // Test seam: applied once per process, before anything else reads paths.
    static void overrideForTests(const XdgPaths &paths);
    static void clearTestOverride();

    [[nodiscard]] QString configDir() const { return m_config; }

    [[nodiscard]] QString dataDir() const { return m_data; }

    [[nodiscard]] QString stateDir() const { return m_state; }

    [[nodiscard]] QString cacheDir() const { return m_cache; }

    [[nodiscard]] QString settingsFile() const;
    [[nodiscard]] QString logFile() const;

    // Creates config/data/state directories with mode 0700 when missing.
    Result<void> ensureDirectories() const;

private:
    QString m_config;
    QString m_data;
    QString m_state;
    QString m_cache;
};

}  // namespace core
