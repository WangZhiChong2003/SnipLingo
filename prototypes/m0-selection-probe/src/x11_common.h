// Shared X11 plumbing for the M0 probe (xcb + XFixes): connection setup, PRIMARY subscription,
// selection conversion (including INCR) and best-effort source identification. Throwaway code.
#pragma once

#include "common.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <xcb/xcb.h>
#include <xcb/xfixes.h>

#include <poll.h>

#include <cstdlib>
#include <cstring>
#include <vector>

namespace probe::x11 {

struct Atoms {
    xcb_atom_t primary = XCB_ATOM_NONE;
    xcb_atom_t utf8String = XCB_ATOM_NONE;
    xcb_atom_t string = XCB_ATOM_NONE;
    xcb_atom_t targets = XCB_ATOM_NONE;
    xcb_atom_t incr = XCB_ATOM_NONE;
    xcb_atom_t prop = XCB_ATOM_NONE;
    xcb_atom_t netWmPid = XCB_ATOM_NONE;
};

struct Connection {
    xcb_connection_t *conn = nullptr;
    xcb_screen_t *screen = nullptr;
    xcb_window_t requestor = XCB_WINDOW_NONE;
    Atoms atoms;
    // X11 delivers extension events as (first_event + subtype). Comparing response_type with
    // XCB_XFIXES_SELECTION_NOTIFY (which is 0) silently matches nothing — the first real run of
    // this probe collected zero events because of exactly that.
    uint8_t xfixesEventBase = 0;
    void *xfixesCookie = nullptr;
};

// X protocol errors arrive as ordinary events with response_type 0. A conversion that is refused
// (BadTime, BadAtom, BadWindow) is therefore invisible unless they are counted explicitly, which is
// exactly what "SelectionNotify timeout" hid during the first probe runs.
inline int &xErrorCount()
{
    static int count = 0;
    return count;
}

inline int &xLastErrorCode()
{
    static int code = 0;
    return code;
}

inline bool isXfixesSelectionNotify(const Connection &c, const xcb_generic_event_t *event)
{
    return event
        && event->response_type == uint8_t(c.xfixesEventBase + XCB_XFIXES_SELECTION_NOTIFY);
}

struct ResolvedText {
    bool ok = false;
    QString text;
    QString error;
    double ms = 0.0;
    bool incr = false;
    QString strategy;
};

inline bool internAtom(xcb_connection_t *conn, const char *name, xcb_atom_t *out)
{
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(conn, xcb_intern_atom(conn, 0, uint16_t(std::strlen(name)), name), nullptr);
    if (!reply)
        return false;
    *out = reply->atom;
    std::free(reply);
    return *out != XCB_ATOM_NONE;
}

// Opens a private connection plus an unmapped InputOnly requestor window for conversions.
inline bool open(Connection &out, QString *error)
{
    int screenNumber = 0;
    out.conn = xcb_connect(nullptr, &screenNumber);
    if (!out.conn || xcb_connection_has_error(out.conn)) {
        *error = QStringLiteral("xcb_connect failed (DISPLAY unset or its socket unreachable)");
        return false;
    }
    if (xcb_get_setup(out.conn)->roots_len == 0) {
        *error = QStringLiteral("no X screen");
        return false;
    }

    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(out.conn));
    for (int i = 0; i < screenNumber && it.rem; ++i)
        xcb_screen_next(&it);
    out.screen = it.data;

    if (!internAtom(out.conn, "PRIMARY", &out.atoms.primary)
        || !internAtom(out.conn, "UTF8_STRING", &out.atoms.utf8String)
        || !internAtom(out.conn, "STRING", &out.atoms.string)
        || !internAtom(out.conn, "TARGETS", &out.atoms.targets)
        || !internAtom(out.conn, "INCR", &out.atoms.incr)
        || !internAtom(out.conn, "SNIPLYNGO_SELECTION", &out.atoms.prop)
        || !internAtom(out.conn, "_NET_WM_PID", &out.atoms.netWmPid)) {
        *error = QStringLiteral("cannot intern required atoms");
        return false;
    }

    out.requestor = xcb_generate_id(out.conn);
    const uint32_t eventMask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(out.conn, 0, out.requestor, out.screen->root, -10, -10, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &eventMask);
    xcb_flush(out.conn);
    return true;
}

inline void close(Connection &c)
{
    if (c.conn)
        xcb_disconnect(c.conn);
    c.conn = nullptr;
}

// Subscribes to PRIMARY ownership notifications.
inline bool subscribePrimary(Connection &c, QString *error)
{
    const xcb_query_extension_reply_t *extension = xcb_get_extension_data(c.conn, &xcb_xfixes_id);
    if (!extension || !extension->present) {
        *error = QStringLiteral("XFixes extension not present on this server");
        return false;
    }
    c.xfixesEventBase = extension->first_event;
    c.xfixesCookie = const_cast<void *>(static_cast<const void *>(&xcb_xfixes_id));
    xcb_xfixes_query_version_reply_t *version =
        xcb_xfixes_query_version_reply(c.conn, xcb_xfixes_query_version(c.conn, 5, 0), nullptr);
    if (!version) {
        *error = QStringLiteral("XFixes QueryVersion failed");
        return false;
    }
    std::free(version);

    const uint32_t mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER
        | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY
        | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
    xcb_generic_error_t *err = xcb_request_check(
        c.conn, xcb_xfixes_select_selection_input_checked(c.conn, c.screen->root, c.atoms.primary, mask));
    if (err) {
        *error = QStringLiteral("XFixes SelectSelectionInput failed (error_code=%1)").arg(err->error_code);
        std::free(err);
        return false;
    }
    xcb_flush(c.conn);
    return true;
}

// Blocking wait for one event; nullptr on timeout.
inline xcb_generic_event_t *nextEvent(Connection &c, int timeoutMs)
{
    xcb_flush(c.conn);
    struct pollfd pfd = {xcb_get_file_descriptor(c.conn), POLLIN, 0};
    if (::poll(&pfd, 1, timeoutMs) <= 0)
        return nullptr;
    return xcb_poll_for_event(c.conn);
}

// Best-effort source identification: _NET_WM_PID of the selection owner, then /proc/<pid>/comm.
inline QString ownerProcessName(Connection &c, xcb_window_t owner)
{
    if (owner == XCB_WINDOW_NONE)
        return QStringLiteral("<none>");

    for (int depth = 0; depth < 8 && owner != XCB_WINDOW_NONE; ++depth) {
        xcb_get_property_reply_t *reply = xcb_get_property_reply(
            c.conn, xcb_get_property(c.conn, 0, owner, c.atoms.netWmPid, XCB_ATOM_CARDINAL, 0, 1), nullptr);
        if (reply && reply->type != XCB_ATOM_NONE && xcb_get_property_value_length(reply) >= 4) {
            const auto pid = *static_cast<const uint32_t *>(xcb_get_property_value(reply));
            std::free(reply);
            QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
            if (comm.open(QIODevice::ReadOnly))
                return QString::fromUtf8(comm.readAll()).trimmed();
            return QStringLiteral("pid:%1").arg(pid);
        }
        std::free(reply);

        xcb_query_tree_reply_t *tree = xcb_query_tree_reply(c.conn, xcb_query_tree(c.conn, owner), nullptr);
        if (!tree)
            break;
        owner = tree->parent;
        std::free(tree);
    }
    return QStringLiteral("<unknown>");
}

inline bool waitForSelectionNotify(Connection &c, std::vector<xcb_xfixes_selection_notify_event_t> *deferred,
                                   xcb_selection_notify_event_t **out, int timeoutMs)
{
    Stopwatch watch;
    while (watch.ms() < timeoutMs) {
        xcb_generic_event_t *event = nextEvent(c, qMax(1, int(timeoutMs - watch.ms())));
        if (!event)
            break;
        const uint8_t type = event->response_type & ~0x80;
        if (type == XCB_SELECTION_NOTIFY) {
            *out = reinterpret_cast<xcb_selection_notify_event_t *>(event);
            return true;
        }
        if (isXfixesSelectionNotify(c, event) && deferred) {
            const auto *notify = reinterpret_cast<xcb_xfixes_selection_notify_event_t *>(event);
            if (notify->owner != XCB_WINDOW_NONE)
                deferred->push_back(*notify);
        } else if ((event->response_type & ~0x80) == 0) {
            const auto *error = reinterpret_cast<xcb_generic_error_t *>(event);
            ++xErrorCount();
            xLastErrorCode() = error->error_code;
        }
        std::free(event);
    }
    *out = nullptr;
    return false;
}

inline bool waitForPropertyNotify(Connection &c, xcb_atom_t property, int timeoutMs)
{
    Stopwatch watch;
    while (watch.ms() < timeoutMs) {
        xcb_generic_event_t *event = nextEvent(c, qMax(1, int(timeoutMs - watch.ms())));
        if (!event)
            break;
        if ((event->response_type & ~0x80) == XCB_PROPERTY_NOTIFY) {
            const auto *notify = reinterpret_cast<xcb_property_notify_event_t *>(event);
            const bool match = notify->atom == property && notify->state == XCB_PROPERTY_NEW_VALUE;
            std::free(event);
            if (match)
                return true;
            continue;
        }
        std::free(event);
    }
    return false;
}

// Reads the converted property, handling INCR and enforcing a byte cap.
inline QByteArray readProperty(Connection &c, int maxBytes, bool *incr, xcb_atom_t *returnedType, QString *error)
{
    xcb_get_property_reply_t *reply = xcb_get_property_reply(
        c.conn, xcb_get_property(c.conn, 0, c.requestor, c.atoms.prop, XCB_ATOM_ANY, 0, maxBytes / 4 + 16), nullptr);
    if (!reply) {
        *error = QStringLiteral("GetProperty failed");
        return {};
    }
    *returnedType = reply->type;

    if (reply->type == c.atoms.incr) {
        *incr = true;
        std::free(reply);
        QByteArray assembled;
        for (int round = 0; round < 4096; ++round) {
            if (!waitForPropertyNotify(c, c.atoms.prop, 2000)) {
                *error = QStringLiteral("INCR chunk timeout");
                return {};
            }
            xcb_get_property_reply_t *chunk = xcb_get_property_reply(
                c.conn, xcb_get_property(c.conn, 1, c.requestor, c.atoms.prop, XCB_ATOM_ANY, 0, maxBytes / 4 + 16),
                nullptr);
            if (!chunk) {
                *error = QStringLiteral("INCR GetProperty failed");
                return {};
            }
            const int length = xcb_get_property_value_length(chunk);
            if (length == 0) {
                std::free(chunk);
                return assembled;
            }
            assembled.append(static_cast<const char *>(xcb_get_property_value(chunk)), length);
            std::free(chunk);
            if (assembled.size() > maxBytes) {
                *error = QStringLiteral("INCR exceeded size cap");
                return {};
            }
        }
        *error = QStringLiteral("INCR too many chunks");
        return {};
    }

    QByteArray data;
    const int length = xcb_get_property_value_length(reply);
    if (length > 0)
        data = QByteArray(static_cast<const char *>(xcb_get_property_value(reply)), length);
    std::free(reply);
    xcb_delete_property(c.conn, c.requestor, c.atoms.prop);
    xcb_flush(c.conn);
    return data;
}

// Converts PRIMARY to `target` and reads the bytes.
//
// Two attempts. The first M0 measurement (automatic harness, Run 8) showed every conversion through an
// XWayland session stalling for the whole deadline when the XFixes event timestamp was used, while the
// same request with CurrentTime answered immediately. The event timestamp therefore gets a short
// deadline, so a stall costs 250 ms instead of 1500 ms, and the winning attempt is reported so the
// numbers stay honest.
inline QByteArray convert(Connection &c, xcb_atom_t target, xcb_timestamp_t time, int maxBytes,
                          std::vector<xcb_xfixes_selection_notify_event_t> *deferred, bool *incr,
                          xcb_atom_t *returnedType, QString *error, QString *strategy)
{
    struct Attempt {
        xcb_timestamp_t time;
        int deadlineMs;
        const char *label;
    };
    Attempt attempts[2] = {{time, 250, "event-time/250ms"}, {XCB_CURRENT_TIME, 1500, "CurrentTime/1500ms"}};
    const int attemptCount = (time == XCB_CURRENT_TIME) ? 1 : 2;
    if (attemptCount == 1)
        attempts[0] = {XCB_CURRENT_TIME, 1500, "CurrentTime/1500ms"};

    *incr = false;
    *returnedType = XCB_ATOM_NONE;
    for (int i = 0; i < attemptCount; ++i) {
        const Attempt &attempt = attempts[i];
        Stopwatch watch;
        xcb_convert_selection(c.conn, c.requestor, c.atoms.primary, target, c.atoms.prop, attempt.time);
        xcb_flush(c.conn);

        xcb_selection_notify_event_t *notify = nullptr;
        if (!waitForSelectionNotify(c, deferred, &notify, attempt.deadlineMs))
            continue;

        const xcb_atom_t property = notify->property;
        std::free(notify);
        if (strategy)
            *strategy = QStringLiteral("%1 answered in %2 ms")
                            .arg(QString::fromLatin1(attempt.label))
                            .arg(watch.ms(), 0, 'f', 1);
        if (property == XCB_ATOM_NONE) {
            *error = QStringLiteral("owner refused target");
            return {};
        }
        return readProperty(c, maxBytes, incr, returnedType, error);
    }

    *error = QStringLiteral("no SelectionNotify from either attempt (X errors: %1, last %2)")
                 .arg(xErrorCount())
                 .arg(xLastErrorCode());
    return {};
}

// Requests PRIMARY as text, falling back to TARGETS negotiation when UTF8_STRING is refused.
inline ResolvedText resolvePrimary(Connection &c, xcb_timestamp_t time, int maxChars,
                                   std::vector<xcb_xfixes_selection_notify_event_t> *deferred)
{
    ResolvedText out;
    Stopwatch watch;
    const int maxBytes = maxChars * 4 + 64;

    QString error;
    QString strategy;
    bool incr = false;
    xcb_atom_t type = XCB_ATOM_NONE;
    QByteArray data = convert(c, c.atoms.utf8String, time, maxBytes, deferred, &incr, &type, &error,
                              &strategy);

    if (data.isEmpty() && !error.isEmpty()) {
        QString targetsError;
        bool targetsIncr = false;
        xcb_atom_t targetsType = XCB_ATOM_NONE;
        QString targetsStrategy;
        const QByteArray targets = convert(c, c.atoms.targets, time, 4096, deferred, &targetsIncr,
                                           &targetsType, &targetsError, &targetsStrategy);
        const auto *atoms = reinterpret_cast<const xcb_atom_t *>(targets.constData());
        const int count = int(targets.size() / int(sizeof(xcb_atom_t)));
        bool hasUtf8 = false;
        bool hasString = false;
        for (int i = 0; i < count; ++i) {
            hasUtf8 = hasUtf8 || atoms[i] == c.atoms.utf8String;
            hasString = hasString || atoms[i] == c.atoms.string;
        }
        if (hasUtf8) {
            data = convert(c, c.atoms.utf8String, time, maxBytes, deferred, &incr, &type, &error,
                           &strategy);
        } else if (hasString) {
            data = convert(c, c.atoms.string, time, maxBytes, deferred, &incr, &type, &error,
                           &strategy);
            if (!data.isEmpty())
                error.clear();
        } else {
            out.ms = watch.ms();
            out.error = QStringLiteral("no text target offered");
            return out;
        }
    }

    out.ms = watch.ms();
    out.strategy = strategy;
    out.incr = incr;
    if (data.isEmpty()) {
        out.error = error.isEmpty() ? QStringLiteral("empty conversion") : error;
        return out;
    }
    out.ok = true;
    out.text = (type == c.atoms.string) ? QString::fromLatin1(data) : QString::fromUtf8(data);
    return out;
}

} // namespace probe::x11
