#include "platform/x11/x11_backend.h"

#include "core/log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QScopeGuard>

#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>

namespace {

struct Atoms {
    xcb_atom_t primary = XCB_ATOM_NONE;
    xcb_atom_t utf8String = XCB_ATOM_NONE;
    xcb_atom_t string = XCB_ATOM_NONE;
    xcb_atom_t incr = XCB_ATOM_NONE;
    xcb_atom_t property = XCB_ATOM_NONE;
    xcb_atom_t netWmPid = XCB_ATOM_NONE;
};

bool internAtom(xcb_connection_t *connection, const char *name, xcb_atom_t *out) {
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
        connection, xcb_intern_atom(connection, 0, uint16_t(std::strlen(name)), name), nullptr);
    if (!reply)
        return false;
    *out = reply->atom;
    std::free(reply);
    return *out != XCB_ATOM_NONE;
}

uint32_t ownerProcessId(xcb_connection_t *connection, xcb_window_t owner, xcb_atom_t pidAtom) {
    for (int depth = 0; depth < 8 && owner != XCB_WINDOW_NONE; ++depth) {
        xcb_get_property_reply_t *reply = xcb_get_property_reply(connection,
            xcb_get_property(connection, 0, owner, pidAtom, XCB_ATOM_CARDINAL, 0, 1), nullptr);
        if (reply && reply->type != XCB_ATOM_NONE && xcb_get_property_value_length(reply) >= 4) {
            const auto pid = *static_cast<const uint32_t *>(xcb_get_property_value(reply));
            std::free(reply);
            return pid;
        }
        std::free(reply);
        xcb_query_tree_reply_t *tree
            = xcb_query_tree_reply(connection, xcb_query_tree(connection, owner), nullptr);
        if (!tree)
            break;
        owner = tree->parent;
        std::free(tree);
    }
    return 0;
}

}  // namespace

X11Backend::X11Backend(QObject *parent)
    : selection::SelectionBackend(parent) { }

X11Backend::~X11Backend() {
    unsubscribe();
    if (m_thread) {
        m_stopRequested = true;
        if (!m_thread->wait(2000)) {
            core::log::write(core::log::Level::Warning, "selection",
                QStringLiteral("X11 backend thread did not stop; abandoning it"));
        } else {
            delete m_thread;
        }
        m_thread = nullptr;
    }
}

selection::BackendProbe X11Backend::probe() {
    selection::BackendProbe probe;
    probe.identifier = backendId();
    probe.humanName = QStringLiteral("X11 PRIMARY ownership notifications");

    int screenNumber = 0;
    xcb_connection_t *connection = xcb_connect(nullptr, &screenNumber);
    if (!connection || xcb_connection_has_error(connection)) {
        if (connection)
            xcb_disconnect(connection);
        probe.reason = QStringLiteral("no usable X11 display in DISPLAY");
        return probe;
    }
    const xcb_query_extension_reply_t *extension
        = xcb_get_extension_data(connection, &xcb_xfixes_id);
    const bool hasXfixes = extension && extension->present;
    xcb_xfixes_query_version_reply_t *version = hasXfixes
        ? xcb_xfixes_query_version_reply(
            connection, xcb_xfixes_query_version(connection, 5, 0), nullptr)
        : nullptr;
    const bool usable = version != nullptr;
    std::free(version);
    xcb_disconnect(connection);

    probe.interfacePresent = hasXfixes;
    probe.accessGranted = usable;
    probe.reason = usable
        ? QStringLiteral(
            "XFixes is available; applications that do not publish ownership changes stay "
            "invisible")
        : QStringLiteral("XFixes PRIMARY is unavailable on the X server");
    return probe;
}

core::Result<void> X11Backend::subscribe() {
    if (m_state == selection::BackendState::Running)
        return {};
    if (m_thread != nullptr) {
        if (m_thread->isRunning())
            return core::Error {core::ErrorCode::SelectionBackendFailed,
                QStringLiteral("the previous X11 listener is still stopping")};
        delete m_thread;
        m_thread = nullptr;
    }

    int wakeEnds[2] = {-1, -1};
    if (pipe(wakeEnds) != 0) {
        return core::Error {
            core::ErrorCode::SelectionBackendFailed, QStringLiteral("cannot create wake pipe")};
    }
    m_wakeReadFd = wakeEnds[0];
    m_wakeFd = wakeEnds[1];

    m_stopRequested = false;
    {
        QMutexLocker locker(&m_stateMutex);
        m_failureReason.clear();
    }
    m_state = selection::BackendState::Subscribing;
    m_thread = QThread::create([this] { workerLoop(); });
    m_thread->start();

    // The worker publishes its own state; wait for it before claiming success.
    for (int waited = 0; waited < 3000; waited += 20) {
        if (m_state == selection::BackendState::Running)
            return {};
        if (m_state == selection::BackendState::Failed
            || m_state == selection::BackendState::Denied)
            break;
        QThread::msleep(20);
    }
    if (m_state == selection::BackendState::Running)
        return {};
    QString reason;
    {
        QMutexLocker locker(&m_stateMutex);
        reason = m_failureReason;
    }
    unsubscribe();
    return core::Error {core::ErrorCode::SelectionBackendFailed,
        reason.isEmpty() ? QStringLiteral("X11 backend did not start") : reason};
}

void X11Backend::reportFailure(selection::BackendState state, const QString &reason) {
    {
        QMutexLocker locker(&m_stateMutex);
        m_failureReason = reason;
    }
    m_state = state;
    emit stateChanged(state, reason);
}

void X11Backend::unsubscribe() {
    if (m_thread == nullptr)
        return;
    m_stopRequested = true;
    if (m_wakeFd >= 0) {
        const char byte = 1;
        [[maybe_unused]] const ssize_t written = ::write(m_wakeFd, &byte, 1);
    }
    if (m_thread && !m_thread->wait(2000)) {
        core::log::write(core::log::Level::Warning, "selection",
            QStringLiteral("X11 backend thread did not stop within the deadline"));
        return;
    }
    delete m_thread;
    m_thread = nullptr;
    if (m_wakeFd >= 0)
        ::close(m_wakeFd);
    if (m_wakeReadFd >= 0)
        ::close(m_wakeReadFd);
    m_wakeFd = -1;
    m_wakeReadFd = -1;
    m_state = selection::BackendState::Stopped;
    emit stateChanged(m_state, QString());
}

void X11Backend::postCommand(std::function<void()> command) {
    {
        QMutexLocker locker(&m_queueMutex);
        m_commands.push_back(std::move(command));
    }
    if (m_wakeFd >= 0) {
        const char byte = 1;
        [[maybe_unused]] const ssize_t written = ::write(m_wakeFd, &byte, 1);
    }
}

void X11Backend::resolveLatest(
    quint64 seq, selection::ResolveLimits limits, selection::ResolveCallback callback) {
    const bool sameSeq = (seq == m_lastEmittedSeq.load());
    if (!sameSeq) {
        callback(seq,
            core::Error {core::ErrorCode::SelectionCancelled, QStringLiteral("unknown event")});
        return;
    }
    postCommand([this, seq, limits, callback] {
        performResolve(seq, limits,
            [this, callback](quint64 resolvedSeq, core::Result<selection::SelectionText> result) {
                // The coordinator and provider live on the UI thread, not on this X11 worker.
                QMetaObject::invokeMethod(
                    this,
                    [callback, resolvedSeq, result = std::move(result)]() mutable {
                        callback(resolvedSeq, std::move(result));
                    },
                    Qt::QueuedConnection);
            });
    });
}

void X11Backend::cancelPending() {
    // Conversions are bounded by their deadlines in the worker thread; a superseded request is
    // discarded by the coordinator's generation check.
}

void X11Backend::workerLoop() {
    int screenNumber = 0;
    xcb_connection_t *connection = xcb_connect(nullptr, &screenNumber);
    if (!connection || xcb_connection_has_error(connection)) {
        reportFailure(selection::BackendState::Denied, QStringLiteral("no usable X11 display"));
        if (connection)
            xcb_disconnect(connection);
        return;
    }
    m_connection = connection;

    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(xcb_get_setup(connection));
    for (int i = 0; i < screenNumber && iterator.rem; ++i)
        xcb_screen_next(&iterator);
    xcb_screen_t *screen = iterator.data;

    Atoms atoms;
    const bool atomsOk = internAtom(connection, "PRIMARY", &atoms.primary)
        && internAtom(connection, "UTF8_STRING", &atoms.utf8String)
        && internAtom(connection, "STRING", &atoms.string)
        && internAtom(connection, "INCR", &atoms.incr)
        && internAtom(connection, "SNIPLYNGO_SELECTION", &atoms.property)
        && internAtom(connection, "_NET_WM_PID", &atoms.netWmPid);
    const xcb_query_extension_reply_t *extension
        = xcb_get_extension_data(connection, &xcb_xfixes_id);
    if (!atomsOk || !extension || !extension->present || !screen) {
        reportFailure(selection::BackendState::Failed,
            QStringLiteral("XFixes or required atoms unavailable"));
        xcb_disconnect(connection);
        m_connection = nullptr;
        return;
    }
    m_xfixesEventBase = extension->first_event;
    xcb_xfixes_query_version_reply_t *version = xcb_xfixes_query_version_reply(
        connection, xcb_xfixes_query_version(connection, 5, 0), nullptr);
    if (!version) {
        reportFailure(
            selection::BackendState::Failed, QStringLiteral("XFixes version negotiation failed"));
        xcb_disconnect(connection);
        m_connection = nullptr;
        return;
    }
    std::free(version);

    const uint32_t mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER
        | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY
        | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
    xcb_generic_error_t *error = xcb_request_check(connection,
        xcb_xfixes_select_selection_input_checked(connection, screen->root, atoms.primary, mask));
    if (error) {
        const int errorCode = error->error_code;
        std::free(error);
        reportFailure(selection::BackendState::Denied,
            QStringLiteral("XFixes selection subscription failed (X11 error %1)").arg(errorCode));
        xcb_disconnect(connection);
        m_connection = nullptr;
        return;
    }
    m_rootWindow = screen->root;
    m_atomPrimary = atoms.primary;
    m_atomUtf8 = atoms.utf8String;
    m_atomIncr = atoms.incr;
    m_atomProperty = atoms.property;
    xcb_flush(connection);

    m_state = selection::BackendState::Running;
    emit stateChanged(m_state, QString());

    const int xcbFd = xcb_get_file_descriptor(connection);
    while (!m_stopRequested.load()) {
        struct pollfd descriptors[2] = {{xcbFd, POLLIN, 0}, {m_wakeReadFd, POLLIN, 0}};
        const int ready = ::poll(descriptors, 2, 200);
        if (ready < 0 && errno != EINTR)
            break;
        while (xcb_generic_event_t *event = xcb_poll_for_event(connection)) {
            if (platform::x11::isXfixesSelectionNotify(m_xfixesEventBase, event->response_type))
                handleXfixesEvent(event);
            std::free(event);
        }
        if (descriptors[1].revents & POLLIN) {
            char drain[64];
            // The pipe is blocking: a second read would wait forever after consuming the wakeup.
            [[maybe_unused]] const ssize_t readBytes = ::read(m_wakeReadFd, drain, sizeof drain);
            drainCommands();
        }
        xcb_flush(connection);
    }

    xcb_disconnect(connection);
    m_connection = nullptr;
    m_state = selection::BackendState::Stopped;
    emit stateChanged(m_state, QString());
}

void X11Backend::drainCommands() {
    std::deque<std::function<void()>> pending;
    {
        QMutexLocker locker(&m_queueMutex);
        pending.swap(m_commands);
    }
    for (auto &command : pending) {
        if (command)
            command();
    }
}

void X11Backend::handleXfixesEvent(const void *opaque) {
    const auto *event = static_cast<const xcb_xfixes_selection_notify_event_t *>(opaque);
    if (event->owner == XCB_WINDOW_NONE)
        return;

    auto *connection = static_cast<xcb_connection_t *>(m_connection);
    xcb_atom_t pidAtom = XCB_ATOM_NONE;
    internAtom(connection, "_NET_WM_PID", &pidAtom);
    const uint32_t ownerPid
        = pidAtom == XCB_ATOM_NONE ? 0 : ownerProcessId(connection, event->owner, pidAtom);
    if (ownerPid != 0 && ownerPid == uint32_t(QCoreApplication::applicationPid()))
        return;

    selection::SelectionEvent emitted;
    emitted.seq = m_nextSeq++;
    m_lastEmittedSeq = emitted.seq;
    emitted.source.appName = platform::x11::processNameForPid(ownerPid);
    emitted.source.backendId = backendId();
    emitted.observedAt = QDateTime::currentDateTimeUtc();

    // The pointer is a legitimate anchor on X11 (it is not available to Wayland clients).
    xcb_query_pointer_reply_t *pointer
        = xcb_query_pointer_reply(connection, xcb_query_pointer(connection, event->owner), nullptr);
    if (pointer) {
        emitted.anchorLogical = QRect(pointer->root_x, pointer->root_y, 1, 1);
        std::free(pointer);
    }
    emit eventObserved(emitted);
}

void X11Backend::performResolve(
    quint64 seq, selection::ResolveLimits limits, selection::ResolveCallback callback) {
    auto *connection = static_cast<xcb_connection_t *>(m_connection);
    if (!connection) {
        callback(seq,
            core::Error {core::ErrorCode::SelectionBackendFailed, QStringLiteral("no connection")});
        return;
    }
    if (seq != m_lastEmittedSeq.load()) {
        callback(
            seq, core::Error {core::ErrorCode::SelectionCancelled, QStringLiteral("superseded")});
        return;
    }

    const int maxBytes = limits.maxChars * 4 + 64;
    platform::x11::IncrAssembler assembler(maxBytes);

    // A requestor belongs to one conversion. Destroying it prevents a late SelectionNotify from
    // an older owner from being mistaken for the reply to a newer selection.
    const xcb_window_t requestor = xcb_generate_id(connection);
    const uint32_t requestorMask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(connection, 0, requestor, m_rootWindow, -10, -10, 1, 1, 0,
        XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &requestorMask);
    const auto requestorCleanup = qScopeGuard([connection, requestor] {
        xcb_destroy_window(connection, requestor);
        xcb_flush(connection);
    });

    // Measured on a real session (M0 Run 8): the conversion with the XFixes event timestamp stalls
    // for the full deadline, while CurrentTime answers immediately. The plan therefore uses
    // CurrentTime here.
    const platform::x11::ConversionPlan plan = platform::x11::conversionPlan(false);
    const platform::x11::ConversionAttempt attempt = plan.attempts[0];
    const int timeoutMs = qMin(limits.timeoutMs, attempt.deadlineMs);

    xcb_convert_selection(
        connection, requestor, m_atomPrimary, m_atomUtf8, m_atomProperty, XCB_CURRENT_TIME);
    xcb_flush(connection);

    const int xcbFd = xcb_get_file_descriptor(connection);
    QElapsedTimer elapsed;
    elapsed.start();

    bool sawNotify = false;
    xcb_atom_t property = XCB_ATOM_NONE;
    while (!m_stopRequested && elapsed.elapsed() < timeoutMs) {
        struct pollfd descriptor = {xcbFd, POLLIN, 0};
        if (::poll(&descriptor, 1, 50) < 0 && errno != EINTR)
            break;
        while (xcb_generic_event_t *event = xcb_poll_for_event(connection)) {
            const uint8_t type = event->response_type & ~0x80;
            if (type == XCB_SELECTION_NOTIFY) {
                const auto *notify = reinterpret_cast<xcb_selection_notify_event_t *>(event);
                if (notify->requestor == requestor && notify->selection == m_atomPrimary
                    && notify->target == m_atomUtf8) {
                    property = notify->property;
                    sawNotify = true;
                }
            } else if (platform::x11::isXfixesSelectionNotify(
                           m_xfixesEventBase, event->response_type))
                handleXfixesEvent(event);
            std::free(event);
        }
        if (seq != m_lastEmittedSeq.load()) {
            callback(seq,
                core::Error {core::ErrorCode::SelectionCancelled, QStringLiteral("superseded")});
            return;
        }
        if (sawNotify)
            break;
        // Keep owner notifications flowing to the UI thread while we wait.
        xcb_flush(connection);
    }

    if (!sawNotify) {
        callback(seq,
            core::Error {core::ErrorCode::SelectionTimeout,
                QStringLiteral("owner did not answer within %1 ms").arg(timeoutMs)});
        return;
    }
    if (property == XCB_ATOM_NONE) {
        callback(seq,
            core::Error {core::ErrorCode::SelectionNotText,
                QStringLiteral("owner refused the UTF8_STRING target")});
        return;
    }

    xcb_get_property_reply_t *reply = xcb_get_property_reply(connection,
        xcb_get_property(
            connection, 0, requestor, m_atomProperty, XCB_ATOM_ANY, 0, maxBytes / 4 + 16),
        nullptr);
    if (!reply) {
        callback(seq,
            core::Error {core::ErrorCode::SelectionNotText, QStringLiteral("GetProperty failed")});
        return;
    }

    QByteArray data;
    if (reply->type == m_atomIncr) {
        std::free(reply);
        // ICCCM INCR transfers start only after the requestor deletes the initial property.
        xcb_delete_property(connection, requestor, m_atomProperty);
        xcb_flush(connection);
        // INCR: chunks arrive as PropertyNotify events until a zero-length chunk.
        for (int round = 0; round < 4096; ++round) {
            bool notified = false;
            while (!m_stopRequested && elapsed.elapsed() < timeoutMs && !notified) {
                struct pollfd descriptor = {xcbFd, POLLIN, 0};
                if (::poll(&descriptor, 1, 50) < 0 && errno != EINTR)
                    break;
                while (xcb_generic_event_t *event = xcb_poll_for_event(connection)) {
                    if ((event->response_type & ~0x80) == XCB_PROPERTY_NOTIFY) {
                        const auto *notify = reinterpret_cast<xcb_property_notify_event_t *>(event);
                        notified = notify->window == requestor && notify->atom == m_atomProperty
                            && notify->state == XCB_PROPERTY_NEW_VALUE;
                    } else if (platform::x11::isXfixesSelectionNotify(
                                   m_xfixesEventBase, event->response_type))
                        handleXfixesEvent(event);
                    std::free(event);
                }
                if (seq != m_lastEmittedSeq.load()) {
                    callback(seq,
                        core::Error {
                            core::ErrorCode::SelectionCancelled, QStringLiteral("superseded")});
                    return;
                }
            }
            if (!notified) {
                callback(seq,
                    core::Error {
                        core::ErrorCode::SelectionTimeout, QStringLiteral("INCR chunk timed out")});
                return;
            }
            xcb_get_property_reply_t *chunk = xcb_get_property_reply(connection,
                xcb_get_property(
                    connection, 1, requestor, m_atomProperty, XCB_ATOM_ANY, 0, maxBytes / 4 + 16),
                nullptr);
            if (!chunk) {
                callback(seq,
                    core::Error {core::ErrorCode::SelectionNotText,
                        QStringLiteral("INCR GetProperty failed")});
                return;
            }
            if (chunk->bytes_after != 0) {
                std::free(chunk);
                callback(seq,
                    core::Error {core::ErrorCode::SelectionTooLarge,
                        QStringLiteral("INCR chunk exceeded the size limit")});
                return;
            }
            const int length = xcb_get_property_value_length(chunk);
            QByteArray piece;
            if (length > 0)
                piece
                    = QByteArray(static_cast<const char *>(xcb_get_property_value(chunk)), length);
            std::free(chunk);
            if (!assembler.append(piece) || assembler.overCap()) {
                callback(seq,
                    core::Error {core::ErrorCode::SelectionTooLarge,
                        QStringLiteral("INCR transfer exceeded the size limit")});
                return;
            }
            if (piece.isEmpty())
                break;
        }
        if (!assembler.finished()) {
            callback(seq,
                core::Error {core::ErrorCode::SelectionNotText,
                    QStringLiteral("INCR transfer exceeded the chunk limit")});
            return;
        }
        data = assembler.data();
    } else {
        if (reply->bytes_after != 0) {
            std::free(reply);
            callback(seq,
                core::Error {core::ErrorCode::SelectionTooLarge,
                    QStringLiteral("selection exceeded the size limit")});
            return;
        }
        const int length = xcb_get_property_value_length(reply);
        if (length > 0)
            data = QByteArray(static_cast<const char *>(xcb_get_property_value(reply)), length);
        std::free(reply);
    }

    if (data.isEmpty()) {
        callback(
            seq, core::Error {core::ErrorCode::SelectionEmpty, QStringLiteral("empty conversion")});
        return;
    }

    selection::SelectionText text;
    text.text = QString::fromUtf8(data);
    if (text.text.size() > limits.maxChars) {
        callback(seq,
            core::Error {core::ErrorCode::SelectionTooLarge,
                QStringLiteral("selection exceeds the size limit")});
        return;
    }
    text.text = text.text.trimmed();
    if (text.text.isEmpty()) {
        callback(seq,
            core::Error {
                core::ErrorCode::SelectionEmpty, QStringLiteral("selection is whitespace")});
        return;
    }
    callback(seq, text);
}
