// The generation token is what makes stale async completions harmless
// (docs/design/05-coordinator.md §6).
#include "core/generation.h"

#include <QtTest>

class GenerationTest : public QObject {
    Q_OBJECT

private slots:
    void startsInvalidAndWithoutGeneration();
    void nextIsMonotonicAndNeverZero();
    void onlyTheLatestGenerationIsCurrent();
    void invalidateMakesEverythingStale();
    void nextAfterInvalidateIsCurrentAgain();
};

void GenerationTest::startsInvalidAndWithoutGeneration() {
    core::GenerationSource source;
    QCOMPARE(source.current(), core::Generation {0});
    QVERIFY(!source.valid());
    QVERIFY(!source.isCurrent(0));
    QVERIFY(!source.isCurrent(1));
}

void GenerationTest::nextIsMonotonicAndNeverZero() {
    core::GenerationSource source;
    core::Generation previous = 0;
    for (int i = 0; i < 1000; ++i) {
        const core::Generation current = source.next();
        QVERIFY(current != 0);
        QVERIFY(current > previous);
        previous = current;
    }
}

void GenerationTest::onlyTheLatestGenerationIsCurrent() {
    core::GenerationSource source;
    const core::Generation first = source.next();
    const core::Generation second = source.next();
    QVERIFY(!source.isCurrent(first));
    QVERIFY(source.isCurrent(second));
}

void GenerationTest::invalidateMakesEverythingStale() {
    core::GenerationSource source;
    const core::Generation only = source.next();
    QVERIFY(source.isCurrent(only));
    source.invalidate();
    QVERIFY(!source.isCurrent(only));
    QVERIFY(!source.valid());
    // current() keeps the number: callers may still compare it, but isCurrent() is the authority.
    QCOMPARE(source.current(), only);
}

void GenerationTest::nextAfterInvalidateIsCurrentAgain() {
    core::GenerationSource source;
    const core::Generation first = source.next();
    source.invalidate();
    const core::Generation second = source.next();
    QVERIFY(!source.isCurrent(first));
    QVERIFY(source.isCurrent(second));
}

QTEST_GUILESS_MAIN(GenerationTest)
#include "core_generation_test.moc"
