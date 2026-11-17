// The only delay primitive modules use, so timing is greppable and testable
// (docs/design/01-core.md §4).
#pragma once

#include <QObject>
#include <QTimer>

#include <functional>

namespace core {

class CancellableDelay : public QObject {
    Q_OBJECT
public:
    explicit CancellableDelay(QObject *parent = nullptr);

    // Restarting replaces the pending callback; the previous one is never invoked.
    void start(int milliseconds, std::function<void()> onElapsed);
    void cancel();

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] int remainingMs() const noexcept;

private:
    void fire();

    QTimer m_timer;
    std::function<void()> m_callback;
    quint64 m_token = 0;
};

}  // namespace core
