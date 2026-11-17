// AT-SPI composition rules: what counts as a selection, what is ignored, and the size policy.
// These decisions are pure, so they are tested here; the libatspi glue is exercised on a real
// session.
#include "platform/atspi/selection_reading.h"

#include <QtTest>

using namespace platform::atspi;

class SelectionReadingTest : public QObject {
    Q_OBJECT

private slots:
    void protectedRolesAreIgnored_data();
    void protectedRolesAreIgnored();
    void ordinaryRolesAreNotIgnored();
    void ownApplicationEventsAreIgnored();
    void emptyRangesAreEmptyNotErrors();
    void whitespaceOnlyIsEmpty();
    void multipleRangesAreJoinedWithNewline();
    void surroundingWhitespaceIsTrimmed();
    void oversizeIsRefusedNotTruncated();
    void anchorIsPassedThrough();
};

void SelectionReadingTest::protectedRolesAreIgnored_data() {
    QTest::addColumn<QString>("role");
    QTest::addColumn<bool>("ignored");
    QTest::newRow("password text") << QStringLiteral("password text") << true;
    QTest::newRow("password") << QStringLiteral("password") << true;
    QTest::newRow("Password Text case") << QStringLiteral("Password Text") << true;
    QTest::newRow("protected") << QStringLiteral("protected") << true;
    QTest::newRow("text") << QStringLiteral("text") << false;
    QTest::newRow("terminal") << QStringLiteral("terminal") << false;
    QTest::newRow("entry") << QStringLiteral("entry") << false;
}

void SelectionReadingTest::protectedRolesAreIgnored() {
    QFETCH(QString, role);
    QFETCH(bool, ignored);
    QCOMPARE(isIgnorableRole(role), ignored);
}

void SelectionReadingTest::ordinaryRolesAreNotIgnored() {
    QVERIFY(!isIgnorableRole(QString()));
    QVERIFY(!isIgnorableRole(QStringLiteral("document text")));
}

void SelectionReadingTest::ownApplicationEventsAreIgnored() {
    QVERIFY(isOwnApplication(QStringLiteral("sniplyngo"), QStringLiteral("sniplyngo")));
    QVERIFY(isOwnApplication(QStringLiteral("SnipLingo"), QStringLiteral("snipLINGO")));
    QVERIFY(!isOwnApplication(QStringLiteral("firefox"), QStringLiteral("sniplyngo")));
    // Without a known own-name nothing is filtered: silence would hide real events.
    QVERIFY(!isOwnApplication(QStringLiteral("sniplyngo"), QString()));
}

void SelectionReadingTest::emptyRangesAreEmptyNotErrors() {
    ReadPolicy policy;
    const auto result = composeSelection({}, std::nullopt, policy);
    QVERIFY(!result.ok());
    QCOMPARE(result.error().code, core::ErrorCode::SelectionEmpty);
}

void SelectionReadingTest::whitespaceOnlyIsEmpty() {
    ReadPolicy policy;
    const auto result
        = composeSelection({QStringLiteral("   "), QStringLiteral("\n\t")}, std::nullopt, policy);
    QVERIFY(!result.ok());
    QCOMPARE(result.error().code, core::ErrorCode::SelectionEmpty);
}

void SelectionReadingTest::multipleRangesAreJoinedWithNewline() {
    ReadPolicy policy;
    const auto result = composeSelection(
        {QStringLiteral("first"), QStringLiteral("second")}, std::nullopt, policy);
    QVERIFY(result.ok());
    QCOMPARE(result.value().text, QStringLiteral("first\nsecond"));
}

void SelectionReadingTest::surroundingWhitespaceIsTrimmed() {
    ReadPolicy policy;
    const auto result
        = composeSelection({QStringLiteral("  keep inner spacing \n")}, std::nullopt, policy);
    QVERIFY(result.ok());
    // Internal whitespace and newlines stay untouched: the user's text is translated as-is.
    QCOMPARE(result.value().text, QStringLiteral("keep inner spacing"));
}

void SelectionReadingTest::oversizeIsRefusedNotTruncated() {
    ReadPolicy policy;
    policy.hardCap = 10;
    const auto result = composeSelection({QString(11, QLatin1Char('a'))}, std::nullopt, policy);
    QVERIFY(!result.ok());
    QCOMPARE(result.error().code, core::ErrorCode::SelectionTooLarge);
    QVERIFY(result.error().detail.contains(QStringLiteral("11")));
}

void SelectionReadingTest::anchorIsPassedThrough() {
    ReadPolicy policy;
    const QRect anchor(120, 60, 200, 24);
    const auto result = composeSelection({QStringLiteral("text")}, anchor, policy);
    QVERIFY(result.ok());
    QVERIFY(result.value().anchorLogical.has_value());
    QCOMPARE(*result.value().anchorLogical, anchor);
}

QTEST_GUILESS_MAIN(SelectionReadingTest)
#include "selection_reading_test.moc"
