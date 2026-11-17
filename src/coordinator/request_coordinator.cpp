#include "coordinator/request_coordinator.h"

#include "core/log.h"

namespace coordinator {

RequestCoordinator::RequestCoordinator(selection::SelectionBackend *backend,
    provider::Provider *provider, settings::SettingsStore *settings, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_provider(provider)
    , m_settings(settings) {
    // The backend may be absent (no observation on this session); explicit flows still work.
    Q_ASSERT(m_provider != nullptr);
    Q_ASSERT(m_settings != nullptr);

    m_stability.setParent(this);
    m_view.originalCollapsed = true;

    if (m_backend != nullptr) {
        connect(m_backend, &selection::SelectionBackend::eventObserved, this,
            &RequestCoordinator::onSelectionEvent);
        connect(m_backend, &selection::SelectionBackend::stateChanged, this,
            &RequestCoordinator::onBackendStateChanged);
    }
    connect(m_settings, &settings::SettingsStore::changed, this,
        &RequestCoordinator::onSettingsChanged);
}

RequestCoordinator::~RequestCoordinator() {
    abortInFlight();
}

bool RequestCoordinator::automaticEnabled() const {
    return m_settings->current().selection.automaticEnabled;
}

core::Result<void> RequestCoordinator::enableAutomatic() {
    // An explicit translation can be in progress even though observation is still disabled.
    if (m_observed)
        return {};

    if (m_settings->current().activeProvider() == nullptr) {
        return core::Error {core::ErrorCode::ProviderNotConfigured,
            QStringLiteral("no active provider is configured")};
    }

    if (m_backend == nullptr) {
        return core::Error {core::ErrorCode::SelectionBackendAbsent,
            QStringLiteral("this session offers no selection backend")};
    }

    if (auto subscribed = m_backend->subscribe(); !subscribed.ok()) {
        emit notice(subscribed.error());
        return subscribed;
    }
    m_observed = true;

    m_updatingSettings = true;
    const core::Result<void> enabled = m_settings->setAutomaticEnabled(true);
    const core::Result<void> resumed = m_settings->setPaused(false);
    m_updatingSettings = false;
    if (!enabled.ok()) {
        m_backend->unsubscribe();
        m_observed = false;
        return enabled;
    }
    if (!resumed.ok()) {
        m_backend->unsubscribe();
        m_observed = false;
        return resumed;
    }

    m_consentNotified = false;
    setState(State::Idle);
    return {};
}

void RequestCoordinator::disableAutomatic() {
    abortInFlight();
    m_stability.cancel();
    if (m_observed) {
        if (m_backend != nullptr)
            m_backend->unsubscribe();
        m_observed = false;
    }
    clearRetained();

    m_updatingSettings = true;
    m_settings->setAutomaticEnabled(false);
    m_updatingSettings = false;
    setState(State::Disabled);
}

core::Result<void> RequestCoordinator::setPaused(bool paused) {
    m_updatingSettings = true;
    const core::Result<void> saved = m_settings->setPaused(paused);
    m_updatingSettings = false;
    if (!saved.ok())
        return saved;

    if (paused) {
        abortInFlight();
        m_stability.cancel();
        if (m_observed) {
            if (m_backend != nullptr)
                m_backend->unsubscribe();
            m_observed = false;
        }
        clearRetained();
        setState(automaticEnabled() ? State::Paused : State::Disabled);
        return {};
    }

    if (!automaticEnabled()) {
        setState(State::Disabled);
        return {};
    }
    if (m_backend == nullptr) {
        m_settings->setPaused(true);
        return core::Error {core::ErrorCode::SelectionBackendAbsent,
            QStringLiteral("this session offers no selection backend")};
    }
    if (auto subscribed = m_backend->subscribe(); !subscribed.ok()) {
        emit notice(subscribed.error());
        m_updatingSettings = true;
        m_settings->setPaused(true);
        m_updatingSettings = false;
        setState(State::Paused);
        return subscribed;
    }
    m_observed = true;
    m_consentNotified = false;
    setState(State::Idle);
    return {};
}

core::Result<void> RequestCoordinator::submitExplicitText(const QString &text, Origin origin) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return core::Error {core::ErrorCode::SelectionEmpty, QStringLiteral("empty input")};
    }
    const int maxChars = m_settings->current().selection.maxSelectionChars;
    if (trimmed.size() > maxChars) {
        return core::Error {core::ErrorCode::SelectionTooLarge,
            QStringLiteral("%1 characters exceed the %2 limit").arg(trimmed.size()).arg(maxChars)};
    }
    if (m_settings->current().activeProvider() == nullptr) {
        return core::Error {core::ErrorCode::ProviderNotConfigured,
            QStringLiteral("no active provider is configured")};
    }
    if (m_settings->current().language.target.isEmpty()) {
        return core::Error {core::ErrorCode::SettingsInvalid,
            QStringLiteral("choose the language to translate into")};
    }

    abortInFlight();
    m_generation.next();
    m_consentNotified = false;
    beginTranslate(trimmed, origin);
    return {};
}

void RequestCoordinator::dismissResult() {
    abortInFlight();
    m_stability.cancel();
    clearRetained();

    m_view = ResultView {};
    m_view.viewId = ++m_viewId;
    m_view.originalCollapsed = true;
    emit viewChanged(m_view);
    setState(automaticEnabled() ? (m_observed ? State::Idle : State::Paused) : State::Disabled);
}

void RequestCoordinator::dismissOnFocusLoss() {
    if (m_state != State::WaitingStability && m_state != State::Resolving) {
        dismissResult();
        return;
    }
    // A click outside can also begin a newer selection. Hide the old view without cancelling the
    // selection that is already being debounced or resolved.
    clearRetained();
    m_beforeSelection = State::Idle;
    m_view = ResultView {};
    m_view.viewId = ++m_viewId;
    emit viewChanged(m_view);
}

void RequestCoordinator::retryLastRequest() {
    if (!m_hasText || m_state != State::ShowingError)
        return;
    const QString text = m_text;
    const Origin origin = m_origin;
    abortInFlight();
    m_generation.next();
    beginTranslate(text, origin, QRect(), false);
}

core::Result<void> RequestCoordinator::recheckConsent() {
    const settings::Settings &current = m_settings->current();
    const settings::ProviderConfig *active = current.activeProvider();
    if (active == nullptr) {
        return core::Error {core::ErrorCode::ProviderNotConfigured,
            QStringLiteral("no active provider is configured")};
    }
    if (current.mayAutoSend()) {
        m_consentNotified = false;
        return {};
    }
    if (!m_consentNotified) {
        m_consentNotified = true;
        emit consentRequired(active->destinationLabel(), current.consent.valid());
    }
    return {};
}

// ---------------------------------------------------------------- selection events

void RequestCoordinator::onSelectionEvent(const selection::SelectionEvent &event) {
    // An event already queued by a backend can arrive after pause or disable, including while an
    // explicit translation is using the state machine.
    if (!m_observed)
        return;
    switch (m_state) {
    case State::Disabled:
    case State::Paused:
        return;
    default:
        break;
    }
    if (event.seq == 0 || event.seq <= m_lastSeenSeq)
        return;
    if (!automaticEnabled())
        return;

    if (m_state != State::WaitingStability && m_state != State::Resolving) {
        m_beforeSelection = m_state == State::ShowingResult || m_state == State::ShowingError
            ? m_state
            : State::Idle;
    }
    m_lastSeenSeq = event.seq;
    m_pendingEvent = event;

    // A newer selection supersedes whatever was in flight, and its completion must not reach the
    // window.
    abortInFlight();
    m_generation.next();

    setState(State::WaitingStability);
    const int delayMs = m_settings->current().selection.stabilityDelayMs;
    m_stability.start(delayMs, [this] { beginResolve(); });
}

void RequestCoordinator::onBackendStateChanged(
    selection::BackendState state, const QString &reason) {
    if (state == selection::BackendState::Running)
        return;
    if (state == selection::BackendState::Denied || state == selection::BackendState::Failed) {
        emit notice(core::Error {core::ErrorCode::SelectionBackendFailed, reason});
        if (m_observed) {
            if (m_backend != nullptr)
                m_backend->unsubscribe();
            m_observed = false;
        }
        if (automaticEnabled()) {
            m_updatingSettings = true;
            m_settings->setPaused(true);
            m_updatingSettings = false;
        }
        setState(automaticEnabled() ? State::Paused : State::Disabled);
    }
}

void RequestCoordinator::beginResolve() {
    if (m_state != State::WaitingStability)
        return;

    const core::Generation generation = m_generation.current();
    const quint64 seq = m_pendingEvent.seq;
    const settings::Settings &current = m_settings->current();

    selection::ResolveLimits limits;
    limits.maxChars = current.selection.maxSelectionChars;
    limits.timeoutMs = current.selection.resolveTimeoutMs;

    setState(State::Resolving);
    m_backend->resolveLatest(seq, limits,
        [this, generation, seq](
            quint64 resolvedSeq, core::Result<selection::SelectionText> result) {
            if (!m_generation.isCurrent(generation) || resolvedSeq != seq)
                return;  // superseded: drop silently
            if (!result.ok()) {
                handleAcquisitionError(result.error());
                return;
            }
            selection::SelectionText value = result.take();
            if (value.text.trimmed().isEmpty()) {
                handleAcquisitionError(core::Error {
                    core::ErrorCode::SelectionEmpty, QStringLiteral("empty selection")});
                return;
            }
            beginTranslate(
                value.text, Origin::AutomaticSelection, value.anchorLogical.value_or(QRect()));
        });
}

void RequestCoordinator::handleAcquisitionError(const core::Error &error) {
    core::log::writeError("coordinator", error);

    switch (error.code) {
    case core::ErrorCode::SelectionTooLarge:
        clearRetained();
        m_origin = Origin::AutomaticSelection;
        emit presentationRequested(true, QRect());
        m_view.lastError = error;
        publishView(State::ShowingError);
        setState(State::ShowingError);
        return;
    case core::ErrorCode::SelectionBackendAbsent:
    case core::ErrorCode::SelectionBackendDenied:
    case core::ErrorCode::SelectionBackendFailed:
        emit notice(error);
        disableAutomatic();
        return;
    default:
        // Silent outcomes (empty, protected, timeout, cancelled) leave the window untouched.
        setState(automaticEnabled() ? m_beforeSelection : State::Disabled);
        return;
    }
}

void RequestCoordinator::beginTranslate(
    const QString &text, Origin origin, const QRect &anchor, bool present) {
    m_text = text;
    m_hasText = true;
    m_origin = origin;
    m_streamed.clear();
    m_view.lastError = core::Error {};

    const settings::Settings &current = m_settings->current();
    const settings::ProviderConfig *active = current.activeProvider();

    if (origin == Origin::AutomaticSelection && !current.mayAutoSend()) {
        const bool stale = active != nullptr && current.consent.valid();
        if (!m_consentNotified) {
            m_consentNotified = true;
            emit consentRequired(active ? active->destinationLabel() : QString(), stale);
        }
        clearRetained();
        setState(automaticEnabled() ? State::Idle : State::Disabled);
        return;
    }

    if (present)
        emit presentationRequested(origin == Origin::AutomaticSelection, anchor);

    if (current.language.target.isEmpty()) {
        // Sending an empty target would ask the engine to translate into itself or into "auto".
        m_view.lastError = core::Error {
            core::ErrorCode::SettingsInvalid, QStringLiteral("no target language is chosen")};
        m_view.canRetry = false;
        publishView(State::ShowingError);
        return;
    }

    provider::TranslateRequest request;
    request.generation = m_generation.current();
    request.text = m_text;
    request.sourceLanguage = current.language.source;
    request.targetLanguage = current.language.target;
    request.streaming = m_provider->capabilities().streaming;

    m_view.providerLabel = active ? active->destinationLabel() : QString();
    m_view.origin = origin;
    setState(State::Translating);
    publishView(State::Translating);
    m_provider->translate(request, *this);
}

// ---------------------------------------------------------------- provider sink

void RequestCoordinator::onDelta(quint64 generation, QStringView delta) {
    if (!m_generation.isCurrent(generation))
        return;
    m_streamed.append(delta);
    publishView(State::Translating);
}

void RequestCoordinator::onFinished(quint64 generation) {
    if (!m_generation.isCurrent(generation))
        return;
    publishView(State::ShowingResult);
    setState(State::ShowingResult);
}

void RequestCoordinator::onFailed(quint64 generation, core::Error error) {
    if (!m_generation.isCurrent(generation))
        return;
    if (core::isSilent(error.code)) {
        core::log::writeError("coordinator", error);
        setState(automaticEnabled() ? State::Idle : State::Disabled);
        return;
    }
    m_view.lastError = error;
    publishView(State::ShowingError);
    setState(State::ShowingError);
}

// ---------------------------------------------------------------- settings

void RequestCoordinator::onSettingsChanged(const settings::Settings &current) {
    if (m_updatingSettings)
        return;

    if (!current.selection.automaticEnabled) {
        abortInFlight();
        if (m_observed) {
            if (m_backend != nullptr)
                m_backend->unsubscribe();
            m_observed = false;
        }
        setState(State::Disabled);
        return;
    }
    if (current.selection.paused) {
        abortInFlight();
        if (m_observed) {
            if (m_backend != nullptr)
                m_backend->unsubscribe();
            m_observed = false;
        }
        setState(State::Paused);
        return;
    }
    const settings::ProviderConfig *active = current.activeProvider();
    // Only meaningful while automatic observation runs; with the mode off there is nothing to
    // authorise.
    if (m_observed && active != nullptr && !active->isLocal() && !current.mayAutoSend()) {
        // Consent was revoked or the destination changed while running: stop sending and ask again.
        m_consentNotified = false;
        recheckConsent();
    }
}

// ---------------------------------------------------------------- helpers

void RequestCoordinator::abortInFlight() {
    const core::Generation current = m_generation.current();
    if (current != 0)
        m_provider->cancel(current);
    if (m_backend != nullptr)
        m_backend->cancelPending();
    m_generation.invalidate();
    m_streamed.clear();
}

void RequestCoordinator::clearRetained() {
    m_text.clear();
    m_hasText = false;
    m_streamed.clear();
}

void RequestCoordinator::publishView(State state) {
    m_view.viewId = ++m_viewId;
    m_view.state = state;
    m_view.translatedText = m_streamed;
    m_view.originalText = m_hasText ? m_text : QString();
    m_view.streaming = state == State::Translating;
    m_view.canRetry = m_hasText && state == State::ShowingError;
    m_view.origin = m_origin;
    emit viewChanged(m_view);
}

void RequestCoordinator::setState(State state) {
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

}  // namespace coordinator
