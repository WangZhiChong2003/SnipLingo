// Staleness token shared by every flow that can replace the single result window.
// Owned by the request coordinator only.
#pragma once

#include <QtGlobal>

namespace core {

using Generation = quint64;

class GenerationSource {
public:
    // Monotonic; starts at 1 so that Generation{0} can mean "no generation".
    Generation next() noexcept {
        m_valid = true;
        return ++m_current;
    }

    [[nodiscard]] Generation current() const noexcept { return m_current; }

    // A completion is acceptworthy only when its generation is current and still valid.
    [[nodiscard]] bool isCurrent(Generation generation) const noexcept {
        return m_valid && generation != 0 && generation == m_current;
    }

    // Makes every outstanding generation stale without issuing a new one (pause, dismiss,
    // shutdown).
    void invalidate() noexcept { m_valid = false; }

    [[nodiscard]] bool valid() const noexcept { return m_valid; }

private:
    Generation m_current = 0;
    bool m_valid = false;
};

}  // namespace core
