// Capability report: what this session actually offers, assembled without touching user data.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "secrets/secret_store.h"
#include "selection/backend_selector.h"
#include "settings/settings.h"

namespace app {

struct SessionFacts {
    QString sessionType;  // XDG_SESSION_TYPE
    QString desktop;  // XDG_CURRENT_DESKTOP
    QString waylandDisplay;  // present or empty
    QString display;  // present or empty
    bool x11PrimaryAvailable = false;
    bool sessionBusConnected = false;
    bool accessibilityBusReachable = false;
    bool secretServiceRegistered = false;
    QList<QPair<QString, int>> portalVersions;  // interface -> version, -1 when not exported
    secrets::AvailabilityReport secrets;
    QStringList settingsIssues;
};

struct CapabilityReport {
    SessionFacts facts;
    selection::SelectionSession selectionSession;
    QString configuredBackend;  // what settings asked for ("auto" or an explicit id)
    QString selectionBackend;  // empty when no backend is available
    QString selectionReason;  // explanation or failure reason, never user data
    QStringList notes;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] QString toText() const;
};

CapabilityReport buildCapabilityReport(
    const SessionFacts &facts, const settings::Settings &settings);

}  // namespace app
