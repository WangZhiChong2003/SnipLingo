#include "selection/backend_selector.h"

#include "core/error.h"

namespace selection {
namespace {

constexpr const char *kAtspi = "atspi";
constexpr const char *kX11Primary = "x11-primary";
constexpr const char *kWlrDataControl = "wlr-data-control";

bool dataControlAvailable(const SelectionSession &session) {
    return !session.waylandDataControlGlobals.isEmpty();
}

}  // namespace

core::Result<QString> BackendSelector::choose(
    const SelectionSession &session, const QString &configured) {
    if (configured != QLatin1String("auto")) {
        // An explicit choice is honoured or reported; silently using something else would make the
        // capability report a lie.
        if (configured == QLatin1String(kAtspi)) {
            if (session.accessibilityBusReachable)
                return QString::fromLatin1(kAtspi);
            return core::Error {core::ErrorCode::SelectionBackendAbsent,
                QStringLiteral("atspi: accessibility bus is not reachable")};
        }
        if (configured == QLatin1String(kX11Primary)) {
            if (session.x11PrimaryAvailable)
                return QString::fromLatin1(kX11Primary);
            return core::Error {core::ErrorCode::SelectionBackendAbsent,
                QStringLiteral("x11-primary: XFixes PRIMARY is unavailable")};
        }
        if (configured == QLatin1String(kWlrDataControl)) {
            if (dataControlAvailable(session))
                return QString::fromLatin1(kWlrDataControl);
            return core::Error {core::ErrorCode::SelectionBackendDenied,
                QStringLiteral("wlr-data-control: global not advertised to this client")};
        }
        return core::Error {core::ErrorCode::SelectionBackendAbsent,
            QStringLiteral("unknown backend id: %1").arg(configured)};
    }

    // XWayland can expose primary selections from both X11 and native Wayland applications.
    // Its coverage still depends on the compositor and source application.
    if (session.x11PrimaryAvailable)
        return QString::fromLatin1(kX11Primary);
    if (session.accessibilityBusReachable)
        return QString::fromLatin1(kAtspi);
    if (dataControlAvailable(session))
        return QString::fromLatin1(kWlrDataControl);

    return core::Error {core::ErrorCode::SelectionBackendAbsent,
        QStringLiteral("no selection backend available in this session")};
}

QString BackendSelector::explain(const SelectionSession &session, const QString &chosen) {
    if (chosen == QLatin1String(kAtspi)) {
        return QStringLiteral(
            "AT-SPI accessibility events; per-application coverage varies and is measured, "
            "not assumed");
    }
    if (chosen == QLatin1String(kX11Primary)) {
        return QStringLiteral(
            "X11 PRIMARY ownership notifications; applications that do not publish "
            "ownership changes are invisible to it");
    }
    if (chosen == QLatin1String(kWlrDataControl)) {
        return QStringLiteral("compositor data-control primary selection (%1 global advertised)")
            .arg(session.waylandDataControlGlobals.join(QLatin1String(", ")));
    }
    return QStringLiteral("no backend");
}

}  // namespace selection
