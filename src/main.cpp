// sniplyngo entry point. This build ships the diagnostic surface plus a selection-event watcher;
// the application window, the tray and the translation flow arrive with their own modules.
#include "app/backend_factory.h"
#include "app/capability_report.h"
#include "app/cli.h"
#include "app/engine_cli.h"
#include "app/network.h"
#include "app/probe.h"
#include "app/runtime.h"
#include "core/log.h"
#include "core/paths.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QTimer>

#include <cstdio>

#include "secrets/session_store.h"
#include "selection/selection_backend.h"
#include "settings/settings_store.h"

namespace {

struct Startup {
    core::XdgPaths paths;
    std::unique_ptr<settings::SettingsStore> settings;
    std::unique_ptr<secrets::SecretStore> secrets;
    app::CapabilityReport report;
    settings::Settings currentSettings;
};

Startup prepareStartup() {
    Startup startup;
    startup.paths = core::XdgPaths::resolve();

    QStringList startupIssues;
    if (const auto ensured = startup.paths.ensureDirectories(); !ensured.ok()) {
        core::log::writeError("app", ensured.error());
        startupIssues.append(QStringLiteral("cannot create application directories: %1")
                                 .arg(ensured.error().detail));
    }

    startup.settings = std::make_unique<settings::SettingsStore>(startup.paths);
    const settings::LoadResult loaded = startup.settings->load();
    startup.currentSettings = loaded.settings;
    app::applyNetworkSettings(startup.currentSettings);

    const secrets::ServiceProbe secretProbe = app::probeSecretService();
    startup.secrets = secrets::createSecretStore(secretProbe);

    app::SessionFacts facts = app::probeSessionFacts(startup.secrets->availability());
    facts.settingsIssues = loaded.issues + startupIssues;

    startup.report = app::buildCapabilityReport(facts, loaded.settings);
    return startup;
}

int reportCapabilities(const app::CommandLine &commandLine) {
    core::log::init(core::log::Level::Info);
    const Startup startup = prepareStartup();

    if (commandLine.json) {
        const QByteArray json
            = QJsonDocument(startup.report.toJson()).toJson(QJsonDocument::Indented);
        std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    } else {
        const QByteArray text = startup.report.toText().toUtf8();
        std::fwrite(text.constData(), 1, size_t(text.size()), stdout);
    }
    return 0;
}

int watchSelection(const app::CommandLine &commandLine) {
    core::log::init(core::log::Level::Info);
    Startup startup = prepareStartup();

    if (startup.report.selectionBackend.isEmpty()) {
        std::fprintf(stderr, "no selection backend is available here: %s\n",
            qPrintable(startup.report.selectionReason));
        return 2;
    }

    app::BackendCreation creation = app::createSelectionBackend(startup.report.selectionBackend);
    if (!creation.backend) {
        std::fprintf(stderr, "%s\n", qPrintable(creation.reason));
        return 2;
    }

    if (commandLine.showText)
        std::fprintf(stderr,
            "warning: --show-text prints selection content; do not use with sensitive text\n");

    selection::SelectionBackend *backend = creation.backend.get();
    selection::ResolveLimits limits;
    limits.maxChars = startup.currentSettings.selection.maxSelectionChars;
    limits.timeoutMs = startup.currentSettings.selection.resolveTimeoutMs;

    int events = 0;
    int withText = 0;
    QObject::connect(backend, &selection::SelectionBackend::eventObserved, backend,
        [backend, limits, showText = commandLine.showText, &events, &withText](
            const selection::SelectionEvent &event) {
            ++events;
            backend->resolveLatest(event.seq, limits,
                [showText, event, &withText](
                    quint64 seq, core::Result<selection::SelectionText> result) {
                    if (!result.ok()) {
                        std::printf("seq=%llu app=%s SKIPPED: %s\n",
                            static_cast<unsigned long long>(seq), qPrintable(event.source.appName),
                            core::stableName(result.error().code));
                        std::fflush(stdout);
                        return;
                    }
                    ++withText;
                    const selection::SelectionText value = result.value();
                    std::printf("seq=%llu app=%s chars=%d anchor=%s\n",
                        static_cast<unsigned long long>(seq), qPrintable(event.source.appName),
                        int(value.text.size()),
                        (value.anchorLogical || event.anchorLogical) ? "yes" : "no");
                    if (showText)
                        std::printf("    text: \"%s\"\n", qPrintable(value.text.left(80)));
                    std::fflush(stdout);
                });
        });

    if (const auto subscribed = backend->subscribe(); !subscribed.ok()) {
        std::fprintf(stderr, "cannot subscribe to %s: %s\n",
            qPrintable(startup.report.selectionBackend), qPrintable(subscribed.error().detail));
        return 2;
    }

    std::printf("watching %s for %d ms — select text in an application now\n",
        qPrintable(startup.report.selectionBackend), commandLine.durationMs);
    std::fflush(stdout);

    QTimer::singleShot(
        commandLine.durationMs, QCoreApplication::instance(), &QCoreApplication::quit);
    QCoreApplication::instance()->exec();

    backend->unsubscribe();
    std::printf("\nsummary: events=%d with-text=%d\n", events, withText);
    return events > 0 ? 0 : 1;
}

}  // namespace

namespace {

// Reading the one preference that changes the QPA platform has to happen before QGuiApplication
// exists; the regular SettingsStore is created later in the GUI startup path.
bool xwaylandPopupRequested() {
    if (!qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        return false;
    const bool waylandSession = qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")
        || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    if (!waylandSession || qEnvironmentVariableIsEmpty("DISPLAY"))
        return false;

    QFile file(core::XdgPaths::resolve().settingsFile());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return false;
    return settings::loadFromDocument(document.object(), true).settings.selection.xwaylandPopup;
}

int runGuiCommand(int argc, char **argv, const app::CommandLine &commandLine) {
    // Qt defaults to xcb whenever DISPLAY is set, which is the case on every Wayland session
    // (XWayland). Prefer Wayland there and keep xcb as the fallback, unless the user chose a
    // platform explicitly. XWayland is also the only way a Wayland session can position the result
    // window next to a selection, so it is an explicit opt-in.
    const bool waylandSession = qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")
        || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && waylandSession) {
        // The second plugin is the fallback if the first is unavailable in this sandbox.
        qputenv("QT_QPA_PLATFORM", xwaylandPopupRequested() ? "xcb;wayland" : "wayland;xcb");
    }
    QApplication application(argc, argv);
    // The settings window offers an explicit Hide action for background selection translation.
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName(QStringLiteral("sniplyngo"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SNIPLYNGO_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("sniplingo"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/sniplingo-icon.png")));
    return app::runGui(commandLine);
}

}  // namespace

int main(int argc, char **argv) {
#ifdef SNIPLYNGO_HAVE_ATSPI
    // libatspi dispatches on GLib's default context. Let its worker own that context instead of
    // allowing Qt's GLib dispatcher on the GUI thread to acquire it and steal callbacks.
    qputenv("QT_NO_GLIB", "1");
#endif
    QStringList rawArguments;
    for (int index = 1; index < argc; ++index)
        rawArguments.append(QString::fromLocal8Bit(argv[index]));

    // The window commands need a GUI application object; the diagnostics must keep working without
    // a display, so the choice happens before any application object exists.
    if (app::requiresGui(rawArguments)) {
        // parseCommandLine expects the arguments after the program name.
        const app::CommandLine guiCommandLine = app::parseCommandLine(rawArguments);
        if (!guiCommandLine.ok()) {
            std::fprintf(
                stderr, "%s\n\n%s", qPrintable(guiCommandLine.error), qPrintable(app::usageText()));
            return 1;
        }
        return runGuiCommand(argc, argv, guiCommandLine);
    }

    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("sniplyngo"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SNIPLYNGO_VERSION));

    const app::CommandLine commandLine = app::parseCommandLine(application.arguments().mid(1));
    if (!commandLine.ok()) {
        std::fprintf(
            stderr, "%s\n\n%s", qPrintable(commandLine.error), qPrintable(app::usageText()));
        return 1;
    }

    switch (commandLine.command) {
    case app::CommandLine::Command::Help: {
        const QByteArray help = app::helpText(app::helpColorSupported()).toUtf8();
        std::fwrite(help.constData(), 1, size_t(help.size()), stdout);
        return 0;
    }
    case app::CommandLine::Command::Version:
        std::printf("sniplyngo %s\n", qPrintable(QCoreApplication::applicationVersion()));
        return 0;
    case app::CommandLine::Command::Caps:
    case app::CommandLine::Command::Diag:
        return reportCapabilities(commandLine);
    case app::CommandLine::Command::Watch:
        return watchSelection(commandLine);
    case app::CommandLine::Command::Proxy:
        return app::proxyCommand(commandLine.proxyMode, commandLine.proxyUrl);
    case app::CommandLine::Command::Engine:
        if (commandLine.engineAction == QLatin1String("list"))
            return app::engineList(commandLine);
        if (commandLine.engineAction == QLatin1String("use"))
            return app::engineUse(commandLine.engineArgument);
        if (commandLine.engineAction == QLatin1String("add")) {
            const QStringList parts = commandLine.engineArgument.split(QLatin1Char(' '));
            return app::engineAdd(parts.value(0), parts.value(1), parts.value(2));
        }
        if (commandLine.engineAction == QLatin1String("target"))
            return app::engineSetTarget(commandLine.engineArgument);
        return app::engineTest(commandLine.engineArgument, commandLine.all);
    case app::CommandLine::Command::Settings:
    case app::CommandLine::Command::Trigger:
        // Handled by the GUI branch above; reaching here means requiresGui() disagrees with the
        // parser.
        std::fprintf(stderr, "sniplyngo: this command needs the application window\n");
        return 2;
    }
    return 1;
}
