// CancellableDelay: restart replaces, cancel prevents, destruction drops, zero is asynchronous.
#include "core/delay.h"

#include <QtTest>

class DelayTest : public QObject {
    Q_OBJECT

private slots:
    void firesOnceAfterTheDelay();
    void restartReplacesThePendingCallback();
    void cancelPreventsTheCallback();
    void zeroStillFiresAsynchronously();
    void destroyedReceiverDoesNotCrash();
    void reportsActivityAndRemainingTime();
};

void DelayTest::firesOnceAfterTheDelay() {
    core::CancellableDelay delay;
    int calls = 0;
    delay.start(30, [&calls] { ++calls; });
    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 2000);
    QTest::qWait(80);
    QCOMPARE(calls, 1);  // single shot, no repeats
}

void DelayTest::restartReplacesThePendingCallback() {
    core::CancellableDelay delay;
    int first = 0;
    int second = 0;
    delay.start(40, [&first] { ++first; });
    delay.start(120, [&second] { ++second; });

    QTest::qWait(90);  // past the first deadline, before the second
    QCOMPARE(first, 0);
    QCOMPARE(second, 0);

    QTRY_COMPARE_WITH_TIMEOUT(second, 1, 2000);
    QCOMPARE(first, 0);
}

void DelayTest::cancelPreventsTheCallback() {
    core::CancellableDelay delay;
    int calls = 0;
    delay.start(30, [&calls] { ++calls; });
    delay.cancel();
    QTest::qWait(120);
    QCOMPARE(calls, 0);
    QVERIFY(!delay.isActive());
}

void DelayTest::zeroStillFiresAsynchronously() {
    core::CancellableDelay delay;
    int calls = 0;
    delay.start(0, [&calls] { ++calls; });
    // The contract is asynchrony: a zero delay must not run the callback inside start().
    QCOMPARE(calls, 0);
    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 1000);
}

void DelayTest::destroyedReceiverDoesNotCrash() {
    int calls = 0;
    {
        core::CancellableDelay delay;
        delay.start(200, [&calls] { ++calls; });
    }  // destroyed before firing
    QTest::qWait(300);
    QCOMPARE(calls, 0);
}

void DelayTest::reportsActivityAndRemainingTime() {
    core::CancellableDelay delay;
    QVERIFY(!delay.isActive());
    QCOMPARE(delay.remainingMs(), -1);

    delay.start(500, [] {});
    QVERIFY(delay.isActive());
    const int remaining = delay.remainingMs();
    QVERIFY2(remaining > 0 && remaining <= 500, qPrintable(QString::number(remaining)));

    delay.cancel();
    QVERIFY(!delay.isActive());
}

QTEST_GUILESS_MAIN(DelayTest)
#include "core_delay_test.moc"
