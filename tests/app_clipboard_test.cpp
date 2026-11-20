// The clipboard read must tolerate the asynchronous Wayland round trip without hanging.
#include "app/clipboard.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QtTest>

class AppClipboardTest : public QObject {
    Q_OBJECT

private slots:
    void readsTheClipboardImmediately();
    void emptyClipboardReturnsEmptyAndDoesNotHang();
};

void AppClipboardTest::readsTheClipboardImmediately() {
    QGuiApplication::clipboard()->setText(QStringLiteral("hello"));
    QCOMPARE(app::readClipboardText(0), QStringLiteral("hello"));
    QCOMPARE(app::readClipboardText(500), QStringLiteral("hello"));
}

void AppClipboardTest::emptyClipboardReturnsEmptyAndDoesNotHang() {
    QGuiApplication::clipboard()->clear();
    QCOMPARE(app::readClipboardText(0), QString());

    // Wayland may have nothing to deliver, so the bounded pump must give up quickly instead of
    // blocking.
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(app::readClipboardText(100), QString());
    QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed())));
}

QTEST_MAIN(AppClipboardTest)
#include "app_clipboard_test.moc"
