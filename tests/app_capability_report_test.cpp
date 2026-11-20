// Capability report: pure assembly from probed facts — testable without a desktop.
#include "app/capability_report.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using namespace app;

namespace {

SessionFacts baseFacts() {
    SessionFacts facts;
    facts.sessionType = QStringLiteral("wayland");
    facts.desktop = QStringLiteral("ubuntu:GNOME");
    facts.waylandDisplay = QStringLiteral("wayland-0");
    facts.display = QStringLiteral(":0");
    facts.x11PrimaryAvailable = true;
    facts.sessionBusConnected = true;
    facts.accessibilityBusReachable = true;
    facts.secrets.availability = secrets::Availability::SessionOnly;
    facts.secrets.backend = QStringLiteral("session");
    facts.secrets.reason
        = QStringLiteral("keys are held in memory and forgotten when the application exits");
    facts.portalVersions = {{QStringLiteral("org.freedesktop.portal.Screenshot"), 2},
        {QStringLiteral("org.freedesktop.portal.GlobalShortcuts"), -1}};
    return facts;
}

settings::Settings baseSettings() {
    settings::Settings settings;
    settings.language.target = QStringLiteral("zh-Hans");
    return settings;
}

}  // namespace

class AppCapabilityReportTest : public QObject {
    Q_OBJECT

private slots:
    void waylandWithXwaylandChoosesPrimary();
    void waylandWithoutXwaylandChoosesAtspi();
    void x11OnlySessionChoosesXfices();
    void sessionWithoutCandidatesExplainsThePasteAlternative();
    void explicitBackendThatCannotWorkLeavesNoBackend();
    void notesReportUnexportedPortalsAndCredentialLimitation();
    void jsonCarriesNoUserData();
    void textReportNamesTheBackendAndItsLimits();
};

void AppCapabilityReportTest::waylandWithXwaylandChoosesPrimary() {
    const CapabilityReport report = buildCapabilityReport(baseFacts(), baseSettings());
    QCOMPARE(report.selectionBackend, QStringLiteral("x11-primary"));
    QVERIFY(report.selectionReason.contains(QStringLiteral("ownership")));
    QVERIFY(report.notes.isEmpty() || !report.selectionBackend.isEmpty());
}

void AppCapabilityReportTest::waylandWithoutXwaylandChoosesAtspi() {
    SessionFacts facts = baseFacts();
    facts.x11PrimaryAvailable = false;
    const CapabilityReport report = buildCapabilityReport(facts, baseSettings());
    QCOMPARE(report.selectionBackend, QStringLiteral("atspi"));
}

void AppCapabilityReportTest::x11OnlySessionChoosesXfices() {
    SessionFacts facts = baseFacts();
    facts.sessionType = QStringLiteral("x11");
    facts.waylandDisplay.clear();

    const CapabilityReport report = buildCapabilityReport(facts, baseSettings());
    QCOMPARE(report.selectionBackend, QStringLiteral("x11-primary"));
    QVERIFY(report.selectionReason.contains(QStringLiteral("ownership")));
}

void AppCapabilityReportTest::sessionWithoutCandidatesExplainsThePasteAlternative() {
    SessionFacts facts = baseFacts();
    facts.sessionType = QStringLiteral("wayland");
    facts.waylandDisplay = QStringLiteral("wayland-0");
    facts.display.clear();
    facts.x11PrimaryAvailable = false;
    facts.accessibilityBusReachable = false;

    const CapabilityReport report = buildCapabilityReport(facts, baseSettings());
    QVERIFY(report.selectionBackend.isEmpty());
    QVERIFY(!report.selectionReason.isEmpty());
    bool mentionsPaste = false;
    for (const QString &note : report.notes)
        mentionsPaste = mentionsPaste || note.contains(QStringLiteral("paste"));
    QVERIFY2(mentionsPaste, "the report must name the documented alternative");
}

void AppCapabilityReportTest::explicitBackendThatCannotWorkLeavesNoBackend() {
    settings::Settings settings = baseSettings();
    settings.selection.backend = QStringLiteral("wlr-data-control");

    SessionFacts facts = baseFacts();
    facts.sessionType = QStringLiteral("x11");
    facts.waylandDisplay.clear();

    const CapabilityReport report = buildCapabilityReport(facts, settings);
    QVERIFY(report.selectionBackend.isEmpty());
    QVERIFY(report.selectionReason.contains(QStringLiteral("wlr-data-control")));
}

void AppCapabilityReportTest::notesReportUnexportedPortalsAndCredentialLimitation() {
    const CapabilityReport report = buildCapabilityReport(baseFacts(), baseSettings());
    bool portalNote = false;
    bool credentialNote = false;
    for (const QString &note : report.notes) {
        portalNote = portalNote || note.contains(QStringLiteral("GlobalShortcuts"));
        credentialNote = credentialNote || note.contains(QStringLiteral("session-only"));
    }
    QVERIFY(portalNote);
    QVERIFY(credentialNote);
}

void AppCapabilityReportTest::jsonCarriesNoUserData() {
    const CapabilityReport report = buildCapabilityReport(baseFacts(), baseSettings());
    const QJsonObject json = report.toJson();

    QVERIFY(json.contains(QStringLiteral("session")));
    QVERIFY(json.contains(QStringLiteral("portals")));
    QVERIFY(json.contains(QStringLiteral("selection")));
    QVERIFY(json.contains(QStringLiteral("secrets")));
    QVERIFY(json.contains(QStringLiteral("notes")));

    const QJsonObject session = json.value(QStringLiteral("session")).toObject();
    QCOMPARE(session.value(QStringLiteral("type")).toString(), QStringLiteral("wayland"));
    QVERIFY(session.value(QStringLiteral("accessibilityBusReachable")).toBool());
    QCOMPARE(json.value(QStringLiteral("selection"))
                 .toObject()
                 .value(QStringLiteral("backend"))
                 .toString(),
        QStringLiteral("x11-primary"));
    QCOMPARE(json.value(QStringLiteral("secrets"))
                 .toObject()
                 .value(QStringLiteral("availability"))
                 .toString(),
        QStringLiteral("session-only"));

    // Nothing resembling captured text, a selection or a credential may appear anywhere in the
    // report.
    const QByteArray serialized = QJsonDocument(json).toJson();
    for (const QByteArray &forbidden : {QByteArrayLiteral("sk-"), QByteArrayLiteral("apiKey"),
             QByteArrayLiteral("selectedText")}) {
        QVERIFY2(!serialized.contains(forbidden), forbidden.constData());
    }
}

void AppCapabilityReportTest::textReportNamesTheBackendAndItsLimits() {
    const CapabilityReport report = buildCapabilityReport(baseFacts(), baseSettings());
    const QString text = report.toText();
    QVERIFY(text.contains(QStringLiteral("selection")));
    QVERIFY(text.contains(QStringLiteral("x11-primary")));
    QVERIFY(text.contains(QStringLiteral("session-only")));
    QVERIFY(text.contains(QStringLiteral("GlobalShortcuts")));
}

QTEST_GUILESS_MAIN(AppCapabilityReportTest)
#include "app_capability_report_test.moc"
