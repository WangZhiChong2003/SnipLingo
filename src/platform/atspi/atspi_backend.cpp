#include "platform/atspi/atspi_backend.h"

#include "core/log.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QMutexLocker>

#include <atspi/atspi.h>
#include <glib.h>
#include <memory>

namespace {

QString takeGError(GError **error) {
    if (!error || !*error)
        return {};
    const QString message = QString::fromUtf8((*error)->message);
    g_error_free(*error);
    *error = nullptr;
    return message;
}

QString roleNameOf(AtspiAccessible *accessible) {
    GError *error = nullptr;
    gchar *role = atspi_accessible_get_role_name(accessible, &error);
    takeGError(&error);
    const QString name = role ? QString::fromUtf8(role) : QString();
    g_free(role);
    return name;
}

QString appNameOf(AtspiAccessible *accessible) {
    GError *error = nullptr;
    AtspiAccessible *application = atspi_accessible_get_application(accessible, &error);
    takeGError(&error);
    if (!application)
        return {};
    gchar *name = atspi_accessible_get_name(application, &error);
    takeGError(&error);
    const QString appName = name ? QString::fromUtf8(name) : QString();
    g_free(name);
    g_object_unref(application);
    return appName;
}

bool accessibilityBusReachable(QString *reason) {
    // atspi_init() aborts the process when the bus is unreachable (at-spi2-core's dbind), so the
    // bus must be pre-checked. Learned the hard way in M0.
    if (!QDBusConnection::sessionBus().isConnected()) {
        *reason = QStringLiteral("session bus unreachable");
        return false;
    }
    const QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.a11y.Bus"),
        QStringLiteral("/org/a11y/bus"), QStringLiteral("org.a11y.Bus"),
        QStringLiteral("GetAddress"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()
        || reply.arguments().first().toString().isEmpty()) {
        *reason = reply.errorMessage().isEmpty()
            ? QStringLiteral("org.a11y.Bus.GetAddress returned nothing")
            : reply.errorMessage();
        return false;
    }
    return true;
}

struct LoopTask {
    std::function<void()> work;
    std::shared_ptr<QSemaphore> done;
};

gboolean runLoopTask(gpointer data) {
    auto *task = static_cast<LoopTask *>(data);
    if (task->work)
        task->work();
    if (task->done)
        task->done->release();
    delete task;
    return G_SOURCE_REMOVE;
}

void onSelectionChanged(AtspiEvent *event, void *userData) {
    static_cast<AtspiBackend *>(userData)->handleAccessibleEvent(event);
}

}  // namespace

AtspiBackend::AtspiBackend(QObject *parent)
    : selection::SelectionBackend(parent) {
    m_policy.ownAppName = QCoreApplication::applicationName();
}

AtspiBackend::~AtspiBackend() {
    unsubscribe();
    if (!m_thread)
        return;
    // atspi_event_quit must run inside its GLib event loop on older at-spi2 releases.
    runInEventLoop(
        [this] {
            deregisterListener();
            {
                QMutexLocker locker(&m_cacheMutex);
                if (m_cachedSource) {
                    g_object_unref(static_cast<AtspiAccessible *>(m_cachedSource));
                    m_cachedSource = nullptr;
                }
            }
            atspi_event_quit();
        },
        false);
    if (m_thread->wait(5000))
        delete m_thread;
    else
        core::log::write(core::log::Level::Warning, "selection",
            QStringLiteral("AT-SPI event thread did not stop within the deadline"));
    m_thread = nullptr;
}

void AtspiBackend::setOwnAppName(const QString &name) {
    m_policy.ownAppName = name;
}

selection::BackendProbe AtspiBackend::probe() {
    selection::BackendProbe probe;
    probe.identifier = backendId();
    probe.humanName = QStringLiteral("AT-SPI accessibility events");

    QString reason;
    const bool reachable = accessibilityBusReachable(&reason);
    probe.interfacePresent = reachable;
    probe.accessGranted = reachable;
    probe.reason = reachable
        ? QStringLiteral(
            "accessibility bus is reachable; per-application coverage is measured, not assumed")
        : reason;
    return probe;
}

core::Result<void> AtspiBackend::subscribe() {
    if (m_state == selection::BackendState::Running)
        return {};

    QString reason;
    if (!accessibilityBusReachable(&reason)) {
        m_state = selection::BackendState::Denied;
        emit stateChanged(m_state, reason);
        return core::Error {core::ErrorCode::SelectionBackendDenied, reason};
    }

    // atspi_init() returns 0 on success and non-zero when the library is already initialised, so it
    // is called only once per process: pause/resume re-subscribes without retiring libatspi.
    if (!atspi_is_initialized() && atspi_init() != 0) {
        m_state = selection::BackendState::Failed;
        const QString detail = QStringLiteral("atspi_init failed");
        emit stateChanged(m_state, detail);
        return core::Error {core::ErrorCode::SelectionBackendFailed, detail};
    }

    if (!m_thread) {
        while (m_ready.tryAcquire(1)) { /* drain stale permits */
        }
        m_thread = QThread::create([this] { workerLoop(); });
        m_thread->start();
        if (!m_ready.tryAcquire(1, 3000)) {
            m_state = selection::BackendState::Failed;
            emit stateChanged(m_state, QStringLiteral("event loop did not start"));
            return core::Error {core::ErrorCode::SelectionBackendFailed,
                QStringLiteral("AT-SPI event loop did not start within 3 s")};
        }
    }

    {
        QMutexLocker locker(&m_cacheMutex);
        m_cachedSeq.reset();
    }
    m_observing = true;
    QString registrationError;
    if (!registerListener(&registrationError)) {
        m_observing = false;
        m_state = selection::BackendState::Failed;
        emit stateChanged(m_state, registrationError);
        return core::Error {core::ErrorCode::SelectionBackendFailed,
            QStringLiteral("AT-SPI RegisterEvent failed: %1").arg(registrationError)};
    }
    m_state = selection::BackendState::Running;
    emit stateChanged(m_state, QString());
    return {};
}

void AtspiBackend::workerLoop() {
    m_ready.release();

    // Runs until atspi_event_quit(); registration and deregistration are scheduled into this loop,
    // because libatspi must not be touched from another thread.
    atspi_event_main();
}

bool AtspiBackend::runInEventLoop(std::function<void()> fn, bool waitForCompletion) {
    auto done = waitForCompletion ? std::make_shared<QSemaphore>() : nullptr;
    auto *task = new LoopTask {std::move(fn), done};
    // g_main_context_invoke() may call inline on the UI thread when it can acquire the default
    // context. Attaching a source ensures the work is dispatched by the event loop instead.
    GSource *source = g_idle_source_new();
    g_source_set_callback(source, runLoopTask, task, nullptr);
    g_source_attach(source, nullptr);
    g_source_unref(source);
    return !done || done->tryAcquire(1, 3000);
}

bool AtspiBackend::registerListener(QString *reason) {
    struct Registration {
        bool registered = false;
        QString error;
    };

    const auto result = std::make_shared<Registration>();
    const bool completed = runInEventLoop(
        [this, result] {
            if (m_listener) {
                result->registered = true;
                return;
            }
            GError *error = nullptr;
            auto *listener = atspi_event_listener_new(&onSelectionChanged, this, nullptr);
            result->registered = listener
                && atspi_event_listener_register(listener, "object:text-selection-changed", &error);
            if (result->registered) {
                m_listener = listener;
            } else {
                result->error = takeGError(&error);
                if (listener)
                    g_object_unref(listener);
            }
        },
        true);
    if (reason)
        *reason = completed ? result->error : QStringLiteral("registration timed out");
    return completed && result->registered;
}

void AtspiBackend::deregisterListener() {
    if (!m_listener)
        return;
    GError *error = nullptr;
    const bool removed = atspi_event_listener_deregister(
        static_cast<AtspiEventListener *>(m_listener), "object:text-selection-changed", &error);
    const QString detail = takeGError(&error);
    if (!removed)
        core::log::write(core::log::Level::Warning, "selection",
            QStringLiteral("could not remove AT-SPI selection listener: %1").arg(detail));
    g_object_unref(static_cast<AtspiEventListener *>(m_listener));
    m_listener = nullptr;
}

void AtspiBackend::unsubscribe() {
    if (m_state == selection::BackendState::Stopped
        || m_state == selection::BackendState::Uninitialised)
        return;

    m_observing = false;
    if (m_thread) {
        // Do not block the UI on the accessibility bus. If observation has already resumed by
        // the time this runs, keep the existing listener for the resumed subscription.
        runInEventLoop(
            [this] {
                if (!m_observing)
                    deregisterListener();
            },
            false);
    }
    {
        QMutexLocker locker(&m_cacheMutex);
        m_cachedSeq.reset();
    }
    m_state = selection::BackendState::Stopped;
    emit stateChanged(m_state, QString());
}

void AtspiBackend::handleAccessibleEvent(void *atspiEvent) {
    auto *event = static_cast<AtspiEvent *>(atspiEvent);
    if (!event || !event->source)
        return;

    if (!m_observing)
        return;  // paused: nothing is read, nothing is forwarded

    AtspiAccessible *source = event->source;
    if (platform::atspi::isIgnorableRole(roleNameOf(source)))
        return;

    const QString appName = appNameOf(source);
    if (platform::atspi::isOwnApplication(appName, m_policy.ownAppName))
        return;

    AtspiText *text = atspi_accessible_get_text_iface(source);
    if (!text)
        return;
    g_object_unref(text);
    publishSelection(appName, source);
}

core::Result<selection::SelectionText> AtspiBackend::readSelection(
    void *accessible, selection::ResolveLimits limits) {
    auto *source = static_cast<AtspiAccessible *>(accessible);
    GError *error = nullptr;
    AtspiText *text = atspi_accessible_get_text_iface(source);
    if (!text)
        return core::Error {
            core::ErrorCode::SelectionNotText, QStringLiteral("text interface unavailable")};
    QStringList rangeTexts;
    std::optional<QRect> anchor;
    const int selections = atspi_text_get_n_selections(text, &error);
    if (error) {
        const QString detail = takeGError(&error);
        g_object_unref(text);
        return core::Error {core::ErrorCode::SelectionNotText,
            QStringLiteral("cannot query the selection: %1").arg(detail)};
    }
    for (int i = 0; i < selections; ++i) {
        AtspiRange *range = atspi_text_get_selection(text, i, &error);
        if (!range) {
            const QString detail = takeGError(&error);
            g_object_unref(text);
            return core::Error {core::ErrorCode::SelectionNotText,
                QStringLiteral("cannot read the selection range: %1").arg(detail)};
        }
        const int start = range->start_offset;
        const int end = range->end_offset;
        g_free(range);
        if (end <= start)
            continue;  // caret movement, not a selection

        gchar *chunk = atspi_text_get_text(text, start, end, &error);
        if (!chunk) {
            const QString detail = takeGError(&error);
            g_object_unref(text);
            return core::Error {core::ErrorCode::SelectionNotText,
                QStringLiteral("cannot read selected text: %1").arg(detail)};
        }
        rangeTexts.append(QString::fromUtf8(chunk));
        g_free(chunk);

        if (!anchor) {
            AtspiRect *rect
                = atspi_text_get_range_extents(text, start, end, ATSPI_COORD_TYPE_SCREEN, &error);
            if (rect) {
                anchor = QRect(rect->x, rect->y, rect->width, rect->height);
                g_free(rect);
            } else {
                takeGError(&error);
            }
        }
    }
    g_object_unref(text);
    takeGError(&error);

    core::Result<selection::SelectionText> composed
        = platform::atspi::composeSelection(rangeTexts, anchor, m_policy);
    if (composed.ok() && composed.value().text.size() > limits.maxChars)
        return core::Error {core::ErrorCode::SelectionTooLarge,
            QStringLiteral("%1 characters exceed the %2 limit")
                .arg(composed.value().text.size())
                .arg(limits.maxChars)};
    return composed;
}

void AtspiBackend::publishSelection(const QString &appName, void *accessible) {
    const quint64 seq = m_nextSeq++;
    {
        QMutexLocker locker(&m_cacheMutex);
        if (m_cachedSource)
            g_object_unref(static_cast<AtspiAccessible *>(m_cachedSource));
        m_cachedSource = g_object_ref(static_cast<AtspiAccessible *>(accessible));
        m_cachedSeq = seq;
    }

    selection::SelectionEvent event;
    event.seq = seq;
    event.source.appName = appName;
    event.source.backendId = backendId();
    event.observedAt = QDateTime::currentDateTimeUtc();
    emit eventObserved(event);  // delivered queued to receivers on the UI thread
}

void AtspiBackend::resolveLatest(
    quint64 seq, selection::ResolveLimits limits, selection::ResolveCallback callback) {
    runInEventLoop(
        [this, seq, limits, callback = std::move(callback)] {
            AtspiAccessible *source = nullptr;
            {
                QMutexLocker locker(&m_cacheMutex);
                if (m_observing && m_cachedSeq.has_value() && *m_cachedSeq == seq && m_cachedSource)
                    source = static_cast<AtspiAccessible *>(g_object_ref(m_cachedSource));
            }
            core::Result<selection::SelectionText> result = source
                ? readSelection(source, limits)
                : core::Result<selection::SelectionText>(core::Error {
                    core::ErrorCode::SelectionCancelled, QStringLiteral("superseded")});
            if (source)
                g_object_unref(source);
            QMetaObject::invokeMethod(
                this,
                [seq, callback, result = std::move(result)]() mutable {
                    callback(seq, std::move(result));
                },
                Qt::QueuedConnection);
        },
        false);
}

void AtspiBackend::cancelPending() {
    // A superseded request is discarded by the coordinator's generation check.
}
