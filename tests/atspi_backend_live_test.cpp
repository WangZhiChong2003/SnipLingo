// Live integration test for the AT-SPI backend.
//
// A private D-Bus daemon hosts a fake accessibility service that speaks the real wire protocol. The
// fake lives on its own thread with an event loop: the backend's calls are blocking, so a
// same-thread fake could never answer them — the first version of this test failed with "Did not
// receive a reply" for exactly that reason.
#include "platform/atspi/atspi_backend.h"

#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <memory>

#include "fake_atspi_service.h"
#include "test_bus.h"

namespace {

selection::ResolveLimits defaultLimits() {
    selection::ResolveLimits limits;
    limits.maxChars = 4000;
    limits.timeoutMs = 1500;
    return limits;
}

}  // namespace

class AtspiBackendLiveTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void deliversSelectionThenAppliesSizeLimit();
    void protectedRoleProducesNoEvent();
    void zeroLengthRangeResolvesAsEmpty();
    void eventsFromOurOwnApplicationAreIgnored();
    void pausedBackendForwardsNothing();

private:
    // Runs `fn` on the fake service's thread, where its event loop lives.
    template <typename Fn>
    void onFake(Fn fn) {
        const bool invoked = QMetaObject::invokeMethod(
            &m_fakeContext, std::move(fn), Qt::BlockingQueuedConnection);
        QVERIFY2(invoked, "could not reach the fake service thread");
    }

    bool startFake();
    void stopFake();

    PrivateTestBus m_bus;

    QThread m_fakeThread;
    QObject m_fakeContext;
    FakeAtspiService *m_service = nullptr;
    bool m_fakeReady = false;
    // One instance for the whole case: that is what the product does, and libatspi's event loop is
    // process-global, so a second instance could never receive events.
    AtspiBackend m_backend;
};

void AtspiBackendLiveTest::initTestCase() {
    qputenv("ATSPI_NO_CACHE", "1");  // the fake intentionally exposes no cache interface

    QVERIFY2(m_bus.start(), qPrintable(m_bus.error()));
    qputenv("DBUS_SESSION_BUS_ADDRESS", m_bus.address().toUtf8());

    QVERIFY2(startFake(), "the fake accessibility service did not start");
}

void AtspiBackendLiveTest::cleanupTestCase() {
    stopFake();
    m_bus.stop();
}

bool AtspiBackendLiveTest::startFake() {
    m_fakeContext.moveToThread(&m_fakeThread);
    m_fakeThread.start();

    const QString address = m_bus.address();
    onFake([this, address] {
        m_service = new FakeAtspiService(address);
        m_fakeReady = m_service->ready();
    });
    return m_fakeReady;
}

void AtspiBackendLiveTest::stopFake() {
    if (!m_fakeThread.isRunning())
        return;
    const bool invoked = QMetaObject::invokeMethod(
        &m_fakeContext,
        [this] {
            delete m_service;
            m_service = nullptr;
        },
        Qt::BlockingQueuedConnection);
    Q_UNUSED(invoked)
    m_fakeThread.quit();
    m_fakeThread.wait(3000);
}

void AtspiBackendLiveTest::deliversSelectionThenAppliesSizeLimit() {
    // Some applications publish the selection event before the final range becomes available.
    onFake([this] { m_service->setSelection(QStringLiteral("hello world"), 0, 0); });

    QSignalSpy events(&m_backend, &selection::SelectionBackend::eventObserved);
    const auto subscribed = m_backend.subscribe();
    QVERIFY2(subscribed.ok(), subscribed.ok() ? "" : qPrintable(subscribed.error().detail));

    int registrations = 0;
    onFake([this, &registrations] { registrations = m_service->registerEventCalls(); });
    QVERIFY2(registrations >= 1, "the backend never registered for the event");

    onFake([this] { QVERIFY(m_service->emitSelectionChanged(nullptr)); });
    QTRY_COMPARE_WITH_TIMEOUT(events.count(), 1, 5000);

    const auto first = events.at(0).at(0).value<selection::SelectionEvent>();
    QCOMPARE(first.source.appName, QStringLiteral("fake-editor"));
    QCOMPARE(first.source.backendId, QStringLiteral("atspi"));
    onFake([this] { m_service->setSelection(QStringLiteral("hello world"), 0, 11); });

    bool resolved = false;
    bool resolutionFinished = false;
    bool hasAnchor = false;
    bool callbackOnQtThread = false;
    QString text;
    QString resolveError;
    m_backend.resolveLatest(
        first.seq, defaultLimits(), [&](quint64, core::Result<selection::SelectionText> result) {
            callbackOnQtThread = QThread::currentThread() == qApp->thread();
            resolved = result.ok();
            resolutionFinished = true;
            if (result.ok()) {
                text = result.value().text;
                hasAnchor = result.value().anchorLogical.has_value();
            } else
                resolveError = result.error().detail;
        });
    QTRY_VERIFY_WITH_TIMEOUT(resolutionFinished, 3000);
    QVERIFY2(resolved, qPrintable(resolveError));
    QVERIFY(callbackOnQtThread);
    QCOMPARE(text, QStringLiteral("hello world"));
    QVERIFY2(hasAnchor, "range extents were not read");

    // The size limit is applied per request, so it can be checked on the same event: the backend
    // must refuse rather than truncate, and the earlier successful resolve must be unaffected.
    selection::ResolveLimits limits = defaultLimits();
    limits.maxChars = 5;
    bool limited = true;
    core::ErrorCode code = core::ErrorCode::Internal;
    m_backend.resolveLatest(
        first.seq, limits, [&](quint64, core::Result<selection::SelectionText> result) {
            limited = result.ok();
            if (!result.ok())
                code = result.error().code;
        });
    QTRY_VERIFY_WITH_TIMEOUT(!limited, 3000);
    QCOMPARE(code, core::ErrorCode::SelectionTooLarge);

    m_backend.unsubscribe();
    QVERIFY(m_backend.subscribe().ok());
    onFake([this] {
        m_service->setSelection(QStringLiteral("hello again"), 0, 11);
        QVERIFY(m_service->emitSelectionChanged(nullptr));
    });
    QTRY_COMPARE_WITH_TIMEOUT(events.count(), 2, 5000);
    const auto resumed = events.at(1).at(0).value<selection::SelectionEvent>();
    QVERIFY(resumed.seq > first.seq);
    m_backend.unsubscribe();
}

void AtspiBackendLiveTest::protectedRoleProducesNoEvent() {
    onFake([this] {
        m_service->setSelection(QStringLiteral("secret"), 0, 6, QStringLiteral("password text"));
    });

    QSignalSpy events(&m_backend, &selection::SelectionBackend::eventObserved);
    const auto subscribed = m_backend.subscribe();
    QVERIFY2(subscribed.ok(), subscribed.ok() ? "" : qPrintable(subscribed.error().detail));

    onFake([this] { m_service->emitSelectionChanged(nullptr); });
    QTest::qWait(700);
    QCOMPARE(events.count(), 0);

    m_backend.unsubscribe();
}

void AtspiBackendLiveTest::zeroLengthRangeResolvesAsEmpty() {
    onFake([this] { m_service->setSelection(QStringLiteral("hello world"), 5, 5); });  // caret

    QSignalSpy events(&m_backend, &selection::SelectionBackend::eventObserved);
    const auto subscribed = m_backend.subscribe();
    QVERIFY2(subscribed.ok(), subscribed.ok() ? "" : qPrintable(subscribed.error().detail));

    onFake([this] { m_service->emitSelectionChanged(nullptr); });
    QTRY_COMPARE_WITH_TIMEOUT(events.count(), 1, 5000);
    const auto event = events.at(0).at(0).value<selection::SelectionEvent>();
    bool finished = false;
    core::ErrorCode code = core::ErrorCode::Internal;
    m_backend.resolveLatest(
        event.seq, defaultLimits(), [&](quint64, core::Result<selection::SelectionText> result) {
            finished = true;
            if (!result.ok())
                code = result.error().code;
        });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 3000);
    QCOMPARE(code, core::ErrorCode::SelectionEmpty);

    m_backend.unsubscribe();
}

void AtspiBackendLiveTest::eventsFromOurOwnApplicationAreIgnored() {
    onFake([this] { m_service->setSelection(QStringLiteral("self text"), 0, 9); });

    m_backend.setOwnAppName(QStringLiteral("fake-editor"));
    QSignalSpy events(&m_backend, &selection::SelectionBackend::eventObserved);
    const auto subscribed = m_backend.subscribe();
    QVERIFY2(subscribed.ok(), subscribed.ok() ? "" : qPrintable(subscribed.error().detail));

    onFake([this] { m_service->emitSelectionChanged(nullptr); });
    QTest::qWait(700);
    QCOMPARE(events.count(), 0);

    m_backend.unsubscribe();
}

void AtspiBackendLiveTest::pausedBackendForwardsNothing() {
    // A paused backend must not forward events, including those already in flight.
    m_backend.setOwnAppName(QStringLiteral("resume-probe"));
    QVERIFY2(m_backend.subscribe().ok(), "subscribe failed");
    QSignalSpy events(&m_backend, &selection::SelectionBackend::eventObserved);
    auto deregistrations = [this] {
        int count = 0;
        onFake([this, &count] { count = m_service->deregisterEventCalls(); });
        return count;
    };
    const int beforePause = deregistrations();
    m_backend.unsubscribe();
    QTRY_VERIFY_WITH_TIMEOUT(deregistrations() > beforePause, 3000);

    onFake([this] { m_service->setSelection(QStringLiteral("while paused"), 0, 12); });
    onFake([this] { m_service->emitSelectionChanged(nullptr); });
    QTest::qWait(500);
    QCOMPARE(events.count(), 0);

    QVERIFY2(m_backend.subscribe().ok(), "subscribe after deregistration failed");
    onFake([this] { m_service->emitSelectionChanged(nullptr); });
    QTRY_COMPARE_WITH_TIMEOUT(events.count(), 1, 5000);
    m_backend.unsubscribe();
}

QTEST_GUILESS_MAIN(AtspiBackendLiveTest)
#include "atspi_backend_live_test.moc"
