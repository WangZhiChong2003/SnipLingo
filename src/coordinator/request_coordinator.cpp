#include "coordinator/request_coordinator.h"

#include "core/log.h"

namespace coordinator {

namespace {
constexpr int kSelectionTestMs = 10000;
}

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
        connect(m_backend, &selection::SelectionBackend::anchorRefined, this,
            &RequestCoordinator::onRefinedAnchor);
        connect(m_backend, &selection::SelectionBackend::stateChanged, this,
            &RequestCoordinator::onBackendStateChanged);
    }
    connect(m_settings, &settings::SettingsStore::changed, this,
        &RequestCoordinator::onSettingsChanged);
}

RequestCoordinator::~RequestCoordinator() {
    cancelSelectionTest();
    abortInFlight();
}

bool RequestCoordinator::automaticEnabled() const {
    return m_settings->current().selection.automaticEnabled;
}

core::Result<void> RequestCoordinator::enableAutomatic() {
    cancelSelectionTest();
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
    cancelSelectionTest();
    abortInFlight();
    m_stability.cancel();
    if (m_observed) {
        if (m_backend != nullptr)
            m_backend->unsubscribe();
        m_observed = false;
    }
    dismissResult();

    m_updatingSettings = true;
    m_settings->setAutomaticEnabled(false);
    m_updatingSettings = false;
    setState(State::Disabled);
}

core::Result<void> RequestCoordinator::setPaused(bool paused) {
    cancelSelectionTest();
    m_updatingSettings = true;
    const core::Result<void> saved = m_settings->setPaused(paused);
    m_updatingSettings = false;
    if (!saved.ok())
        return saved;

    if (paused) {
        if (m_observed) {
            if (m_backend != nullptr)
                m_backend->unsubscribe();
            m_observed = false;
        }
        dismissResult();
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

    cancelSelectionTest();
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

void RequestCoordinator::startSelectionTest() {
    if (m_selectionTest == SelectionTest::Waiting)
        return;
    if (m_backend == nullptr || (!m_observed && !m_backend->subscribe().ok())) {
        m_selectionTest = SelectionTest::Unavailable;
        emit selectionTestChanged();
        return;
    }

    // The test takes over the same acquisition pipeline. Its selection is discarded before the
    // provider boundary, even if automatic translation is already enabled and authorised.
    dismissResult();
    m_selectionTest = SelectionTest::Waiting;
    m_testDeadline.start(
        kSelectionTestMs, [this] { finishSelectionTest(SelectionTest::TimedOut); });
    emit selectionTestChanged();
}

void RequestCoordinator::cancelSelectionTest() {
    if (m_selectionTest == SelectionTest::Waiting)
        finishSelectionTest(SelectionTest::Idle);
}

void RequestCoordinator::finishSelectionTest(SelectionTest outcome) {
    m_testDeadline.cancel();
    m_selectionTest = outcome;
    if (!m_observed)
        m_backend->unsubscribe();
    dismissResult();
    emit selectionTestChanged();
}

void RequestCoordinator::dismissOnFocusLoss() {
    if (m_presentedForPendingEvent
        || (m_state != State::WaitingStability && m_state != State::Resolving)) {
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
    if (active->isLocal() || current.hasConsentFor(active->fingerprint())) {
        m_consentNotified = false;
        return {};
    }
    if (!m_consentNotified) {
        m_consentNotified = true;
        emit consentRequired(active->destinationLabel(), !current.consents.isEmpty());
    }
    return {};
}

// ---------------------------------------------------------------- selection events

void RequestCoordinator::onSelectionEvent(const selection::SelectionEvent &event) {
    // An event already queued by a backend can arrive after pause or disable, including while an
    // explicit translation is using the state machine.
    if (m_selectionTest != SelectionTest::Waiting
        && (!m_observed || !automaticEnabled() || m_state == State::Disabled
            || m_state == State::Paused))
        return;
    if (event.seq == 0 || event.seq <= m_lastSeenSeq)
        return;

    if (core::log::isEnabled(core::log::Level::Debug)) {
        core::log::write(core::log::Level::Debug, "coordinator",
            QStringLiteral("selection observed seq=%1 backend=%2")
                .arg(event.seq)
                .arg(event.source.backendId));
    }

    if (m_state != State::WaitingStability && m_state != State::Resolving) {
        m_beforeSelection = m_state == State::ShowingResult || m_state == State::ShowingError
            ? m_state
            : State::Idle;
    }
    m_lastSeenSeq = event.seq;
    m_pendingEvent = event;
    m_presentedForPendingEvent = false;
    m_presentationAnchor = QRect();

    // A newer selection supersedes whatever was in flight, and its completion must not reach the
    // window.
    abortInFlight();
    m_generation.next();

    setState(State::WaitingStability);
    const int delayMs = m_settings->current().selection.stabilityDelayMs;
    m_stability.start(delayMs, [this] { beginResolve(); });
}

void RequestCoordinator::onRefinedAnchor(quint64 seq, const QRect &anchor) {
    if (m_selectionTest == SelectionTest::Waiting || m_state != State::Resolving
        || seq != m_pendingEvent.seq || !anchor.isValid())
        return;
    if (!m_settings->current().mayAutoSend())
        return;

    m_pendingEvent.anchorLogical = anchor;
    if (!m_presentedForPendingEvent) {
        m_presentedForPendingEvent = true;
        m_presentationAnchor = anchor;
        emit presentationRequested(Origin::AutomaticSelection, anchor);
        return;
    }
    if (m_presentationAnchor == anchor)
        return;
    m_presentationAnchor = anchor;
    emit presentationRequested(Origin::AutomaticSelection, anchor);
}

void RequestCoordinator::onBackendStateChanged(
    selection::BackendState state, const QString &reason) {
    if (state == selection::BackendState::Running)
        return;
    if (state == selection::BackendState::Denied || state == selection::BackendState::Failed) {
        if (m_selectionTest == SelectionTest::Waiting) {
            finishSelectionTest(SelectionTest::Unavailable);
            if (!m_observed)
                return;
        }
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
        dismissResult();
    }
}

void RequestCoordinator::beginResolve() {
    if (m_state != State::WaitingStability)
        return;

    const core::Generation generation = m_generation.current();
    const quint64 seq = m_pendingEvent.seq;
    const settings::Settings &current = m_settings->current();

    if (core::log::isEnabled(core::log::Level::Debug)) {
        core::log::write(core::log::Level::Debug, "coordinator",
            QStringLiteral("selection read started seq=%1").arg(seq));
    }

    selection::ResolveLimits limits;
    limits.maxChars = current.selection.maxSelectionChars;
    limits.timeoutMs = current.selection.resolveTimeoutMs;

    // The pointer anchor is already known. Put the affordance on screen now instead of waiting for
    // the selection owner to return the text; beginTranslate() reuses this presentation.
    if (m_selectionTest != SelectionTest::Waiting && current.mayAutoSend()
        && m_pendingEvent.anchorLogical.has_value()) {
        m_presentedForPendingEvent = true;
        m_presentationAnchor = *m_pendingEvent.anchorLogical;
        emit presentationRequested(Origin::AutomaticSelection, m_presentationAnchor);
    }

    setState(State::Resolving);
    m_backend->resolveLatest(seq, limits,
        [this, generation, seq](
            quint64 resolvedSeq, core::Result<selection::SelectionText> result) {
            if (!m_generation.isCurrent(generation) || resolvedSeq != seq)
                return;  // superseded: drop silently
            if (core::log::isEnabled(core::log::Level::Debug)) {
                core::log::write(core::log::Level::Debug, "coordinator",
                    QStringLiteral("selection read finished seq=%1 ok=%2")
                        .arg(seq)
                        .arg(result.ok()));
            }
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
            if (m_selectionTest == SelectionTest::Waiting) {
                finishSelectionTest(SelectionTest::Readable);
                return;
            }
            // X11 refreshes the pointer anchor when it resolves the text, because some owners
            // claim PRIMARY at the start of a drag. Other backends may provide the selected text's
            // geometry instead.
            const QRect anchor = m_pendingEvent.source.backendId == QLatin1String("x11-primary")
                    && value.anchorLogical.has_value()
                ? *value.anchorLogical
                : m_pendingEvent.anchorLogical.value_or(value.anchorLogical.value_or(QRect()));
            beginTranslate(value.text, Origin::AutomaticSelection, anchor);
        });
}

void RequestCoordinator::handleAcquisitionError(const core::Error &error) {
    core::log::writeError("coordinator", error);
    const bool alreadyPresented = m_presentedForPendingEvent;
    m_presentedForPendingEvent = false;
    m_presentationAnchor = QRect();

    if (m_selectionTest == SelectionTest::Waiting) {
        if (error.code == core::ErrorCode::SelectionBackendAbsent
            || error.code == core::ErrorCode::SelectionBackendDenied
            || error.code == core::ErrorCode::SelectionBackendFailed)
            onBackendStateChanged(selection::BackendState::Failed, error.detail);
        return;  // A caret movement or unreadable selection can be followed by a readable one.
    }

    switch (error.code) {
    case core::ErrorCode::SelectionTooLarge:
        clearRetained();
        m_origin = Origin::AutomaticSelection;
        m_view.providerLabel.clear();
        m_view.sourceLanguage.clear();
        m_view.targetLanguage.clear();
        if (!alreadyPresented)
            emit presentationRequested(
                Origin::AutomaticSelection, m_pendingEvent.anchorLogical.value_or(QRect()));
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
        const bool stale = active != nullptr && !current.consents.isEmpty();
        if (!m_consentNotified) {
            m_consentNotified = true;
            emit consentRequired(active ? active->destinationLabel() : QString(), stale);
        }
        clearRetained();
        m_presentedForPendingEvent = false;
        m_presentationAnchor = QRect();
        setState(automaticEnabled() ? State::Idle : State::Disabled);
        return;
    }

    m_view.sourceLanguage = origin == Origin::EngineTest
        ? (current.language.target == QLatin1String("en") ? QStringLiteral("zh-Hans")
                                                          : QStringLiteral("en"))
        : current.language.source;
    m_view.targetLanguage = current.language.target;
    m_view.providerLabel = active ? active->destinationLabel() : QString();
    const bool alreadyPresented
        = origin == Origin::AutomaticSelection && m_presentedForPendingEvent;
    const bool anchorChanged = alreadyPresented && m_presentationAnchor != anchor;
    if (present && (!alreadyPresented || anchorChanged))
        emit presentationRequested(origin, anchor);
    m_presentedForPendingEvent = false;
    m_presentationAnchor = QRect();

    if (current.language.target.isEmpty()) {
        // Sending an empty target would ask the engine to translate into itself or into "auto".
        m_view.lastError = core::Error {
            core::ErrorCode::SettingsInvalid, QStringLiteral("no target language is chosen")};
        clearRetained();
        publishView(State::ShowingError);
        setState(State::ShowingError);
        return;
    }

    provider::TranslateRequest request;
    request.generation = m_generation.current();
    request.text = m_text;
    request.sourceLanguage = m_view.sourceLanguage;
    request.targetLanguage = m_view.targetLanguage;
    request.streaming = m_provider->capabilities().streaming;

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
    const settings::ProviderConfig *active = current.activeProvider();
    const QString destination
        = active != nullptr && !active->isLocal() ? active->fingerprint() : QString();
    const bool granted = current.hasConsentFor(destination);
    const bool consentContextChanged
        = destination != m_consentDestinationFingerprint || granted != m_destinationHasConsent;
    // Keep the comparison current even when observation is paused or a local test is running.
    m_consentDestinationFingerprint = destination;
    m_destinationHasConsent = granted;

    if (m_updatingSettings)
        return;

    // Unrelated preference saves must not interrupt a user-initiated local selection test.
    if (m_selectionTest == SelectionTest::Waiting)
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
    if (m_observed && consentContextChanged && !destination.isEmpty() && !granted) {
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
    m_presentedForPendingEvent = false;
    m_presentationAnchor = QRect();
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
