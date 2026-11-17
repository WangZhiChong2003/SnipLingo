// Request coordination: the only place that decides when work happens
// (docs/design/05-coordinator.md — the state table there is the specification for this class).
#pragma once

#include "core/delay.h"
#include "core/generation.h"
#include "core/result.h"

#include <QObject>
#include <QRect>

#include "provider/provider.h"
#include "selection/selection_backend.h"
#include "settings/settings_store.h"

namespace coordinator {

enum class State {
    Disabled,
    Paused,
    Idle,
    WaitingStability,
    Resolving,
    Translating,
    ShowingResult,
    ShowingError
};

enum class Origin { AutomaticSelection, ExplicitPaste, ExplicitClipboard };

struct ResultView {
    quint64 viewId = 0;
    State state = State::Idle;
    QString translatedText;
    QString originalText;
    QString providerLabel;
    bool streaming = false;
    bool originalCollapsed = true;
    bool canRetry = false;
    core::Error lastError;
    Origin origin = Origin::AutomaticSelection;
};

class RequestCoordinator : public QObject, private provider::Sink {
    Q_OBJECT
public:
    RequestCoordinator(selection::SelectionBackend *backend, provider::Provider *provider,
        settings::SettingsStore *settings, QObject *parent = nullptr);
    ~RequestCoordinator() override;

    [[nodiscard]] State state() const { return m_state; }

    [[nodiscard]] bool automaticEnabled() const;

    [[nodiscard]] bool observationActive() const { return m_observed; }

    [[nodiscard]] QString selectionBackendId() const {
        return m_backend ? m_backend->backendId() : QString();
    }

    [[nodiscard]] const ResultView &view() const { return m_view; }

    core::Result<void> enableAutomatic();
    void disableAutomatic();
    core::Result<void> setPaused(bool paused);
    core::Result<void> submitExplicitText(const QString &text, Origin origin);
    void dismissResult();
    void dismissOnFocusLoss();
    void retryLastRequest();
    core::Result<void> recheckConsent();

signals:
    void stateChanged(coordinator::State state);
    void viewChanged(const coordinator::ResultView &view);
    void consentRequired(const QString &destinationLabel, bool staleConsent);
    void notice(const core::Error &error);
    // Prepare the result window for a new request; an empty rectangle means no usable text anchor.
    void presentationRequested(bool automatic, const QRect &anchor);

private:
    // provider::Sink
    void onDelta(quint64 generation, QStringView delta) override;
    void onFinished(quint64 generation) override;
    void onFailed(quint64 generation, core::Error error) override;

    void onSelectionEvent(const selection::SelectionEvent &event);
    void onBackendStateChanged(selection::BackendState state, const QString &reason);
    void onSettingsChanged(const settings::Settings &settings);

    void beginResolve();
    void beginTranslate(
        const QString &text, Origin origin, const QRect &anchor = QRect(), bool present = true);
    void handleAcquisitionError(const core::Error &error);
    void publishView(State state);
    void setState(State state);
    void clearRetained();
    void abortInFlight();

    selection::SelectionBackend *m_backend = nullptr;
    provider::Provider *m_provider = nullptr;
    settings::SettingsStore *m_settings = nullptr;

    core::GenerationSource m_generation;
    core::CancellableDelay m_stability;

    State m_state = State::Disabled;
    State m_beforeSelection = State::Idle;
    bool m_observed = false;
    quint64 m_lastSeenSeq = 0;
    quint64 m_viewId = 0;

    selection::SelectionEvent m_pendingEvent;

    QString m_text;  // retained only while its result is on screen (retry / copy)
    Origin m_origin = Origin::AutomaticSelection;
    bool m_hasText = false;
    QString m_streamed;

    ResultView m_view;
    bool m_consentNotified = false;
    bool m_updatingSettings = false;  // guards re-entrancy from SettingsStore::changed
};

}  // namespace coordinator

Q_DECLARE_METATYPE(coordinator::State)
Q_DECLARE_METATYPE(coordinator::ResultView)
