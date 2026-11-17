// M0: does XFixes PRIMARY ownership notification + conversion actually deliver selection text?
#include <QJsonObject>

#include <vector>

#include "common.h"
#include "x11_common.h"

int runX11Watch(int durationMs, bool showText, const QString &outPath, int maxEvents) {
    probe::x11::Connection c;
    QString error;
    if (!probe::x11::open(c, &error)) {
        probe::line(QStringLiteral("x11: unavailable — %1").arg(error));
        return 2;
    }
    if (!probe::x11::subscribePrimary(c, &error)) {
        probe::line(QStringLiteral("x11: XFixes subscription failed — %1").arg(error));
        probe::x11::close(c);
        return 2;
    }
    probe::line(QStringLiteral("x11: subscribed to PRIMARY ownership notifications "
                               "(XFixes, event base %1)")
                    .arg(c.xfixesEventBase));

    if (showText)
        std::fprintf(stderr,
            "warning: --show-text prints selection content; do not use with sensitive text\n");

    probe::Recorder recorder(outPath);
    probe::Stats resolveStats;
    int events = 0;
    int failures = 0;
    int incrCount = 0;
    int unexpectedEvents = 0;
    std::vector<xcb_xfixes_selection_notify_event_t> deferred;

    probe::line(
        QStringLiteral("x11: watching for %1 ms — select text in any X11/XWayland application")
            .arg(durationMs));

    probe::Stopwatch total;
    while (total.ms() < durationMs && events < maxEvents) {
        if (deferred.empty()) {
            xcb_generic_event_t *event = probe::x11::nextEvent(c, 200);
            if (!event)
                continue;
            if (probe::x11::isXfixesSelectionNotify(c, event)) {
                const auto *notify = reinterpret_cast<xcb_xfixes_selection_notify_event_t *>(event);
                if (notify->owner != XCB_WINDOW_NONE)
                    deferred.push_back(*notify);
            } else if ((event->response_type & ~0x80) != 0) {
                // Anything else is unexpected here; counting it proves whether events arrive at
                // all.
                ++unexpectedEvents;
            }
            std::free(event);
            continue;
        }

        const xcb_xfixes_selection_notify_event_t event = deferred.front();
        deferred.erase(deferred.begin());
        ++events;

        const QString app = probe::x11::ownerProcessName(c, event.owner);
        const probe::x11::ResolvedText resolved
            = probe::x11::resolvePrimary(c, event.timestamp, 4000, &deferred);

        if (resolved.ok) {
            resolveStats.add(resolved.ms);
            incrCount += resolved.incr ? 1 : 0;
            probe::line(
                QStringLiteral("seq=%1 t=%2ms app=%3 chars=%4 digest=%5 incr=%6 resolve=%7ms [%8]")
                    .arg(events)
                    .arg(int(total.ms()))
                    .arg(app)
                    .arg(resolved.text.size())
                    .arg(probe::digest8(resolved.text))
                    .arg(resolved.incr ? "yes" : "no")
                    .arg(resolved.ms, 0, 'f', 1)
                    .arg(resolved.strategy));
            if (showText)
                std::fprintf(stdout, "    text: \"%s\"\n", qPrintable(resolved.text.left(80)));
        } else {
            ++failures;
            probe::line(QStringLiteral("seq=%1 t=%2ms app=%3 FAILED: %4")
                            .arg(events)
                            .arg(int(total.ms()))
                            .arg(app, resolved.error));
        }

        QJsonObject record;
        record[QStringLiteral("backend")] = QStringLiteral("x11-primary");
        record[QStringLiteral("seq")] = events;
        record[QStringLiteral("observe_ms")] = int(total.ms());
        record[QStringLiteral("app")] = app;
        record[QStringLiteral("ok")] = resolved.ok;
        record[QStringLiteral("chars")] = resolved.ok ? resolved.text.size() : 0;
        record[QStringLiteral("digest")] = resolved.ok ? probe::digest8(resolved.text) : QString();
        record[QStringLiteral("incr")] = resolved.incr;
        record[QStringLiteral("resolve_ms")] = resolved.ms;
        record[QStringLiteral("error")] = resolved.error;
        recorder.write(record);
    }

    probe::heading("x11 summary");
    probe::line(QStringLiteral("selection events observed : %1").arg(events));
    probe::line(QStringLiteral("resolved                  : %1").arg(events - failures));
    probe::line(QStringLiteral("failed                    : %1").arg(failures));
    probe::line(QStringLiteral("INCR transfers            : %1").arg(incrCount));
    probe::line(QStringLiteral("other X events seen       : %1").arg(unexpectedEvents));
    probe::line(QStringLiteral("X protocol errors         : %1 (last code %2)")
                    .arg(probe::x11::xErrorCount())
                    .arg(probe::x11::xLastErrorCode()));
    if (resolveStats.count() > 0)
        probe::line(QStringLiteral("resolve ms p50/p95/max    : %1 / %2 / %3")
                        .arg(resolveStats.percentile(50), 0, 'f', 1)
                        .arg(resolveStats.percentile(95), 0, 'f', 1)
                        .arg(resolveStats.max(), 0, 'f', 1));
    if (events == 0)
        probe::line(
            QStringLiteral("no events: nothing was selected, or the source app does not publish "
                           "PRIMARY ownership changes (see docs/design/04-selection.md §7.1)"));
    probe::x11::close(c);
    return (events > 0 && failures == 0) ? 0 : 1;
}
