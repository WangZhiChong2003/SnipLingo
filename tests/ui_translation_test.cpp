// The compiled Chinese catalogue must ship inside the runtime library and cover the strings the
// UI asks for. This also pins the resource path used by the translator in the application.
#include <QCoreApplication>
#include <QTranslator>
#include <QtTest>

class UiTranslationTest : public QObject {
    Q_OBJECT

private slots:
    void chineseCatalogueIsEmbedded();
};

void UiTranslationTest::chineseCatalogueIsEmbedded() {
    Q_INIT_RESOURCE(sniplyngo_runtime_translations);

    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(":/i18n/sniplyngo_zh_CN.qm")));
    QVERIFY(QCoreApplication::installTranslator(&translator));

    QCOMPARE(QCoreApplication::translate("ui::SystemTray", "Quit"), QStringLiteral("退出"));
    QCOMPARE(
        QCoreApplication::translate("SettingsWindow", "Follow system"), QStringLiteral("跟随系统"));
    QCOMPARE(QCoreApplication::translate("Languages", "Chinese (Simplified)"),
        QStringLiteral("简体中文"));
    // Dynamic strings keep their placeholders.
    QCOMPARE(QCoreApplication::translate("SettingsWindow", "In use: %1").arg(QStringLiteral("x")),
        QStringLiteral("当前使用：x"));
}

QTEST_GUILESS_MAIN(UiTranslationTest)
#include "ui_translation_test.moc"
