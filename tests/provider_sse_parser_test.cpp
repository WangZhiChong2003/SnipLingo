// SSE parsing must survive arbitrary chunk boundaries: a proxy may split anywhere.
#include <QtTest>

#include "provider/sse_parser.h"

class SseParserTest : public QObject {
    Q_OBJECT

private slots:
    void singleEventSplitAcrossChunks();
    void crlfAndCommentLines();
    void multipleDataLinesJoinWithNewline();
    void doneSentinelIsDispatched();
    void trailingPartialLineIsDropped();
    void multibyteUtf8SplitAcrossChunks();
    void emptyDataIsDispatchedOnce();
};

namespace {

QList<QByteArray> collect(provider::SseParser &parser, const QList<QByteArray> &chunks) {
    QList<QByteArray> payloads;
    for (const QByteArray &chunk : chunks)
        parser.feed(chunk);
    parser.finish();
    return payloads;
}

}  // namespace

void SseParserTest::singleEventSplitAcrossChunks() {
    QList<QByteArray> seen;
    provider::SseParser parser([&seen](const QByteArray &payload) { seen.append(payload); });
    parser.feed("da");
    parser.feed("ta: {\"a\":1}\n");
    parser.feed("\nda");
    parser.feed("ta: second\n\n");
    QCOMPARE(seen.size(), 2);
    QCOMPARE(seen.at(0), QByteArray("{\"a\":1}"));
    QCOMPARE(seen.at(1), QByteArray("second"));
}

void SseParserTest::crlfAndCommentLines() {
    QList<QByteArray> seen;
    provider::SseParser parser([&seen](const QByteArray &payload) { seen.append(payload); });
    parser.feed(": keep-alive\r\n");
    parser.feed("event: message\r\n");
    parser.feed("id: 7\r\n");
    parser.feed("data: payload\r\n\r\n");
    QCOMPARE(seen.size(), 1);
    QCOMPARE(seen.first(), QByteArray("payload"));
}

void SseParserTest::multipleDataLinesJoinWithNewline() {
    QList<QByteArray> seen;
    provider::SseParser parser([&seen](const QByteArray &payload) { seen.append(payload); });
    parser.feed("data: line one\ndata: line two\n\n");
    QCOMPARE(seen.size(), 1);
    QCOMPARE(seen.first(), QByteArray("line one\nline two"));
}

void SseParserTest::doneSentinelIsDispatched() {
    QList<QByteArray> seen;
    provider::SseParser parser([&seen](const QByteArray &payload) { seen.append(payload); });
    parser.feed("data: {\"x\":1}\n\ndata: [DONE]\n\n");
    QCOMPARE(seen.size(), 2);
    QCOMPARE(seen.last(), QByteArray("[DONE]"));
}

void SseParserTest::trailingPartialLineIsDropped() {
    QList<QByteArray> seen;
    provider::SseParser parser([&seen](const QByteArray &payload) { seen.append(payload); });
    parser.feed("data: complete\n\n");
    parser.feed("data: incomp");
    parser.finish();
    QCOMPARE(seen.size(), 1);
    QCOMPARE(seen.first(), QByteArray("complete"));
}

void SseParserTest::multibyteUtf8SplitAcrossChunks() {
    // "你好" as UTF-8: split in the middle of the first character.
    const QByteArray payload = QStringLiteral("data: 你好\n\n").toUtf8();
    QVERIFY(payload.size() > 8);

    QList<QByteArray> seen;
    provider::SseParser parser([&seen](const QByteArray &chunk) { seen.append(chunk); });
    parser.feed(payload.left(9));  // inside a multi-byte sequence
    parser.feed(payload.mid(9));

    QCOMPARE(seen.size(), 1);
    QCOMPARE(QString::fromUtf8(seen.first()), QStringLiteral("你好"));
}

void SseParserTest::emptyDataIsDispatchedOnce() {
    QList<QByteArray> seen;
    provider::SseParser parser([&seen](const QByteArray &payload) { seen.append(payload); });
    parser.feed("data:\n\ndata: x\n\n");
    QCOMPARE(seen.size(), 2);
    QCOMPARE(seen.at(0), QByteArray());
    QCOMPARE(seen.at(1), QByteArray("x"));
    QCOMPARE(parser.dispatchedEvents(), 2);
}

QTEST_GUILESS_MAIN(SseParserTest)
#include "provider_sse_parser_test.moc"
