// Backend construction, kept out of the coordinator so tests can inject fakes (docs/design/04 §4).
#pragma once

#include <QString>

#include <memory>

#include "selection/selection_backend.h"

namespace app {

struct BackendCreation {
    std::unique_ptr<selection::SelectionBackend> backend;
    QString reason;  // why there is no backend, when `backend` is null
};

BackendCreation createSelectionBackend(const QString &backendId);

}  // namespace app
