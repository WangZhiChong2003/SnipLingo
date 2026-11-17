// Coordinator state machine: the table in docs/design/05-coordinator.md §5, one test per behaviour
// that matters for correctness (staleness, consent, silence, retention).
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include "coordinator/request_coordinator.h"
#include "fake_provider.h"
#include "fake_selection_backend.h"

using coordinator::Origin;
using coordinator::RequestCoordinator;
using coordinator::State;

namespace {

settings::ProviderConfig localProvider() {
    settings::ProviderConfig provider;
    provider.id = QStringLiteral("ollama");
    provider.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
    provider.model = QStringLiteral("qwen2.5");
    return provider;
}

settings::ProviderConfig remoteProvider() {
    settings::ProviderConfig provider;
    provider.id = QStringLiteral("openai-main");
    provider.baseUrl = QStringLiteral("https://api.openai.com/v1");
    provider.model = QStringLiteral("gpt-4o-mini");
    provider.apiKeyRef = QStringLiteral("provider/openai-main/apiKey");
    return provider;
}

}  // namespace

class CoordinatorTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void enableWithoutProviderIsRefused();
    void automaticCycleProducesAResultView();
    void remoteDestinationRequiresConsent();
    void localDestinationNeedsNoConsent();
    void newerSelectionSupersedesTheOldRequest();
    void emptySelectionIsSilent();
    void oversizedSelectionBecomesAnActionableView();
    void pauseUnsubscribesAndResumeWaitsForANewSelection();
    void queuedSelectionWhilePausedDoesNotInterruptExplicitTranslation();
    void retryOnlyWhileTheTextIsRetained();
    void explicitPasteBypassesTheConsentGate();
    void explicitInputIsValidatedLocally();
    void dismissStopsTheRequestAndClearsTheView();

private:
    void configure(const settings::ProviderConfig &provider, int maxChars = 200);

    QTemporaryDir m_root;
    std::unique_ptr<settings::SettingsStore> m_store;
    std::unique_ptr<FakeSelectionBackend> m_backend;
    std::unique_ptr<FakeProvider> m_provider;
    std::unique_ptr<RequestCoordinator> m_coordinator;
};

void CoordinatorTest::init() {
    QVERIFY(m_root.isValid());
    qputenv("XDG_CONFIG_HOME", m_root.filePath(QStringLiteral("config")).toLocal8Bit());
    qputenv("XDG_DATA_HOME", m_root.filePath(QStringLiteral("data")).toLocal8Bit());
    qputenv("XDG_STATE_HOME", m_root.filePath(QStringLiteral("state")).toLocal8Bit());
    qputenv("XDG_CACHE_HOME", m_root.filePath(QStringLiteral("cache")).toLocal8Bit());
    qputenv("HOME", m_root.filePath(QStringLiteral("home")).toLocal8Bit());

    m_store = std::make_unique<settings::SettingsStore>(core::XdgPaths::resolve());
    m_store->load();
    m_backend = std::make_unique<FakeSelectionBackend>();
    m_provider = std::make_unique<FakeProvider>();
    m_coordinator
        = std::make_unique<RequestCoordinator>(m_backend.get(), m_provider.get(), m_store.get());
}

void CoordinatorTest::cleanup() {
    m_coordinator.reset();
    m_provider.reset();
    m_backend.reset();
    m_store.reset();
    for (const char *variable :
        {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME", "HOME"})
        qunsetenv(variable);
}

void CoordinatorTest::configure(const settings::ProviderConfig &provider, int maxChars) {
    settings::Settings next = m_store->current();
    next.provider.providers = {provider};
    next.provider.activeId = provider.id;
    next.language.target = QStringLiteral("zh-Hans");
    next.selection.stabilityDelayMs = 100;  // minimum accepted value keeps the tests fast
    next.selection.maxSelectionChars = maxChars;
    QVERIFY(m_store->save(next).ok());
}

void CoordinatorTest::enableWithoutProviderIsRefused() {
    const auto enabled = m_coordinator->enableAutomatic();
    QVERIFY(!enabled.ok());
    QCOMPARE(enabled.error().code, core::ErrorCode::ProviderNotConfigured);
    QCOMPARE(m_coordinator->state(), State::Disabled);
    QCOMPARE(m_backend->subscribeCalls, 0);
}

void CoordinatorTest::automaticCycleProducesAResultView() {
    configure(localProvider());
    QSignalSpy presentation(m_coordinator.get(), &RequestCoordinator::presentationRequested);
    QVERIFY(m_coordinator->enableAutomatic().ok());
    QCOMPARE(m_coordinator->state(), State::Idle);
    QCOMPARE(m_backend->subscribeCalls, 1);
    QVERIFY(m_coordinator->observationActive());

    m_backend->emitSelection(1);
    QCOMPARE(m_coordinator->state(), State::WaitingStability);

    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    QVERIFY(m_backend->hasPendingResolve());
    QCOMPARE(m_backend->latestLimits.maxChars, 200);  // schema minimum

    m_backend->completeResolve(1, QStringLiteral("hello world"));
    QCOMPARE(presentation.count(), 1);
    QVERIFY(presentation.first().at(0).toBool());
    QCOMPARE(m_coordinator->state(), State::Translating);
    QCOMPARE(m_provider->translateCalls, 1);
    QCOMPARE(m_provider->requests.first().text, QStringLiteral("hello world"));
    QCOMPARE(m_provider->requests.first().targetLanguage, QStringLiteral("zh-Hans"));

    m_provider->deliverDelta(QStringLiteral("你好"));
    m_provider->deliverDelta(QStringLiteral("世界"));
    QCOMPARE(m_coordinator->view().translatedText, QStringLiteral("你好世界"));
    QVERIFY(m_coordinator->view().streaming);

    m_provider->deliverFinished();
    QCOMPARE(m_coordinator->state(), State::ShowingResult);
    QVERIFY(!m_coordinator->view().streaming);
    QCOMPARE(m_coordinator->view().originalText, QStringLiteral("hello world"));
    QVERIFY(!m_coordinator->view().providerLabel.isEmpty());
}

void CoordinatorTest::remoteDestinationRequiresConsent() {
    configure(remoteProvider());
    QSignalSpy consentSpy(m_coordinator.get(), &RequestCoordinator::consentRequired);
    QSignalSpy presentation(m_coordinator.get(), &RequestCoordinator::presentationRequested);
    QVERIFY(m_coordinator->enableAutomatic().ok());

    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("hello"));

    QCOMPARE(m_provider->translateCalls, 0);
    QCOMPARE(presentation.count(), 0);
    QCOMPARE(consentSpy.count(), 1);
    QCOMPARE(m_coordinator->state(), State::Idle);
}

void CoordinatorTest::localDestinationNeedsNoConsent() {
    configure(localProvider());
    QSignalSpy consentSpy(m_coordinator.get(), &RequestCoordinator::consentRequired);
    QVERIFY(m_coordinator->enableAutomatic().ok());

    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("hello"));

    QCOMPARE(consentSpy.count(), 0);
    QCOMPARE(m_provider->translateCalls, 1);
}

void CoordinatorTest::newerSelectionSupersedesTheOldRequest() {
    configure(localProvider());
    QVERIFY(m_coordinator->enableAutomatic().ok());

    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);

    m_backend->emitSelection(2);
    QCOMPARE(m_coordinator->state(), State::WaitingStability);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);

    // The superseded resolution completes late: it must not reach the provider.
    m_backend->completeResolve(1, QStringLiteral("stale text"));
    QCOMPARE(m_provider->translateCalls, 0);

    m_backend->completeResolve(2, QStringLiteral("fresh text"));
    QCOMPARE(m_provider->translateCalls, 1);
    QCOMPARE(m_provider->requests.first().text, QStringLiteral("fresh text"));
}

void CoordinatorTest::emptySelectionIsSilent() {
    configure(localProvider());
    QVERIFY(m_coordinator->enableAutomatic().ok());
    QSignalSpy viewSpy(m_coordinator.get(), &RequestCoordinator::viewChanged);

    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QString());

    QCOMPARE(m_coordinator->state(), State::Idle);
    QCOMPARE(m_provider->translateCalls, 0);
    QCOMPARE(viewSpy.count(), 0);  // nothing to show, no error popup
}

void CoordinatorTest::oversizedSelectionBecomesAnActionableView() {
    configure(localProvider(), 200);  // schema minimum
    QVERIFY(m_coordinator->enableAutomatic().ok());

    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    // The backend enforces the character limit (docs/design/04-selection.md §6.3) and reports it as
    // SelectionTooLarge; the coordinator turns that into an actionable view.
    m_backend->completeResolveWithError(1, core::ErrorCode::SelectionTooLarge);

    QCOMPARE(m_coordinator->state(), State::ShowingError);
    QCOMPARE(m_coordinator->view().lastError.code, core::ErrorCode::SelectionTooLarge);
    QCOMPARE(m_provider->translateCalls, 0);
    QVERIFY2(
        !m_coordinator->view().canRetry, "a too-large selection must not be retried automatically");
}

void CoordinatorTest::pauseUnsubscribesAndResumeWaitsForANewSelection() {
    configure(localProvider());
    QVERIFY(m_coordinator->enableAutomatic().ok());
    QCOMPARE(m_backend->subscribeCalls, 1);

    QVERIFY(m_coordinator->setPaused(true).ok());
    QCOMPARE(m_coordinator->state(), State::Paused);
    QCOMPARE(m_backend->unsubscribeCalls, 1);
    QVERIFY(!m_coordinator->observationActive());

    m_backend->emitSelection(1);
    QTest::qWait(150);
    QCOMPARE(m_coordinator->state(), State::Paused);
    QCOMPARE(m_backend->resolveCalls, 0);

    QVERIFY(m_coordinator->setPaused(false).ok());
    QCOMPARE(m_coordinator->state(), State::Idle);
    QCOMPARE(m_backend->subscribeCalls, 2);
    QCOMPARE(m_provider->translateCalls, 0);  // resuming never replays anything
}

void CoordinatorTest::queuedSelectionWhilePausedDoesNotInterruptExplicitTranslation() {
    configure(localProvider());
    QVERIFY(m_coordinator->enableAutomatic().ok());
    QVERIFY(m_coordinator->setPaused(true).ok());
    QVERIFY(
        m_coordinator->submitExplicitText(QStringLiteral("manual"), Origin::ExplicitPaste).ok());
    QCOMPARE(m_coordinator->state(), State::Translating);

    // A backend callback can already be queued when pause unsubscribes it.
    m_backend->emitSelection(1);
    QTest::qWait(150);
    QCOMPARE(m_coordinator->state(), State::Translating);
    QCOMPARE(m_backend->resolveCalls, 0);
    QCOMPARE(m_provider->translateCalls, 1);
    m_provider->deliverFinished();
    QCOMPARE(m_coordinator->state(), State::ShowingResult);
}

void CoordinatorTest::retryOnlyWhileTheTextIsRetained() {
    configure(localProvider());
    QSignalSpy presentation(m_coordinator.get(), &RequestCoordinator::presentationRequested);
    QVERIFY(m_coordinator->enableAutomatic().ok());

    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("retry me"));
    QCOMPARE(m_provider->translateCalls, 1);

    m_provider->deliverError(core::ErrorCode::ProviderNetwork);
    QCOMPARE(m_coordinator->state(), State::ShowingError);
    QVERIFY(m_coordinator->view().canRetry);

    m_coordinator->retryLastRequest();
    QCOMPARE(presentation.count(), 1);  // retry keeps the already open presentation
    QCOMPARE(m_provider->translateCalls, 2);
    QCOMPARE(m_provider->requests.last().text, QStringLiteral("retry me"));

    m_provider->deliverError(core::ErrorCode::ProviderNetwork);
    m_coordinator->dismissResult();
    QCOMPARE(m_coordinator->state(), State::Idle);
    m_coordinator->retryLastRequest();
    QCOMPARE(m_provider->translateCalls, 2);  // nothing retained after dismissal
}

void CoordinatorTest::explicitPasteBypassesTheConsentGate() {
    configure(remoteProvider());
    QSignalSpy consentSpy(m_coordinator.get(), &RequestCoordinator::consentRequired);
    QSignalSpy presentation(m_coordinator.get(), &RequestCoordinator::presentationRequested);

    QVERIFY(
        m_coordinator->submitExplicitText(QStringLiteral("  pasted text  "), Origin::ExplicitPaste)
            .ok());
    QCOMPARE(m_coordinator->state(), State::Translating);
    QCOMPARE(m_provider->translateCalls, 1);
    QCOMPARE(m_provider->requests.first().text, QStringLiteral("pasted text"));  // trimmed
    QCOMPARE(consentSpy.count(), 0);  // an explicit action is its own consent for this text
    QCOMPARE(presentation.count(), 1);
    QVERIFY(!presentation.first().at(0).toBool());
    QCOMPARE(m_coordinator->view().origin, Origin::ExplicitPaste);
}

void CoordinatorTest::explicitInputIsValidatedLocally() {
    configure(localProvider(), 200);  // the schema clamps lower values up to 200

    const auto empty
        = m_coordinator->submitExplicitText(QStringLiteral("   "), Origin::ExplicitPaste);
    QVERIFY(!empty.ok());
    QCOMPARE(empty.error().code, core::ErrorCode::SelectionEmpty);

    const QString longText(201, QLatin1Char('x'));
    const auto tooLarge = m_coordinator->submitExplicitText(longText, Origin::ExplicitPaste);
    QVERIFY(!tooLarge.ok());
    QCOMPARE(tooLarge.error().code, core::ErrorCode::SelectionTooLarge);

    QCOMPARE(m_provider->translateCalls, 0);
}

void CoordinatorTest::dismissStopsTheRequestAndClearsTheView() {
    configure(localProvider());
    QVERIFY(m_coordinator->enableAutomatic().ok());

    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("text"));
    m_provider->deliverDelta(QStringLiteral("部分"));

    m_coordinator->dismissResult();
    QCOMPARE(m_coordinator->state(), State::Idle);
    QVERIFY(m_coordinator->view().translatedText.isEmpty());
    QVERIFY(m_coordinator->view().originalText.isEmpty());
    QVERIFY(m_provider->cancelCalls >= 1);

    // A completion arriving after dismissal must not resurrect the window.
    m_provider->deliverDelta(QStringLiteral("迟到"));
    m_provider->deliverFinished();
    QTest::qWait(20);
    QVERIFY(m_coordinator->view().translatedText.isEmpty());
    QCOMPARE(m_coordinator->state(), State::Idle);
}

QTEST_GUILESS_MAIN(CoordinatorTest)
#include "coordinator_test.moc"
