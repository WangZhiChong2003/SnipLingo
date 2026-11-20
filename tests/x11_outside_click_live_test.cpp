// Exercise actual X11 button delivery on an isolated server.
#include "platform/x11/x11_outside_click.h"

#include <QGuiApplication>
#include <QSignalSpy>
#include <QWindow>
#include <QtTest>

#include <cstdlib>
#include <xcb/xtest.h>

namespace {

class PressWindow : public QWindow {
public:
    int presses = 0;

protected:
    void mousePressEvent(QMouseEvent *) override { ++presses; }
};

}  // namespace

class X11OutsideClickLiveTest : public QObject {
    Q_OBJECT
private slots:
    void outsidePressDismissesAndReachesItsTarget();
};

void X11OutsideClickLiveTest::outsidePressDismissesAndReachesItsTarget() {
    if (qEnvironmentVariableIsEmpty("SNIPLYNGO_X11_TEST_DISPLAY"))
        QSKIP("set SNIPLYNGO_X11_TEST_DISPLAY to an isolated X server");

    const std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> sender(
        xcb_connect(nullptr, nullptr), xcb_disconnect);
    QVERIFY(!xcb_connection_has_error(sender.get()));
    const auto *xtest = xcb_get_extension_data(sender.get(), &xcb_test_id);
    QVERIFY(xtest && xtest->present);
    const xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(sender.get())).data->root;

    // A desktop shortcut registered on the root prevents an AnyButton/AnyModifier grab.
    const auto grab
        = xcb_grab_button_checked(sender.get(), false, root, XCB_EVENT_MASK_BUTTON_PRESS,
            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, 1, XCB_MOD_MASK_SHIFT);
    const std::unique_ptr<xcb_generic_error_t, decltype(&std::free)> error(
        xcb_request_check(sender.get(), grab), std::free);
    QVERIFY(!error);

    PressWindow popup;
    popup.setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    popup.setGeometry(30, 30, 100, 80);
    popup.show();
    QVERIFY(QTest::qWaitForWindowExposed(&popup));

    PressWindow outside;
    outside.setGeometry(210, 30, 100, 80);
    outside.show();
    QVERIFY(QTest::qWaitForWindowExposed(&outside));

    platform::x11::OutsideClickWatcher watcher;
    QSignalSpy dismissed(&watcher, &platform::x11::OutsideClickWatcher::clickedOutside);
    auto click = [&](const QWindow &window) {
        const auto scale = window.devicePixelRatio();
        const auto center = window.geometry().center();
        xcb_test_fake_input(sender.get(), XCB_MOTION_NOTIFY, 0, XCB_CURRENT_TIME, root,
            int16_t(qRound(center.x() * scale)), int16_t(qRound(center.y() * scale)), 0);
        xcb_test_fake_input(sender.get(), XCB_BUTTON_PRESS, 1, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
        xcb_test_fake_input(
            sender.get(), XCB_BUTTON_RELEASE, 1, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
        xcb_flush(sender.get());
    };

    watcher.watch(&popup);
    click(popup);
    QTRY_COMPARE(popup.presses, 1);
    QCOMPARE(dismissed.count(), 0);

    click(outside);
    QTRY_COMPARE(dismissed.count(), 1);
    QTRY_COMPARE(outside.presses, 1);

    click(outside);
    QTRY_COMPARE(outside.presses, 2);
    QCOMPARE(dismissed.count(), 1);
}

int main(int argc, char **argv) {
    const QByteArray display = qgetenv("SNIPLYNGO_X11_TEST_DISPLAY");
    if (!display.isEmpty())
        qputenv("DISPLAY", display);
    qputenv("QT_QPA_PLATFORM", display.isEmpty() ? "offscreen" : "xcb");
    QGuiApplication app(argc, argv);
    X11OutsideClickLiveTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "x11_outside_click_live_test.moc"
