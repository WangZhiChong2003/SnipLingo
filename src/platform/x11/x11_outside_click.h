// Observe mouse presses while a floating result is visible.
#pragma once

#include <QObject>
#include <QPointer>

#include <memory>
#include <xcb/xcb.h>

class QSocketNotifier;
class QWindow;

namespace platform::x11 {

class OutsideClickWatcher : public QObject {
    Q_OBJECT
public:
    explicit OutsideClickWatcher(QObject *parent = nullptr);
    ~OutsideClickWatcher() override;

    void watch(QWindow *popup);
    void stop();

signals:
    void clickedOutside();

private:
    void processEvents();

    std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> m_connection {
        nullptr, xcb_disconnect};
    std::unique_ptr<QSocketNotifier> m_notifier;
    QPointer<QWindow> m_popup;
    xcb_window_t m_root = XCB_NONE;
    uint8_t m_opcode = 0;
};

}  // namespace platform::x11
