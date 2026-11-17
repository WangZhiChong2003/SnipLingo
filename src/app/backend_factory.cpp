#include "app/backend_factory.h"

#ifdef SNIPLYNGO_HAVE_ATSPI
#include "platform/atspi/atspi_backend.h"
#endif
#ifdef SNIPLYNGO_HAVE_X11
#include "platform/x11/x11_backend.h"
#endif

namespace app {

BackendCreation createSelectionBackend(const QString &backendId) {
    if (backendId == QLatin1String("atspi")) {
#ifdef SNIPLYNGO_HAVE_ATSPI
        return {std::make_unique<AtspiBackend>(), QString()};
#else
        return {nullptr,
            QStringLiteral("this build has no AT-SPI support (libatspi2.0-dev was not "
                           "available at configure time)")};
#endif
    }
    if (backendId == QLatin1String("x11-primary")) {
#ifdef SNIPLYNGO_HAVE_X11
        return {std::make_unique<X11Backend>(), QString()};
#else
        return {nullptr,
            QStringLiteral("this build has no X11 backend (libxcb-xfixes0-dev was not available at "
                           "configure time)")};
#endif
    }
    if (backendId == QLatin1String("wlr-data-control")) {
        return {nullptr,
            QStringLiteral("backend %1 is designed but not implemented in this build")
                .arg(backendId)};
    }
    return {nullptr, QStringLiteral("unknown backend id: %1").arg(backendId)};
}

}  // namespace app
