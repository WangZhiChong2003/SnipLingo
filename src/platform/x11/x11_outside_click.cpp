#include "platform/x11/x11_outside_click.h"

#include <QSocketNotifier>
#include <QWindow>

#include <cstdlib>
#include <xcb/xinput.h>

namespace platform::x11 {
namespace {

void selectPresses(xcb_connection_t *connection, xcb_window_t root, bool enabled) {
    struct {
        xcb_input_event_mask_t header;
        uint8_t bits[4];
    } mask {{XCB_INPUT_DEVICE_ALL_MASTER, 1}, {}};

    // XI2 event masks are byte arrays in the protocol, regardless of host endianness.
    if (enabled)
        mask.bits[XCB_INPUT_RAW_BUTTON_PRESS / 8] = 1U << (XCB_INPUT_RAW_BUTTON_PRESS % 8);
    xcb_input_xi_select_events(connection, root, 1, &mask.header);
    xcb_flush(connection);
}

}  // namespace

OutsideClickWatcher::OutsideClickWatcher(QObject *parent)
    : QObject(parent) {
    // Keep the subscription separate from Qt's XI2 event masks and version negotiation.
    m_connection.reset(xcb_connect(nullptr, nullptr));
    if (xcb_connection_has_error(m_connection.get()))
        return;
    xcb_connection_t *connection = m_connection.get();
    const auto *extension = xcb_get_extension_data(connection, &xcb_input_id);
    if (!extension || !extension->present)
        return;
    // XI 2.1 delivers raw presses even when the window manager or another application has a grab.
    const std::unique_ptr<xcb_input_xi_query_version_reply_t, decltype(&std::free)> version(
        xcb_input_xi_query_version_reply(
            connection, xcb_input_xi_query_version(connection, 2, 1), nullptr),
        std::free);
    if (!version || version->major_version != 2 || version->minor_version < 1)
        return;

    m_opcode = extension->major_opcode;
    // Every root receives raw events, including presses on another X screen.
    m_root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
    m_notifier = std::make_unique<QSocketNotifier>(
        xcb_get_file_descriptor(connection), QSocketNotifier::Read);
    connect(
        m_notifier.get(), &QSocketNotifier::activated, this, &OutsideClickWatcher::processEvents);
}

OutsideClickWatcher::~OutsideClickWatcher() = default;

void OutsideClickWatcher::watch(QWindow *popup) {
    if (!m_notifier)
        return;
    m_popup = popup;
    selectPresses(m_connection.get(), m_root, true);
}

void OutsideClickWatcher::stop() {
    if (!m_popup)
        return;
    m_popup.clear();
    selectPresses(m_connection.get(), m_root, false);
}

void OutsideClickWatcher::processEvents() {
    xcb_connection_t *connection = m_connection.get();
    while (auto *message = xcb_poll_for_event(connection)) {
        const std::unique_ptr<xcb_generic_event_t, decltype(&std::free)> event(message, std::free);
        if (!m_popup || event->response_type != XCB_GE_GENERIC)
            continue;
        const auto *press = reinterpret_cast<const xcb_input_raw_button_press_event_t *>(message);
        if (press->extension != m_opcode || press->event_type != XCB_INPUT_RAW_BUTTON_PRESS)
            continue;
        if (press->detail >= 4 && press->detail <= 7)
            continue;  // Legacy X11 scroll buttons.

        // Raw events carry no coordinates. Query the device position relative to the real X window
        // and compare in X pixels, including on a fractionally scaled Qt screen.
        const auto window = static_cast<xcb_window_t>(m_popup->winId());
        const auto pointerRequest = xcb_input_xi_query_pointer(connection, window, press->deviceid);
        const auto geometryRequest = xcb_get_geometry(connection, window);
        const std::unique_ptr<xcb_input_xi_query_pointer_reply_t, decltype(&std::free)> pointer(
            xcb_input_xi_query_pointer_reply(connection, pointerRequest, nullptr), std::free);
        const std::unique_ptr<xcb_get_geometry_reply_t, decltype(&std::free)> geometry(
            xcb_get_geometry_reply(connection, geometryRequest, nullptr), std::free);
        if (!geometry || !pointer)
            continue;
        constexpr int64_t unitsPerPixel = 1 << 16;
        if (pointer->same_screen && pointer->win_x >= 0 && pointer->win_y >= 0
            && pointer->win_x < geometry->width * unitsPerPixel
            && pointer->win_y < geometry->height * unitsPerPixel)
            continue;

        stop();
        emit clickedOutside();
    }
    if (xcb_connection_has_error(connection))
        m_notifier->setEnabled(false);
}

}  // namespace platform::x11
