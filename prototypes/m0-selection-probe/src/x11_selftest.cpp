// M0 self-test: proves the XFixes mechanism without a human. This process becomes the PRIMARY
// owner on one connection and the watcher on another, so event delivery + conversion + property
// read are exercised end to end.
#include <QJsonObject>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "common.h"
#include "x11_common.h"

namespace {

class SelectionOwner {
public:
    ~SelectionOwner() { stop(); }

    bool start(QString *error) {
        m_conn = xcb_connect(nullptr, nullptr);
        if (!m_conn || xcb_connection_has_error(m_conn)) {
            *error = QStringLiteral("owner: xcb_connect failed");
            return false;
        }
        xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(m_conn)).data;
        if (!probe::x11::internAtom(m_conn, "PRIMARY", &m_primary)
            || !probe::x11::internAtom(m_conn, "UTF8_STRING", &m_utf8)
            || !probe::x11::internAtom(m_conn, "STRING", &m_string)
            || !probe::x11::internAtom(m_conn, "TARGETS", &m_targets)) {
            *error = QStringLiteral("owner: cannot intern atoms");
            return false;
        }
        const uint32_t noEvents = 0;
        for (xcb_window_t &window : m_windows) {
            window = xcb_generate_id(m_conn);
            xcb_create_window(m_conn, 0, window, screen->root, -10, -10, 1, 1, 0,
                XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &noEvents);
        }
        xcb_flush(m_conn);
        m_thread = std::thread([this] { loop(); });
        return true;
    }

    // Records the text to publish. The actual X call happens on the owner thread: an xcb connection
    // may only be used from one thread, and this method is called from the main thread.
    void publish(const QString &text) {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_text = text;
        ++m_publishRequest;
    }

    bool ownsWindow(xcb_window_t window) const {
        return window == m_windows[0] || window == m_windows[1];
    }

    void stop() {
        if (m_stop.exchange(true))
            return;
        if (m_thread.joinable())
            m_thread.join();
        if (m_conn) {
            xcb_disconnect(m_conn);
            m_conn = nullptr;
        }
    }

private:
    void loop() {
        const int fd = xcb_get_file_descriptor(m_conn);
        int servedRequest = 0;
        while (!m_stop.load()) {
            bool published = false;
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                if (m_publishRequest != servedRequest) {
                    servedRequest = m_publishRequest;
                    published = true;
                }
            }
            if (published) {
                // Alternate the owner window so every publish is a real ownership change.
                m_current = (m_current + 1) % 2;
                xcb_set_selection_owner(m_conn, m_windows[m_current], m_primary, XCB_CURRENT_TIME);
                xcb_flush(m_conn);
            }

            struct pollfd pfd = {fd, POLLIN, 0};
            if (::poll(&pfd, 1, 20) <= 0)
                continue;
            while (xcb_generic_event_t *event = xcb_poll_for_event(m_conn)) {
                const uint8_t type = event->response_type & ~0x80;
                if (type == XCB_SELECTION_REQUEST)
                    serve(reinterpret_cast<xcb_selection_request_event_t *>(event));
                else if (type == XCB_SELECTION_CLEAR) {
                    const auto *clear = reinterpret_cast<xcb_selection_clear_event_t *>(event);
                    xcb_selection_notify_event_t notify {};
                    notify.response_type = XCB_SELECTION_NOTIFY;
                    notify.requestor = clear->owner;
                    notify.selection = clear->selection;
                    notify.property = XCB_ATOM_NONE;
                    notify.time = clear->time;
                    xcb_send_event(m_conn, 0, clear->owner, XCB_EVENT_MASK_NO_EVENT,
                        reinterpret_cast<const char *>(&notify));
                    xcb_flush(m_conn);
                }
                std::free(event);
            }
        }
    }

    void serve(xcb_selection_request_event_t *request) {
        QString text;
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            text = m_text;
        }

        xcb_atom_t property = request->property;
        if (request->target == m_targets) {
            const xcb_atom_t list[] = {m_targets, m_utf8, m_string};
            xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, request->requestor, property,
                XCB_ATOM_ATOM, 32, 3, list);
        } else if (request->target == m_utf8) {
            const QByteArray data = text.toUtf8();
            xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, request->requestor, property, m_utf8,
                8, uint32_t(data.size()), data.constData());
        } else if (request->target == m_string) {
            const QByteArray data = text.toLatin1();
            xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, request->requestor, property,
                m_string, 8, uint32_t(data.size()), data.constData());
        } else {
            property = XCB_ATOM_NONE;
        }

        xcb_selection_notify_event_t notify {};
        notify.response_type = XCB_SELECTION_NOTIFY;
        notify.requestor = request->requestor;
        notify.selection = request->selection;
        notify.target = request->target;
        notify.property = property;
        notify.time = request->time;
        xcb_send_event(m_conn, 0, request->requestor, XCB_EVENT_MASK_NO_EVENT,
            reinterpret_cast<const char *>(&notify));
        xcb_flush(m_conn);
    }

    xcb_connection_t *m_conn = nullptr;
    xcb_window_t m_windows[2] = {XCB_WINDOW_NONE, XCB_WINDOW_NONE};
    int m_current = 0;
    xcb_atom_t m_primary = XCB_ATOM_NONE;
    xcb_atom_t m_utf8 = XCB_ATOM_NONE;
    xcb_atom_t m_string = XCB_ATOM_NONE;
    xcb_atom_t m_targets = XCB_ATOM_NONE;
    QString m_text;
    std::mutex m_mutex;
    std::atomic<bool> m_stop {false};
    int m_publishRequest = 0;  // guarded by m_mutex
    std::thread m_thread;
};

}  // namespace

int runX11SelfTest(int iterations) {
    probe::x11::Connection watcher;
    QString error;
    if (!probe::x11::open(watcher, &error)) {
        probe::line(QStringLiteral("selftest: watcher unavailable — %1").arg(error));
        return 2;
    }
    if (!probe::x11::subscribePrimary(watcher, &error)) {
        probe::line(QStringLiteral("selftest: subscription failed — %1").arg(error));
        probe::x11::close(watcher);
        return 2;
    }

    SelectionOwner owner;
    if (!owner.start(&error)) {
        probe::line(QStringLiteral("selftest: %1").arg(error));
        probe::x11::close(watcher);
        return 2;
    }

    probe::line(
        QStringLiteral("selftest: XFixes event base %1, %2 ownership changes, no user interaction")
            .arg(watcher.xfixesEventBase)
            .arg(iterations));
    int pass = 0;
    int fail = 0;

    for (int i = 1; i <= iterations; ++i) {
        const QString expected = QStringLiteral("SnipLingo M0 selftest line %1").arg(i);
        std::vector<xcb_xfixes_selection_notify_event_t> deferred;
        owner.publish(expected);

        // Wait for the ownership notification.
        xcb_xfixes_selection_notify_event_t notify {};
        bool gotEvent = false;
        probe::Stopwatch wait;
        while (wait.ms() < 1500 && !gotEvent) {
            xcb_generic_event_t *event = probe::x11::nextEvent(watcher, 200);
            if (!event)
                continue;
            if (probe::x11::isXfixesSelectionNotify(watcher, event)) {
                notify = *reinterpret_cast<xcb_xfixes_selection_notify_event_t *>(event);
                gotEvent = notify.owner != XCB_WINDOW_NONE;
            }
            std::free(event);
        }

        if (!gotEvent) {
            ++fail;
            probe::line(QStringLiteral("  %1 FAIL: no ownership notification").arg(i));
            continue;
        }

        const probe::x11::ResolvedText resolved
            = probe::x11::resolvePrimary(watcher, notify.timestamp, 4000, &deferred);
        if (resolved.ok && resolved.text == expected) {
            ++pass;
            probe::line(QStringLiteral("  %1 PASS resolve=%2ms incr=%3 chars=%4 [%5]")
                            .arg(i)
                            .arg(resolved.ms, 0, 'f', 1)
                            .arg(resolved.incr ? "yes" : "no")
                            .arg(resolved.text.size())
                            .arg(resolved.strategy));
        } else {
            ++fail;
            xcb_get_selection_owner_reply_t *current = xcb_get_selection_owner_reply(watcher.conn,
                xcb_get_selection_owner(watcher.conn, watcher.atoms.primary), nullptr);
            const xcb_window_t actualOwner
                = current ? current->owner : xcb_window_t(XCB_WINDOW_NONE);
            std::free(current);
            // If the owner is neither of our windows, something outside this process took PRIMARY
            // over — on a Wayland session that is XWayland/mutter mirroring the compositor
            // clipboard.
            const bool ours = actualOwner != XCB_WINDOW_NONE && owner.ownsWindow(actualOwner);
            probe::line(
                QStringLiteral("  %1 FAIL: %2 (chars=%3 digest=%4, owner=0x%5, owned by us=%6)")
                    .arg(i)
                    .arg(resolved.ok ? QStringLiteral("text mismatch") : resolved.error)
                    .arg(resolved.ok ? resolved.text.size() : 0)
                    .arg(resolved.ok ? probe::digest8(resolved.text) : QString(),
                        QString::number(actualOwner, 16),
                        actualOwner == xcb_window_t(XCB_WINDOW_NONE)
                            ? QStringLiteral("none")
                            : (ours ? QStringLiteral("yes") : QStringLiteral("NO — mediated"))));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    owner.stop();
    probe::x11::close(watcher);

    probe::heading("selftest summary");
    probe::line(QStringLiteral("pass/fail : %1/%2").arg(pass).arg(fail));
    probe::line(QStringLiteral("X errors  : %1 (last error code %2)")
                    .arg(probe::x11::xErrorCount())
                    .arg(probe::x11::xLastErrorCode()));
    probe::line(pass == iterations
            ? QStringLiteral("mechanism verified: XFixes notification + conversion + property read")
            : QStringLiteral("mechanism NOT fully verified — see failures above"));
    return pass == iterations ? 0 : 1;
}
