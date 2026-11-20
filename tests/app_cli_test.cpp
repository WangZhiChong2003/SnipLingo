// Command-line parsing and argument validation.
#include "app/cli.h"

#include <QRegularExpression>
#include <QtTest>

using app::CommandLine;

class AppCliTest : public QObject {
    Q_OBJECT

private slots:
    void noArgumentsMeansOpenTheApplication();
    void capsAndDiagAcceptJsonOnly();
    void triggerRequiresAKnownFlow();
    void unknownCommandIsReported();
    void versionAndHelp();
    void watchAcceptsDurationAndShowText();
    void usageMentionsEveryCommand();
    void helpBannerPrecedesTheUsage();
    void guiCommandsAreRecognisedBeforeParsing();
    void triggerAcceptsTheStdinFlow();
    void engineSubcommandsParse();
};

void AppCliTest::noArgumentsMeansOpenTheApplication() {
    const CommandLine parsed = app::parseCommandLine({});
    QVERIFY(parsed.ok());
    QCOMPARE(parsed.command, CommandLine::Command::Settings);
}

void AppCliTest::capsAndDiagAcceptJsonOnly() {
    for (const QString &name : {QStringLiteral("caps"), QStringLiteral("diag")}) {
        const CommandLine plain = app::parseCommandLine({name});
        QVERIFY(plain.ok());
        QVERIFY(!plain.json);

        const CommandLine asJson = app::parseCommandLine({name, QStringLiteral("--json")});
        QVERIFY(asJson.ok());
        QVERIFY(asJson.json);

        const CommandLine bad = app::parseCommandLine({name, QStringLiteral("--loud")});
        QVERIFY(!bad.ok());
        QVERIFY(bad.error.contains(QStringLiteral("unexpected argument")));
    }
}

void AppCliTest::triggerRequiresAKnownFlow() {
    for (const QString &flow :
        {QStringLiteral("selection"), QStringLiteral("region"), QStringLiteral("clipboard")}) {
        const CommandLine parsed = app::parseCommandLine({QStringLiteral("trigger"), flow});
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.command, CommandLine::Command::Trigger);
        QCOMPARE(parsed.triggerFlow, flow);
    }

    QVERIFY(!app::parseCommandLine({QStringLiteral("trigger")}).ok());
    QVERIFY(!app::parseCommandLine({QStringLiteral("trigger"), QStringLiteral("telepathy")}).ok());
    QVERIFY(!app::parseCommandLine(
        {QStringLiteral("trigger"), QStringLiteral("selection"), QStringLiteral("extra")})
                 .ok());
}

void AppCliTest::unknownCommandIsReported() {
    const CommandLine parsed = app::parseCommandLine({QStringLiteral("translate-everything")});
    QVERIFY(!parsed.ok());
    QVERIFY(parsed.error.contains(QStringLiteral("unknown command")));
}

void AppCliTest::versionAndHelp() {
    QCOMPARE(app::parseCommandLine({QStringLiteral("--version")}).command,
        CommandLine::Command::Version);
    QCOMPARE(
        app::parseCommandLine({QStringLiteral("version")}).command, CommandLine::Command::Version);
    QCOMPARE(app::parseCommandLine({QStringLiteral("--help")}).command, CommandLine::Command::Help);
    QCOMPARE(app::parseCommandLine({QStringLiteral("help")}).command, CommandLine::Command::Help);
}

void AppCliTest::watchAcceptsDurationAndShowText() {
    const CommandLine defaults = app::parseCommandLine({QStringLiteral("watch")});
    QVERIFY(defaults.ok());
    QCOMPARE(defaults.command, CommandLine::Command::Watch);
    QCOMPARE(defaults.durationMs, 10000);
    QVERIFY(!defaults.showText);

    const CommandLine configured = app::parseCommandLine({QStringLiteral("watch"),
        QStringLiteral("--duration"), QStringLiteral("2500"), QStringLiteral("--show-text")});
    QVERIFY(configured.ok());
    QCOMPARE(configured.durationMs, 2500);
    QVERIFY(configured.showText);

    QVERIFY(!app::parseCommandLine({QStringLiteral("watch"), QStringLiteral("--duration")}).ok());
    QVERIFY(!app::parseCommandLine(
        {QStringLiteral("watch"), QStringLiteral("--duration"), QStringLiteral("10")})
                 .ok());
    QVERIFY(!app::parseCommandLine({QStringLiteral("watch"), QStringLiteral("--loud")}).ok());
}

void AppCliTest::guiCommandsAreRecognisedBeforeParsing() {
    // The application type must be chosen from the raw arguments: these need a QGuiApplication.
    QVERIFY(app::requiresGui({}));
    QVERIFY(app::requiresGui({QStringLiteral("settings")}));
    QVERIFY(app::requiresGui({QStringLiteral("trigger"), QStringLiteral("clipboard")}));

    // Diagnostics must keep working without a display.
    for (const QString &name : {QStringLiteral("caps"), QStringLiteral("diag"),
             QStringLiteral("watch"), QStringLiteral("version"), QStringLiteral("help")})
        QVERIFY2(!app::requiresGui({name}), qPrintable(name));
}

void AppCliTest::triggerAcceptsTheStdinFlow() {
    // `trigger text` exists so the translation path can be exercised without any clipboard.
    const CommandLine parsed
        = app::parseCommandLine({QStringLiteral("trigger"), QStringLiteral("text")});
    QVERIFY2(parsed.ok(), qPrintable(parsed.error));
    QCOMPARE(parsed.triggerFlow, QStringLiteral("text"));
    QVERIFY(app::usageText().contains(QStringLiteral("text")));
}

void AppCliTest::engineSubcommandsParse() {
    const CommandLine list
        = app::parseCommandLine({QStringLiteral("engine"), QStringLiteral("list")});
    QVERIFY(list.ok());
    QCOMPARE(list.command, CommandLine::Command::Engine);
    QCOMPARE(list.engineAction, QStringLiteral("list"));

    const CommandLine asJson = app::parseCommandLine(
        {QStringLiteral("engine"), QStringLiteral("list"), QStringLiteral("--json")});
    QVERIFY(asJson.ok());
    QVERIFY(asJson.json);

    const CommandLine use = app::parseCommandLine(
        {QStringLiteral("engine"), QStringLiteral("use"), QStringLiteral("google-free")});
    QVERIFY(use.ok());
    QCOMPARE(use.engineArgument, QStringLiteral("google-free"));

    const CommandLine all = app::parseCommandLine(
        {QStringLiteral("engine"), QStringLiteral("test"), QStringLiteral("--all")});
    QVERIFY(all.ok());
    QVERIFY(all.all);

    QVERIFY(!app::parseCommandLine({QStringLiteral("engine")}).ok());
    QVERIFY(!app::parseCommandLine({QStringLiteral("engine"), QStringLiteral("use")}).ok());
    QVERIFY(!app::parseCommandLine({QStringLiteral("engine"), QStringLiteral("nonsense")}).ok());

    const CommandLine add = app::parseCommandLine(
        {QStringLiteral("engine"), QStringLiteral("add"), QStringLiteral("ollama"),
            QStringLiteral("http://127.0.0.1:11434/v1"), QStringLiteral("qwen2.5")});
    QVERIFY2(add.ok(), qPrintable(add.error));
    QCOMPARE(add.engineArgument, QStringLiteral("ollama http://127.0.0.1:11434/v1 qwen2.5"));
    QVERIFY(!app::parseCommandLine(
        {QStringLiteral("engine"), QStringLiteral("add"), QStringLiteral("ollama")})
                 .ok());
    // Engine management is headless.
    QVERIFY(!app::requiresGui({QStringLiteral("engine"), QStringLiteral("list")}));
}

void AppCliTest::usageMentionsEveryCommand() {
    const QString usage = app::usageText();
    for (const QString &token : {QStringLiteral("caps"), QStringLiteral("diag"),
             QStringLiteral("settings"), QStringLiteral("trigger"), QStringLiteral("version")}) {
        QVERIFY2(usage.contains(token), qPrintable(token));
    }
    // The exit code contract must be visible to whoever reads the help output.
    QVERIFY(usage.contains(QStringLiteral("Exit codes")));
}

void AppCliTest::helpBannerPrecedesTheUsage() {
    // Piped or uncoloured output stays byte-identical to the plain usage text.
    QCOMPARE(app::helpText(false), app::usageText());

    const QString colored = app::helpText(true);
    QVERIFY2(colored.endsWith(app::usageText()), "the logo precedes the command reference");
    QVERIFY(colored.contains(QStringLiteral("\x1b[")));
    QVERIFY(colored.contains(QStringLiteral("▀")));
    QVERIFY(colored.contains(QStringLiteral("█")));
    // The reference itself must stay free of escape sequences.
    QVERIFY(!app::usageText().contains(QStringLiteral("\x1b[")));

    // The icon and the wordmark have to fit a default 80-column terminal without wrapping.
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;]*m"));
    const QString banner = colored.left(colored.indexOf(app::usageText()));
    for (const QString &line : banner.split(QLatin1Char('\n'))) {
        QString visible = line;
        visible.remove(ansi);
        QVERIFY2(visible.size() <= 80,
            qPrintable(QStringLiteral("banner width %1").arg(visible.size())));
    }
}

QTEST_GUILESS_MAIN(AppCliTest)
#include "app_cli_test.moc"
