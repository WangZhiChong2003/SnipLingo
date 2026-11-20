// Result-window controller: state keys, plain text, clipboard only on request, delta coalescing.
#include "ui/result_controller.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

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

}  // namespace

class UiResultControllerTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void hiddenUntilSomethingHappens();
    void loadingAffordanceAppearsBeforeResolve();
    void loadingThenStreamingThenDone();
    void newSelectionKeepsTheExistingWindowVisible();
    void pauseClearsTheRetainedResult();
    void focusLossDoesNotCancelANewerSelection();
    void errorExposesAStableKeyAndRetry();
    void copyUsesTheClipboardOnlyWhenAsked();
    void originalCanBeToggled();
    void streamingDeltasAreCoalesced();

private:
    void configure();

    QTemporaryDir m_root;
    std::unique_ptr<settings::SettingsStore> m_store;
    std::unique_ptr<FakeSelectionBackend> m_backend;
    std::unique_ptr<FakeProvider> m_provider;
    std::unique_ptr<RequestCoordinator> m_coordinator;
    std::unique_ptr<ui::ResultController> m_controller;
};

void UiResultControllerTest::init() {
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
    m_controller = std::make_unique<ui::ResultController>(m_coordinator.get());
}

void UiResultControllerTest::cleanup() {
    m_controller.reset();
    m_coordinator.reset();
    m_provider.reset();
    m_backend.reset();
    m_store.reset();
    for (const char *variable :
        {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME", "HOME"})
        qunsetenv(variable);
}

void UiResultControllerTest::configure() {
    settings::Settings next = m_store->current();
    next.provider.providers = {localProvider()};
    next.provider.activeId = localProvider().id;
    next.language.target = QStringLiteral("zh-Hans");
    next.selection.stabilityDelayMs = 100;
    QVERIFY(m_store->save(next).ok());
}

void UiResultControllerTest::hiddenUntilSomethingHappens() {
    QVERIFY(!m_controller->visible());
    QCOMPARE(m_controller->stateKey(), QStringLiteral("hidden"));
    QVERIFY(m_controller->errorKey().isEmpty());
    QVERIFY(!m_controller->canRetry());
}

void UiResultControllerTest::loadingAffordanceAppearsBeforeResolve() {
    configure();
    QVERIFY(m_coordinator->enableAutomatic().ok());

    m_backend->emitSelection(1, QStringLiteral("fake-app"), QRect(20, 30, 1, 1));
    QCOMPARE(m_controller->stateKey(), QStringLiteral("loading"));
    QVERIFY(!m_controller->visible());

    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    QVERIFY(m_controller->visible());

    m_backend->completeResolve(1, QStringLiteral("hello"));
    QVERIFY(m_controller->visible());
}

void UiResultControllerTest::loadingThenStreamingThenDone() {
    configure();
    QVERIFY(m_coordinator->enableAutomatic().ok());

    m_backend->emitSelection(1);
    QCOMPARE(m_controller->stateKey(), QStringLiteral("loading"));
    QVERIFY(!m_controller->visible());

    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("hello"));
    QCOMPARE(m_controller->stateKey(), QStringLiteral("loading"));
    QVERIFY(m_controller->visible());

    m_provider->deliverDelta(QStringLiteral("你好"));
    QTRY_COMPARE(m_controller->translatedText(), QStringLiteral("你好"));
    QCOMPARE(m_controller->stateKey(), QStringLiteral("streaming"));
    QCOMPARE(m_controller->originalText(), QStringLiteral("hello"));
    QVERIFY(!m_controller->providerLabel().isEmpty());

    m_provider->deliverFinished();
    QCOMPARE(m_controller->stateKey(), QStringLiteral("done"));
    QVERIFY(!m_controller->canRetry());
}

void UiResultControllerTest::errorExposesAStableKeyAndRetry() {
    configure();
    QVERIFY(m_coordinator->enableAutomatic().ok());

    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("hello"));
    m_provider->deliverError(core::ErrorCode::ProviderRateLimited);

    QCOMPARE(m_controller->stateKey(), QStringLiteral("error"));
    QCOMPARE(m_controller->errorKey(),
        QString::fromLatin1(core::stableName(core::ErrorCode::ProviderRateLimited)));
    QVERIFY(m_controller->canRetry());
}

void UiResultControllerTest::newSelectionKeepsTheExistingWindowVisible() {
    configure();
    QVERIFY(m_coordinator->enableAutomatic().ok());
    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("hello"));
    m_provider->deliverDelta(QStringLiteral("你好"));
    m_provider->deliverFinished();
    QVERIFY(m_controller->visible());

    m_backend->emitSelection(2);
    QCOMPARE(m_controller->stateKey(), QStringLiteral("loading"));
    QVERIFY(m_controller->visible());
    QVERIFY(!m_controller->originalVisible());
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolveWithError(2, core::ErrorCode::SelectionEmpty);
    QCOMPARE(m_controller->stateKey(), QStringLiteral("done"));
    QCOMPARE(m_controller->translatedText(), QStringLiteral("你好"));
    QVERIFY(m_controller->visible());
}

void UiResultControllerTest::pauseClearsTheRetainedResult() {
    configure();
    QVERIFY(m_coordinator->enableAutomatic().ok());
    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("hello"));
    m_provider->deliverDelta(QStringLiteral("你好"));
    m_provider->deliverFinished();
    QVERIFY(m_controller->visible());
    QVERIFY(m_coordinator->setPaused(true).ok());
    QVERIFY(!m_controller->visible());
    QVERIFY(m_controller->translatedText().isEmpty());
    QVERIFY(m_controller->originalText().isEmpty());
}

void UiResultControllerTest::focusLossDoesNotCancelANewerSelection() {
    configure();
    QVERIFY(m_coordinator->enableAutomatic().ok());
    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("first"));
    m_provider->deliverFinished();
    QVERIFY(m_controller->visible());

    m_backend->emitSelection(2);
    m_controller->dismissOnFocusLoss();
    QVERIFY(!m_controller->visible());
    QCOMPARE(m_coordinator->state(), State::WaitingStability);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(2, QStringLiteral("second"));
    QVERIFY(m_controller->visible());
    QCOMPARE(m_provider->translateCalls, 2);
    QCOMPARE(m_provider->requests.last().text, QStringLiteral("second"));
}

void UiResultControllerTest::copyUsesTheClipboardOnlyWhenAsked() {
    configure();
    QVERIFY(m_coordinator->enableAutomatic().ok());
    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("hello"));
    m_provider->deliverDelta(QStringLiteral("你好"));
    m_provider->deliverFinished();

    QClipboard *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard != nullptr);
    const QString before = clipboard->text();
    QCOMPARE(before, QString());  // nothing is written without an explicit action

    m_controller->copyResult();
    QCOMPARE(clipboard->text(), QStringLiteral("你好"));
}

void UiResultControllerTest::originalCanBeToggled() {
    configure();
    QVERIFY(m_coordinator->enableAutomatic().ok());
    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("hello"));
    m_provider->deliverFinished();

    QVERIFY(m_controller->originalVisible());
    QVERIFY(m_controller->originalCollapsed());
    m_controller->toggleOriginal();
    QVERIFY(!m_controller->originalCollapsed());
    m_controller->toggleOriginal();
    QVERIFY(m_controller->originalCollapsed());
}

void UiResultControllerTest::streamingDeltasAreCoalesced() {
    configure();
    QVERIFY(m_coordinator->enableAutomatic().ok());
    m_backend->emitSelection(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_coordinator->state(), State::Resolving, 2000);
    m_backend->completeResolve(1, QStringLiteral("hello"));

    QSignalSpy changes(m_controller.get(), &ui::ResultController::changed);
    for (int i = 0; i < 40; ++i)
        m_provider->deliverDelta(QStringLiteral("x"));

    // 40 deltas must not produce 40 QML updates: the throttle coalesces them.
    QVERIFY2(changes.count() <= 3, qPrintable(QString::number(changes.count())));
    QCOMPARE(m_controller->translatedText(), QString(40, QLatin1Char('x')));
}

QTEST_MAIN(UiResultControllerTest)
#include "ui_result_controller_test.moc"
