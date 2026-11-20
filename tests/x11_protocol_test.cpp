// X11 extension event matching, conversion strategy and INCR accounting.
#include "platform/x11/x11_protocol.h"

#include <QCoreApplication>
#include <QtTest>

using namespace platform::x11;

class X11ProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void xfixesEventMatching();
    void conversionPlanPrefersCurrentTimeWhenNoTimestamp();
    void conversionPlanCapsTheFirstAttempt();
    void incrAssemblerEnforcesTheCap();
    void incrAssemblerCompletesOnEmptyChunk();
    void processNameForUnknownPidIsEmpty();
};

void X11ProtocolTest::xfixesEventMatching() {
    const uint8_t base = 86;
    // The subtype constant is 0, so the wire type equals the extension's first event.
    QVERIFY(isXfixesSelectionNotify(base, base));
    QVERIFY(!isXfixesSelectionNotify(base, 0));  // comparing the bare subtype matches nothing
    QVERIFY(!isXfixesSelectionNotify(base, base + 1));
    QVERIFY(!isXfixesSelectionNotify(0, 0));  // unknown extension: never claim a match
}

void X11ProtocolTest::conversionPlanPrefersCurrentTimeWhenNoTimestamp() {
    const ConversionPlan plan = conversionPlan(false);
    QCOMPARE(plan.count, 1);
    QVERIFY(plan.attempts[0].useCurrentTime);
    QVERIFY(plan.attempts[0].deadlineMs >= 1000);
}

void X11ProtocolTest::conversionPlanCapsTheFirstAttempt() {
    const ConversionPlan plan = conversionPlan(true);
    QCOMPARE(plan.count, 2);
    QVERIFY(!plan.attempts[0].useCurrentTime);
    QVERIFY(
        plan.attempts[0].deadlineMs <= 300);  // a stalled owner must not block for a full timeout
    QVERIFY(plan.attempts[1].useCurrentTime);
    QVERIFY(plan.attempts[1].deadlineMs > plan.attempts[0].deadlineMs);
}

void X11ProtocolTest::incrAssemblerEnforcesTheCap() {
    IncrAssembler assembler(10);
    QVERIFY(assembler.append(QByteArray("12345")));
    QVERIFY(assembler.append(QByteArray("67890")));
    QVERIFY(!assembler.append(QByteArray("x")));
    QVERIFY(assembler.overCap());
    QCOMPARE(assembler.data().size(), 10);
}

void X11ProtocolTest::incrAssemblerCompletesOnEmptyChunk() {
    IncrAssembler assembler(64);
    QVERIFY(assembler.append(QByteArray("abc")));
    QVERIFY(!assembler.finished());
    QVERIFY(assembler.append(QByteArray()));
    QVERIFY(assembler.finished());
    QCOMPARE(assembler.data(), QByteArray("abc"));
}

void X11ProtocolTest::processNameForUnknownPidIsEmpty() {
    QVERIFY(processNameForPid(0).isEmpty());
    QVERIFY(processNameForPid(0xFFFFFFFFu).isEmpty());
}

QTEST_GUILESS_MAIN(X11ProtocolTest)
#include "x11_protocol_test.moc"
