#include "core/delay.h"

namespace core {

CancellableDelay::CancellableDelay(QObject *parent)
    : QObject(parent) {
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &CancellableDelay::fire);
}

void CancellableDelay::start(int milliseconds, std::function<void()> onElapsed) {
    ++m_token;
    m_callback = std::move(onElapsed);
    if (milliseconds <= 0) {
        // Keep the contract "callback runs asynchronously" even for degenerate input.
        m_timer.start(1);
        return;
    }
    m_timer.start(milliseconds);
}

void CancellableDelay::cancel() {
    ++m_token;
    m_timer.stop();
    m_callback = nullptr;
}

bool CancellableDelay::isActive() const noexcept {
    return m_timer.isActive();
}

int CancellableDelay::remainingMs() const noexcept {
    return m_timer.remainingTime();
}

void CancellableDelay::fire() {
    // The timer is single-shot and start()/cancel() bump the token, so a callback can only run for
    // the most recent start(). Taking it out first makes re-entrant start() inside the callback
    // safe.
    std::function<void()> callback = std::move(m_callback);
    m_callback = nullptr;
    if (callback)
        callback();
}

}  // namespace core
