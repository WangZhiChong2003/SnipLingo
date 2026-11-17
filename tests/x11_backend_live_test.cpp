// Run against a dedicated X server supplied by the test harness, never the user's desktop.
#include "platform/x11/x11_backend.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QThread>
#include <QtTest>

#include <cstdlib>
#include <cstring>
#include <xcb/xcb.h>

class X11BackendLiveTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void detectsAndResolvesAfterResume();
    void ignoresSelectionOwnedByThisProcess();
    void newerSelectionCancelsAStalledRead();
    void readsLargeSelectionAndEnforcesLimit();
};

void X11BackendLiveTest::initTestCase() {
    const QByteArray display = qgetenv("SNIPLYNGO_X11_TEST_DISPLAY");
    if (display.isEmpty())
        QSKIP("set SNIPLYNGO_X11_TEST_DISPLAY to an isolated X server");
    if (QStandardPaths::findExecutable(QStringLiteral("xclip")).isEmpty())
        QSKIP("xclip is required for this integration test");
    qputenv("DISPLAY", display);
}

void X11BackendLiveTest::detectsAndResolvesAfterResume() {
    X11Backend backend;
    QSignalSpy events(&backend, &selection::SelectionBackend::eventObserved);
    const QByteArray sample = QByteArrayLiteral("controlled selection test");
    quint64 previousSeq = 0;

    for (int cycle = 0; cycle < 2; ++cycle) {
        const auto subscribed = backend.subscribe();
        QVERIFY2(subscribed.ok(), subscribed.ok() ? "" : qPrintable(subscribed.error().detail));
        QProcess owner;
        owner.start(QStringLiteral("xclip"),
            {QStringLiteral("-selection"), QStringLiteral("primary"), QStringLiteral("-quiet")});
        QVERIFY(owner.waitForStarted(3000));
        QCOMPARE(owner.write(sample), qint64(sample.size()));
        owner.closeWriteChannel();

        QTRY_VERIFY_WITH_TIMEOUT(events.count() > cycle, 5000);
        const auto event = events.last().at(0).value<selection::SelectionEvent>();
        QVERIFY(event.seq > previousSeq);
        previousSeq = event.seq;
        bool finished = false;
        bool correctThread = false;
        QString text;
        selection::ResolveLimits limits;
        backend.resolveLatest(
            event.seq, limits, [&](quint64, core::Result<selection::SelectionText> result) {
                finished = true;
                correctThread = QThread::currentThread() == QCoreApplication::instance()->thread();
                if (result.ok())
                    text = result.value().text;
            });
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
        QVERIFY(correctThread);
        QCOMPARE(text, QString::fromUtf8(sample));
        backend.unsubscribe();
        owner.kill();
        owner.waitForFinished(3000);
    }
}

void X11BackendLiveTest::ignoresSelectionOwnedByThisProcess() {
    X11Backend backend;
    QSignalSpy events(&backend, &selection::SelectionBackend::eventObserved);
    QVERIFY(backend.subscribe().ok());

    int screenNumber = 0;
    xcb_connection_t *connection = xcb_connect(nullptr, &screenNumber);
    QVERIFY(connection != nullptr && !xcb_connection_has_error(connection));
    xcb_screen_iterator_t screen = xcb_setup_roots_iterator(xcb_get_setup(connection));
    for (int i = 0; i < screenNumber; ++i)
        xcb_screen_next(&screen);
    const xcb_window_t window = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen.data->root, 0, 0, 10, 10, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, screen.data->root_visual, 0, nullptr);
    constexpr char pidName[] = "_NET_WM_PID";
    xcb_intern_atom_reply_t *pidAtom = xcb_intern_atom_reply(connection,
        xcb_intern_atom(connection, 0, uint16_t(std::strlen(pidName)), pidName), nullptr);
    QVERIFY(pidAtom != nullptr);
    const uint32_t pid = uint32_t(QCoreApplication::applicationPid());
    xcb_change_property(
        connection, XCB_PROP_MODE_REPLACE, window, pidAtom->atom, XCB_ATOM_CARDINAL, 32, 1, &pid);
    std::free(pidAtom);
    xcb_set_selection_owner(connection, window, XCB_ATOM_PRIMARY, XCB_CURRENT_TIME);
    xcb_flush(connection);
    QTest::qWait(300);
    QCOMPARE(events.count(), 0);

    QProcess externalOwner;
    externalOwner.start(QStringLiteral("xclip"),
        {QStringLiteral("-selection"), QStringLiteral("primary"), QStringLiteral("-quiet")});
    QVERIFY(externalOwner.waitForStarted(3000));
    externalOwner.write("external selection");
    externalOwner.closeWriteChannel();
    QTRY_VERIFY_WITH_TIMEOUT(events.count() > 0, 5000);
    externalOwner.kill();
    externalOwner.waitForFinished(3000);
    xcb_destroy_window(connection, window);
    xcb_disconnect(connection);
    backend.unsubscribe();
}

void X11BackendLiveTest::newerSelectionCancelsAStalledRead() {
    X11Backend backend;
    QSignalSpy events(&backend, &selection::SelectionBackend::eventObserved);
    QVERIFY(backend.subscribe().ok());

    int screenNumber = 0;
    xcb_connection_t *connection = xcb_connect(nullptr, &screenNumber);
    QVERIFY(connection != nullptr && !xcb_connection_has_error(connection));
    xcb_screen_iterator_t screen = xcb_setup_roots_iterator(xcb_get_setup(connection));
    for (int i = 0; i < screenNumber; ++i)
        xcb_screen_next(&screen);
    const xcb_window_t window = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen.data->root, 0, 0, 10, 10, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, screen.data->root_visual, 0, nullptr);
    xcb_set_selection_owner(connection, window, XCB_ATOM_PRIMARY, XCB_CURRENT_TIME);
    xcb_flush(connection);
    QTRY_COMPARE_WITH_TIMEOUT(events.count(), 1, 5000);
    const auto first = events.first().at(0).value<selection::SelectionEvent>();

    bool finished = false;
    core::ErrorCode outcome = core::ErrorCode::Internal;
    backend.resolveLatest(first.seq, selection::ResolveLimits {},
        [&](quint64, core::Result<selection::SelectionText> result) {
            finished = true;
            if (!result.ok())
                outcome = result.error().code;
        });
    // Wait for the conversion request, then deliberately leave it unanswered while another
    // application takes ownership. This exercises event delivery during a slow conversion.
    auto requestArrived = [connection](xcb_selection_request_event_t &request) {
        if (request.requestor != XCB_WINDOW_NONE)
            return true;
        while (xcb_generic_event_t *event = xcb_poll_for_event(connection)) {
            const bool isRequest = (event->response_type & ~0x80) == XCB_SELECTION_REQUEST;
            if (isRequest)
                request = *reinterpret_cast<xcb_selection_request_event_t *>(event);
            std::free(event);
            if (isRequest)
                return true;
        }
        return false;
    };
    xcb_selection_request_event_t firstRequest {};
    QTRY_VERIFY_WITH_TIMEOUT(requestArrived(firstRequest), 1000);

    const xcb_window_t secondWindow = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, secondWindow, screen.data->root, 0, 0, 10,
        10, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen.data->root_visual, 0, nullptr);
    xcb_set_selection_owner(connection, secondWindow, XCB_ATOM_PRIMARY, XCB_CURRENT_TIME);
    xcb_flush(connection);
    QTRY_VERIFY_WITH_TIMEOUT(events.count() >= 2, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(outcome, core::ErrorCode::SelectionCancelled);

    const auto latest = events.last().at(0).value<selection::SelectionEvent>();
    bool latestFinished = false;
    QString text;
    backend.resolveLatest(latest.seq, selection::ResolveLimits {},
        [&](quint64, core::Result<selection::SelectionText> result) {
            latestFinished = true;
            if (result.ok())
                text = result.value().text;
        });
    xcb_selection_request_event_t secondRequest {};
    QTRY_VERIFY_WITH_TIMEOUT(requestArrived(secondRequest), 1000);
    auto respond = [connection](const xcb_selection_request_event_t &request, const char *value) {
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, request.requestor, request.property,
            request.target, 8, uint32_t(std::strlen(value)), value);
        xcb_selection_notify_event_t notify {};
        notify.response_type = XCB_SELECTION_NOTIFY;
        notify.time = request.time;
        notify.requestor = request.requestor;
        notify.selection = request.selection;
        notify.target = request.target;
        notify.property = request.property;
        xcb_send_event(connection, 0, request.requestor, XCB_EVENT_MASK_NO_EVENT,
            reinterpret_cast<const char *>(&notify));
        xcb_flush(connection);
    };
    respond(firstRequest, "old selection");
    QTest::qWait(150);
    QVERIFY(!latestFinished);
    respond(secondRequest, "new selection");
    QTRY_VERIFY_WITH_TIMEOUT(latestFinished, 5000);
    QCOMPARE(text, QStringLiteral("new selection"));

    // A non-responsive owner must respect a shorter caller-supplied deadline.
    xcb_set_selection_owner(connection, window, XCB_ATOM_PRIMARY, XCB_CURRENT_TIME);
    xcb_flush(connection);
    QTRY_VERIFY_WITH_TIMEOUT(events.count() >= 3, 1000);
    const auto stalled = events.last().at(0).value<selection::SelectionEvent>();
    bool timedOut = false;
    core::ErrorCode timeoutOutcome = core::ErrorCode::Internal;
    selection::ResolveLimits shortLimits;
    shortLimits.timeoutMs = 200;
    QElapsedTimer timer;
    timer.start();
    backend.resolveLatest(
        stalled.seq, shortLimits, [&](quint64, core::Result<selection::SelectionText> result) {
            timedOut = true;
            if (!result.ok())
                timeoutOutcome = result.error().code;
        });
    QTRY_VERIFY_WITH_TIMEOUT(timedOut, 1000);
    QCOMPARE(timeoutOutcome, core::ErrorCode::SelectionTimeout);
    QVERIFY(timer.elapsed() < 1000);
    xcb_destroy_window(connection, secondWindow);
    xcb_destroy_window(connection, window);
    xcb_disconnect(connection);
    backend.unsubscribe();
}

void X11BackendLiveTest::readsLargeSelectionAndEnforcesLimit() {
    X11Backend backend;
    QSignalSpy events(&backend, &selection::SelectionBackend::eventObserved);
    QVERIFY(backend.subscribe().ok());
    const QByteArray sample(300000, 'x');
    QProcess owner;
    owner.start(QStringLiteral("xclip"),
        {QStringLiteral("-selection"), QStringLiteral("primary"), QStringLiteral("-quiet")});
    QVERIFY(owner.waitForStarted(3000));
    QCOMPARE(owner.write(sample), qint64(sample.size()));
    owner.closeWriteChannel();
    QTRY_VERIFY_WITH_TIMEOUT(!events.isEmpty(), 5000);
    const auto selection = events.last().at(0).value<selection::SelectionEvent>();
    bool finished = false;
    QString text;
    selection::ResolveLimits limits;
    limits.maxChars = 300001;
    backend.resolveLatest(
        selection.seq, limits, [&](quint64, core::Result<selection::SelectionText> result) {
            finished = true;
            if (result.ok())
                text = result.value().text;
        });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QCOMPARE(text.size(), sample.size());
    QCOMPARE(text, QString::fromUtf8(sample));

    finished = false;
    core::ErrorCode outcome = core::ErrorCode::Internal;
    limits.maxChars = 200;
    backend.resolveLatest(
        selection.seq, limits, [&](quint64, core::Result<selection::SelectionText> result) {
            finished = true;
            if (!result.ok())
                outcome = result.error().code;
        });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QCOMPARE(outcome, core::ErrorCode::SelectionTooLarge);
    owner.kill();
    owner.waitForFinished(3000);
    backend.unsubscribe();
}

QTEST_GUILESS_MAIN(X11BackendLiveTest)
#include "x11_backend_live_test.moc"
