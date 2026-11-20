// A stable Provider handle whose implementation can be replaced when settings change.
//
// The coordinator holds a Provider* for its lifetime; reconfiguring an engine must not invalidate
// that pointer or require rebuilding the request pipeline.
#pragma once

#include <memory>

#include "provider/provider.h"

namespace app {

class SwappableProvider : public provider::Provider {
    Q_OBJECT
public:
    using provider::Provider::Provider;

    void set(std::unique_ptr<provider::Provider> next);

    [[nodiscard]] bool hasProvider() const { return m_current != nullptr; }

    [[nodiscard]] provider::Provider *current() const { return m_current.get(); }

    [[nodiscard]] QString id() const override;
    [[nodiscard]] provider::Capabilities capabilities() const override;
    void translate(const provider::TranslateRequest &request, provider::Sink &sink) override;
    void cancel(quint64 generation) override;

private:
    std::unique_ptr<provider::Provider> m_current;
    quint64 m_generation = 0;
};

}  // namespace app
