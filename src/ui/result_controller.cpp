#include "ui/result_controller.h"

#include "core/error.h"

#include <QClipboard>
#include <QGuiApplication>

namespace ui {
namespace {

// Streaming deltas arrive per token; QML layout work is throttled so the window stays smooth.
constexpr int kNotifyIntervalMs = 50;

}  // namespace

ResultController::ResultController(coordinator::RequestCoordinator *coordinator, QObject *parent)
    : QObject(parent)
    , m_coordinator(coordinator) {
    Q_ASSERT(m_coordinator != nullptr);
    m_throttle.setParent(this);
    connect(m_coordinator, &coordinator::RequestCoordinator::viewChanged, this,
        &ResultController::onViewChanged);
    connect(m_coordinator, &coordinator::RequestCoordinator::presentationRequested, this,
        [this](coordinator::Origin origin, const QRect &) {
            m_originalCollapsed = true;
            // The anchor is known before the text: keep the loading affordance visible while the
            // backend resolves it.
            if (origin == coordinator::Origin::AutomaticSelection)
                m_keepVisible = true;
        });
    // State transitions without a payload (waiting, resolving, paused) still change the window's
    // state key.
    connect(m_coordinator, &coordinator::RequestCoordinator::stateChanged, this,
        [this](coordinator::State state) {
            if (m_view.state == state)
                return;
            m_view.state = state;
            if (state == coordinator::State::Idle || state == coordinator::State::Paused
                || state == coordinator::State::Disabled)
                m_keepVisible = false;
            scheduleNotify();
        });
}

bool ResultController::visible() const {
    // Selection-change events also occur for caret movement. Do not show a window until the
    // settled selection has actually yielded text (or an actionable error).
    return m_view.state == coordinator::State::Translating
        || m_view.state == coordinator::State::ShowingResult
        || m_view.state == coordinator::State::ShowingError
        || (m_keepVisible
            && (m_view.state == coordinator::State::WaitingStability
                || m_view.state == coordinator::State::Resolving));
}

QString ResultController::translatedText() const {
    return m_view.translatedText;
}

QString ResultController::originalText() const {
    return m_view.originalText;
}

QString ResultController::providerLabel() const {
    return m_view.providerLabel;
}

QString ResultController::sourceLanguage() const {
    return m_view.sourceLanguage;
}

QString ResultController::targetLanguage() const {
    return m_view.targetLanguage;
}

QString ResultController::stateKey() const {
    switch (m_view.state) {
    case coordinator::State::Disabled:
    case coordinator::State::Paused:
    case coordinator::State::Idle:
        return QStringLiteral("hidden");
    case coordinator::State::WaitingStability:
    case coordinator::State::Resolving:
        return QStringLiteral("loading");
    case coordinator::State::Translating:
        return m_view.translatedText.isEmpty() ? QStringLiteral("loading")
                                               : QStringLiteral("streaming");
    case coordinator::State::ShowingResult:
        return QStringLiteral("done");
    case coordinator::State::ShowingError:
        return QStringLiteral("error");
    }
    return QStringLiteral("hidden");
}

QString ResultController::errorKey() const {
    if (m_view.state != coordinator::State::ShowingError)
        return {};
    return QString::fromLatin1(core::stableName(m_view.lastError.code));
}

bool ResultController::canRetry() const {
    return m_view.state == coordinator::State::ShowingError && m_view.canRetry;
}

bool ResultController::originalVisible() const {
    return m_view.state != coordinator::State::WaitingStability
        && m_view.state != coordinator::State::Resolving && !m_view.originalText.isEmpty();
}

bool ResultController::originalCollapsed() const {
    return m_originalCollapsed;
}

void ResultController::copyResult() {
    // The only clipboard write in the application, and it always follows an explicit user action.
    if ((m_view.state != coordinator::State::Translating
            && m_view.state != coordinator::State::ShowingResult)
        || m_view.translatedText.isEmpty())
        return;
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(m_view.translatedText);
}

void ResultController::dismiss() {
    m_coordinator->dismissResult();
}

void ResultController::dismissOnFocusLoss() {
    m_coordinator->dismissOnFocusLoss();
}

void ResultController::retry() {
    m_coordinator->retryLastRequest();
}

void ResultController::toggleOriginal() {
    m_originalCollapsed = !m_originalCollapsed;
    emit changed();
}

void ResultController::onViewChanged(const coordinator::ResultView &view) {
    m_view = view;
    if (view.state == coordinator::State::Translating
        || view.state == coordinator::State::ShowingResult
        || view.state == coordinator::State::ShowingError)
        m_keepVisible = true;
    else if (view.state == coordinator::State::Idle || view.state == coordinator::State::Paused
        || view.state == coordinator::State::Disabled)
        m_keepVisible = false;
    if (view.state == coordinator::State::Translating) {
        scheduleNotify();  // coalesced while streaming
        return;
    }
    m_throttle.cancel();
    emit changed();
}

void ResultController::scheduleNotify() {
    if (m_throttle.isActive())
        return;
    m_throttle.start(kNotifyIntervalMs, [this] { emit changed(); });
}

}  // namespace ui
