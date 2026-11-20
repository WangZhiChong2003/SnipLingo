// Exercise a synchronous provider failure through the actual CLI and GUI runtime.
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest>

class AppRuntimeIntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void synchronousTranslationFailureExits();
};

void AppRuntimeIntegrationTest::synchronousTranslationFailureExits() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const auto &[variable, name] :
        {qMakePair("XDG_CONFIG_HOME", "config"), qMakePair("XDG_DATA_HOME", "data"),
            qMakePair("XDG_STATE_HOME", "state"), qMakePair("XDG_CACHE_HOME", "cache"),
            qMakePair("XDG_RUNTIME_DIR", "runtime"), qMakePair("TMPDIR", "tmp")}) {
        const QString path = root.filePath(QString::fromLatin1(name));
        QVERIFY(QDir().mkpath(path));
        environment.insert(QString::fromLatin1(variable), path);
    }
    environment.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"),
        QStringLiteral("unix:path=") + root.filePath(QStringLiteral("missing-bus")));
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));

    auto run = [&](const QStringList &arguments, const QByteArray &input = QByteArray()) {
        QProcess process;
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(SNIPLYNGO_EXECUTABLE), arguments);
        if (!process.waitForStarted(5000))
            return qMakePair(-1, process.errorString().toUtf8());
        if (!input.isEmpty())
            process.write(input);
        process.closeWriteChannel();
        if (!process.waitForFinished(10000)) {
            process.kill();
            process.waitForFinished();
            return qMakePair(-1, QByteArrayLiteral("process timed out"));
        }
        return qMakePair(process.exitCode(), process.readAllStandardError());
    };

    const auto configured
        = run({QStringLiteral("engine"), QStringLiteral("use"), QStringLiteral("mymemory")});
    QCOMPARE(configured.first, 0);
    const auto target
        = run({QStringLiteral("engine"), QStringLiteral("target"), QStringLiteral("zh-Hans")});
    QCOMPARE(target.first, 0);
    const auto translated = run(
        {QStringLiteral("trigger"), QStringLiteral("text")}, QString(167, QChar(0x4f60)).toUtf8());
    QCOMPARE(translated.first, 1);
    QVERIFY2(translated.second.contains("SelectionTooLarge"), translated.second.constData());
}

QTEST_GUILESS_MAIN(AppRuntimeIntegrationTest)
#include "app_runtime_integration_test.moc"
