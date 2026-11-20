#include "app/swappable_provider.h"

namespace app {

void SwappableProvider::set(std::unique_ptr<provider::Provider> next) {
    if (m_current && m_generation != 0)
        m_current->cancel(m_generation);
    m_current = std::move(next);
    m_generation = 0;
}

QString SwappableProvider::id() const {
    return m_current ? m_current->id() : QStringLiteral("none");
}

provider::Capabilities SwappableProvider::capabilities() const {
    return m_current ? m_current->capabilities() : provider::Capabilities {};
}

void SwappableProvider::translate(const provider::TranslateRequest &request, provider::Sink &sink) {
    if (!m_current) {
        sink.onFailed(request.generation,
            core::Error {core::ErrorCode::ProviderNotConfigured,
                QStringLiteral("no translation engine is configured")});
        return;
    }
    m_generation = request.generation;
    m_current->translate(request, sink);
}

void SwappableProvider::cancel(quint64 generation) {
    if (m_current)
        m_current->cancel(generation);
}

}  // namespace app
