// Backend choice policy: derived from probed facts, never from desktop names.
#include <QtTest>

#include "selection/backend_selector.h"

using selection::BackendSelector;
using selection::SelectionSession;

class SelectionSelectorTest : public QObject {
    Q_OBJECT

private slots:
    void x11SessionPrefersXficesPrimary();
    void waylandPrefersXwaylandWhenAvailable();
    void waylandUsesAtspiWhenXwaylandIsUnavailable();
    void waylandFallsBackToDataControl();
    void waylandWithoutCandidatesReportsAbsent();
    void explicitChoiceIsHonouredOrReported();
    void explicitUnknownIdIsReported();
    void explainNeverClaimsUniversalCoverage();
};

void SelectionSelectorTest::x11SessionPrefersXficesPrimary() {
    SelectionSession session;
    session.x11Session = true;
    session.x11PrimaryAvailable = true;
    session.accessibilityBusReachable = true;
    const auto chosen = BackendSelector::choose(session, QStringLiteral("auto"));
    QVERIFY(chosen.ok());
    QCOMPARE(chosen.value(), QStringLiteral("x11-primary"));
}

void SelectionSelectorTest::waylandPrefersXwaylandWhenAvailable() {
    SelectionSession session;
    session.waylandSession = true;
    session.x11Session = true;  // XWayland present as well
    session.x11PrimaryAvailable = true;
    session.accessibilityBusReachable = true;
    session.waylandDataControlGlobals = {QStringLiteral("zwlr_data_control_manager_v1")};

    const auto chosen = BackendSelector::choose(session, QStringLiteral("auto"));
    QVERIFY(chosen.ok());
    QCOMPARE(chosen.value(), QStringLiteral("x11-primary"));
}

void SelectionSelectorTest::waylandUsesAtspiWhenXwaylandIsUnavailable() {
    SelectionSession session;
    session.waylandSession = true;
    session.x11Session = true;  // DISPLAY alone does not prove XFixes is usable
    session.accessibilityBusReachable = true;
    const auto chosen = BackendSelector::choose(session, QStringLiteral("auto"));
    QVERIFY(chosen.ok());
    QCOMPARE(chosen.value(), QStringLiteral("atspi"));
}

void SelectionSelectorTest::waylandFallsBackToDataControl() {
    SelectionSession session;
    session.waylandSession = true;
    session.waylandDataControlGlobals = {QStringLiteral("ext_data_control_manager_v1")};

    const auto chosen = BackendSelector::choose(session, QStringLiteral("auto"));
    QVERIFY(chosen.ok());
    QCOMPARE(chosen.value(), QStringLiteral("wlr-data-control"));
}

void SelectionSelectorTest::waylandWithoutCandidatesReportsAbsent() {
    SelectionSession session;
    session.waylandSession = true;
    const auto chosen = BackendSelector::choose(session, QStringLiteral("auto"));
    QVERIFY(!chosen.ok());
    QCOMPARE(chosen.error().code, core::ErrorCode::SelectionBackendAbsent);
}

void SelectionSelectorTest::explicitChoiceIsHonouredOrReported() {
    SelectionSession wayland;
    wayland.waylandSession = true;
    wayland.accessibilityBusReachable = true;

    const auto atspi = BackendSelector::choose(wayland, QStringLiteral("atspi"));
    QVERIFY(atspi.ok());
    QCOMPARE(atspi.value(), QStringLiteral("atspi"));

    // Explicitly asking for a backend this session cannot use must fail, not silently switch.
    const auto x11 = BackendSelector::choose(wayland, QStringLiteral("x11-primary"));
    QVERIFY(!x11.ok());
    QCOMPARE(x11.error().code, core::ErrorCode::SelectionBackendAbsent);

    const auto dataControl = BackendSelector::choose(wayland, QStringLiteral("wlr-data-control"));
    QVERIFY(!dataControl.ok());
    QCOMPARE(dataControl.error().code, core::ErrorCode::SelectionBackendDenied);

    SelectionSession noBus;
    noBus.waylandSession = true;
    const auto unavailable = BackendSelector::choose(noBus, QStringLiteral("atspi"));
    QVERIFY(!unavailable.ok());
}

void SelectionSelectorTest::explicitUnknownIdIsReported() {
    SelectionSession session;
    session.x11Session = true;
    const auto chosen = BackendSelector::choose(session, QStringLiteral("telepathy"));
    QVERIFY(!chosen.ok());
    QCOMPARE(chosen.error().code, core::ErrorCode::SelectionBackendAbsent);
}

void SelectionSelectorTest::explainNeverClaimsUniversalCoverage() {
    SelectionSession session;
    session.waylandSession = true;
    session.accessibilityBusReachable = true;

    const QString atspi = BackendSelector::explain(session, QStringLiteral("atspi"));
    QVERIFY(atspi.contains(QStringLiteral("coverage")));
    QVERIFY(!atspi.contains(QStringLiteral("works everywhere")));

    const QString x11 = BackendSelector::explain(session, QStringLiteral("x11-primary"));
    QVERIFY(x11.contains(QStringLiteral("ownership")));
}

QTEST_GUILESS_MAIN(SelectionSelectorTest)
#include "selection_selector_test.moc"
