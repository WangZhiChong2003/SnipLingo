#include "app/capability_report.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace app {
namespace {

QString availabilityName(secrets::Availability availability) {
    switch (availability) {
    case secrets::Availability::Available:
        return QStringLiteral("available");
    case secrets::Availability::SessionOnly:
        return QStringLiteral("session-only");
    case secrets::Availability::Unavailable:
        return QStringLiteral("unavailable");
    }
    return QStringLiteral("unknown");
}

}  // namespace

CapabilityReport buildCapabilityReport(
    const SessionFacts &facts, const settings::Settings &settings) {
    CapabilityReport report;
    report.facts = facts;

    report.selectionSession.waylandSession
        = facts.sessionType == QLatin1String("wayland") || !facts.waylandDisplay.isEmpty();
    report.selectionSession.x11Session = !facts.display.isEmpty();
    report.selectionSession.x11PrimaryAvailable = facts.x11PrimaryAvailable;
    report.selectionSession.accessibilityBusReachable = facts.accessibilityBusReachable;
    // Data-control globals are only knowable with a live Wayland connection; until the backend
    // probes them, they are reported as unknown rather than assumed.
    report.selectionSession.waylandDataControlGlobals = {};
    report.configuredBackend = settings.selection.backend;

    const core::Result<QString> chosen
        = selection::BackendSelector::choose(report.selectionSession, settings.selection.backend);
    if (chosen.ok()) {
        report.selectionBackend = chosen.value();
        report.selectionReason
            = selection::BackendSelector::explain(report.selectionSession, report.selectionBackend);
    } else {
        report.selectionReason = chosen.error().detail;
    }

    for (auto it = facts.portalVersions.constBegin(); it != facts.portalVersions.constEnd(); ++it) {
        if (it->second < 0)
            report.notes.append(
                QStringLiteral("%1 is not exported by this desktop").arg(it->first));
    }
    if (facts.secrets.availability != secrets::Availability::Available) {
        report.notes.append(
            QStringLiteral("credentials are %1: %2")
                .arg(availabilityName(facts.secrets.availability), facts.secrets.reason));
    }
    if (!facts.settingsIssues.isEmpty()) {
        report.notes.append(QStringLiteral("settings fields were adjusted: %1")
                                .arg(facts.settingsIssues.join(QStringLiteral(", "))));
    }
    if (report.selectionBackend == QLatin1String("x11-primary")) {
        report.notes.append(
            QStringLiteral("an X11 display is advertised by DISPLAY; whether a given "
                           "application publishes PRIMARY changes stays unverified until "
                           "it is tested"));
    }
    if (report.selectionBackend.isEmpty()) {
        report.notes.append(
            QStringLiteral("automatic selection is unavailable; the explicit paste flow is "
                           "the documented alternative"));
    }
    return report;
}

QJsonObject CapabilityReport::toJson() const {
    QJsonObject session;
    session[QStringLiteral("type")] = facts.sessionType;
    session[QStringLiteral("desktop")] = facts.desktop;
    session[QStringLiteral("waylandDisplayPresent")] = !facts.waylandDisplay.isEmpty();
    session[QStringLiteral("displayPresent")] = !facts.display.isEmpty();
    session[QStringLiteral("sessionBusConnected")] = facts.sessionBusConnected;
    session[QStringLiteral("accessibilityBusReachable")] = facts.accessibilityBusReachable;
    session[QStringLiteral("secretServiceRegistered")] = facts.secretServiceRegistered;

    QJsonObject portals;
    for (auto it = facts.portalVersions.constBegin(); it != facts.portalVersions.constEnd(); ++it)
        portals[it->first] = it->second;

    QJsonObject selectionReport;
    selectionReport[QStringLiteral("backend")] = selectionBackend;
    selectionReport[QStringLiteral("reason")] = selectionReason;
    selectionReport[QStringLiteral("configured")] = configuredBackend;

    QJsonObject secretsReport;
    secretsReport[QStringLiteral("availability")] = availabilityName(facts.secrets.availability);
    secretsReport[QStringLiteral("backend")] = facts.secrets.backend;
    secretsReport[QStringLiteral("reason")] = facts.secrets.reason;

    QJsonArray noteArray;
    for (const QString &note : notes)
        noteArray.append(note);

    QJsonObject root;
    root[QStringLiteral("session")] = session;
    root[QStringLiteral("portals")] = portals;
    root[QStringLiteral("selection")] = selectionReport;
    root[QStringLiteral("secrets")] = secretsReport;
    root[QStringLiteral("notes")] = noteArray;
    return root;
}

QString CapabilityReport::toText() const {
    QString text;
    const auto line = [&text](const QString &content) { text += content + QLatin1Char('\n'); };

    line(QStringLiteral("session"));
    line(QStringLiteral("  type                : %1")
             .arg(facts.sessionType.isEmpty() ? QStringLiteral("<unknown>") : facts.sessionType));
    line(QStringLiteral("  desktop             : %1").arg(facts.desktop));
    line(QStringLiteral("  session bus         : %1")
             .arg(facts.sessionBusConnected ? QStringLiteral("connected")
                                            : QStringLiteral("unreachable")));
    line(QStringLiteral("  accessibility bus   : %1")
             .arg(facts.accessibilityBusReachable ? QStringLiteral("reachable")
                                                  : QStringLiteral("unreachable")));
    line(QString());

    line(QStringLiteral("selection"));
    line(QStringLiteral("  backend             : %1")
             .arg(selectionBackend.isEmpty() ? QStringLiteral("<none>") : selectionBackend));
    line(QStringLiteral("  configured          : %1").arg(configuredBackend));
    line(QStringLiteral("  note                : %1").arg(selectionReason));
    line(QString());

    line(QStringLiteral("credentials"));
    line(QStringLiteral("  storage             : %1")
             .arg(availabilityName(facts.secrets.availability)));
    line(QStringLiteral("  backend             : %1").arg(facts.secrets.backend));
    line(QString());

    line(QStringLiteral("portals"));
    for (auto it = facts.portalVersions.constBegin(); it != facts.portalVersions.constEnd(); ++it) {
        line(
            QStringLiteral("  %1 : %2")
                .arg(it->first.leftJustified(42),
                    it->second < 0 ? QStringLiteral("not exported") : QString::number(it->second)));
    }
    if (facts.portalVersions.isEmpty())
        line(QStringLiteral("  <no portal information; session bus unreachable>"));

    if (!notes.isEmpty()) {
        line(QString());
        line(QStringLiteral("notes"));
        for (const QString &note : notes)
            line(QStringLiteral("  - %1").arg(note));
    }
    return text;
}

}  // namespace app
