#include "ui/system_tray.h"

#include <QAction>
#include <QPixmap>
#include <QSignalSpy>
#include <QtTest>

class SystemTrayTest : public QObject {
    Q_OBJECT

private slots:
    void menuAndIconEmitRequests();
};

void SystemTrayTest::menuAndIconEmitRequests() {
    QPixmap image(16, 16);
    image.fill(Qt::blue);
    const QIcon logo(image);
    ui::SystemTray tray(logo);
    QSignalSpy opened(&tray, &ui::SystemTray::openRequested);
    QSignalSpy quit(&tray, &ui::SystemTray::quitRequested);
    auto *icon = tray.findChild<QSystemTrayIcon *>();
    QVERIFY(icon);
    QCOMPARE(icon->icon().cacheKey(), logo.cacheKey());
    QVERIFY(!icon->icon().isNull());
    QVERIFY(icon->isVisible());
    QVERIFY(icon->contextMenu());
    const auto actions = icon->contextMenu()->actions();
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions.first()->text(), QStringLiteral("Open SnipLingo"));
    QCOMPARE(actions.last()->text(), QStringLiteral("Quit"));

    actions.first()->trigger();
    QCOMPARE(opened.size(), 1);
    QCOMPARE(quit.size(), 0);
    icon->activated(QSystemTrayIcon::Trigger);
    QCOMPARE(opened.size(), 2);
    icon->activated(QSystemTrayIcon::Context);
    QCOMPARE(opened.size(), 2);
    actions.last()->trigger();
    QCOMPARE(quit.size(), 1);
}

QTEST_MAIN(SystemTrayTest)
#include "ui_system_tray_test.moc"
