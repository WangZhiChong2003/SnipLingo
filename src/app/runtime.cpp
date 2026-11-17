#include "app/runtime.h"

#include "app/backend_factory.h"
#include "app/clipboard.h"
#include "app/network.h"
#include "app/probe.h"
#include "app/provider_factory.h"
#include "app/swappable_provider.h"
#include "core/log.h"
#include "core/paths.h"
#include "ui/result_controller.h"
#include "ui/settings_controller.h"

#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMimeData>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include <cstdio>
#include <functional>
#include <memory>
#include <unistd.h>

#include "coordinator/request_coordinator.h"
#include "secrets/secret_store.h"
#include "settings/settings_store.h"

namespace app {
namespace {

constexpr int kForwardTimeoutMs = 400;

QString instanceSocketName() {
    // One instance per user: two accounts must be able to run the application at the same time.
    return QStringLiteral("sniplyngo-%1").arg(QString::number(::getuid()));
}

QString qrcPath(const QString &file) {
    return QStringLiteral("qrc:/ui/qml/") + file;
}

// State that lives as long as the process; the windows and controllers are rebuilt only on exit.
struct Application {
    core::XdgPaths paths;
    std::unique_ptr<settings::SettingsStore> settings;
    std::unique_ptr<secrets::SecretStore> secrets;
    std::unique_ptr<selection::SelectionBackend> backend;
    std::unique_ptr<SwappableProvider> provider;
    std::unique_ptr<coordinator::RequestCoordinator> coordinator;
    std::unique_ptr<ui::ResultController> resultController;
    std::unique_ptr<ui::SettingsController> settingsController;

    QQmlEngine engine;
    QObject *resultWindow = nullptr;
    QObject *settingsWindow = nullptr;
    bool resultWasVisible = false;
    bool automaticPresentation = false;
    bool haveSelectionPosition = false;
    QPoint selectionPosition;
    QPoint resultAnchor;

    bool prepare();
    void rebuildProvider();
    void loadWindows();
    void showSettings();
    void showResult();
    // Runs a flow and reports its real outcome (translated, or the engine's error).
    void runTrigger(const QString &flow, const QString &payload,
        std::function<void(bool, const QString &)> done);
    void reportNotice(const core::Error &error);
};

bool Application::prepare() {
    paths = core::XdgPaths::resolve();
    if (const core::Result<void> ensured = paths.ensureDirectories(); !ensured.ok()) {
        std::fprintf(stderr, "cannot create application directories: %s\n",
            qPrintable(ensured.error().detail));
        return false;
    }

    settings = std::make_unique<settings::SettingsStore>(paths);
    settings->load();
    applyNetworkSettings(settings->current());
    secrets = secrets::createSecretStore(probeSecretService());

    provider = std::make_unique<SwappableProvider>();
    rebuildProvider();

    // The selection backend is optional for the clipboard flow: a translate-it-now request must
    // still work where observation is unavailable. "auto" is resolved by the capability report, the
    // single source of truth for what this session offers.
    const app::SessionFacts facts = probeSessionFacts(secrets->availability());
    const app::CapabilityReport report = buildCapabilityReport(facts, settings->current());
    app::BackendCreation creation;
    if (report.selectionBackend.isEmpty())
        creation.reason = report.selectionReason;
    else
        creation = createSelectionBackend(report.selectionBackend);
    backend = std::move(creation.backend);
    if (!backend)
        core::log::write(core::log::Level::Info, "app",
            QStringLiteral("selection observation unavailable: %1").arg(creation.reason));
    if (backend) {
        // Capture the pointer only when an explicitly enabled selection event occurs. This keeps
        // the icon near a mouse selection without monitoring pointer motion in the background.
        QObject::connect(
            backend.get(), &selection::SelectionBackend::eventObserved, &engine, [this] {
                selectionPosition = QCursor::pos();
                haveSelectionPosition = true;
            });
    }

    coordinator = std::make_unique<coordinator::RequestCoordinator>(
        backend.get(), provider.get(), settings.get());
    resultController = std::make_unique<ui::ResultController>(coordinator.get());
    settingsController = std::make_unique<ui::SettingsController>(
        settings.get(), secrets.get(), coordinator.get());

    QObject::connect(settings.get(), &settings::SettingsStore::changed, settings.get(),
        [this](const settings::Settings &next) {
            applyNetworkSettings(next);
            rebuildProvider();
        });
    // Application is not a QObject; the coordinator and the settings store own the connections.
    QObject::connect(coordinator.get(), &coordinator::RequestCoordinator::notice, coordinator.get(),
        [this](const core::Error &error) { reportNotice(error); });

    loadWindows();
    // A saved enabled preference must re-establish the subscription on each start.
    if (settings->current().selection.automaticEnabled && !settings->current().selection.paused)
        settingsController->setAutomaticEnabled(true);
    return true;
}

void Application::rebuildProvider() {
    const settings::Settings &current = settings->current();
    const settings::ProviderConfig *active = current.activeProvider();
    if (active == nullptr) {
        provider->set(nullptr);
        return;
    }
    provider->set(app::createProvider(*active, current.provider, secrets.get()));
}

void Application::loadWindows() {
    QObject::connect(&engine, &QQmlEngine::quit, qApp, &QCoreApplication::quit);
    engine.rootContext()->setContextProperty(
        QStringLiteral("resultController"), resultController.get());
    engine.rootContext()->setContextProperty(
        QStringLiteral("settingsController"), settingsController.get());

    QQmlComponent resultComponent(&engine, QUrl(qrcPath(QStringLiteral("ResultWindow.qml"))));
    if (resultComponent.isError()) {
        std::fprintf(stderr, "cannot load the result window: %s\n",
            qPrintable(resultComponent.errorString()));
        return;
    }
    QObject *const result = resultComponent.create();
    if (result == nullptr) {
        std::fprintf(stderr, "cannot create the result window\n");
        return;
    }
    result->setProperty("resultController", QVariant::fromValue(resultController.get()));
    resultWindow = result;
    QObject::connect(coordinator.get(), &coordinator::RequestCoordinator::presentationRequested,
        result, [this, result](bool automatic, const QRect &anchor) {
            automaticPresentation = automatic;
            if (automatic) {
                if (haveSelectionPosition)
                    resultAnchor = selectionPosition;
                else if (anchor.isValid() && QGuiApplication::screenAt(anchor.center()))
                    resultAnchor = anchor.bottomRight();
                else
                    resultAnchor = QCursor::pos();
            }
            result->setProperty("automaticRequest", automatic);
            result->setProperty("activatedForDismissal", false);
            result->setProperty("interactive", false);
            result->setProperty(
                "expanded", !automatic || settings->current().selection.showResultImmediately);
            showResult();
            if (result->property("visible").toBool() && result->property("expanded").toBool())
                QMetaObject::invokeMethod(result, "activateForInteraction", Qt::QueuedConnection);
        });
    if (auto *window = qobject_cast<QWindow *>(result)) {
        QObject::connect(window, &QWindow::widthChanged, result, [this] { showResult(); });
        QObject::connect(window, &QWindow::heightChanged, result, [this] { showResult(); });
    }
    QObject::connect(resultController.get(), &ui::ResultController::changed, result, [this] {
        const bool visible = resultController->visible();
        if (visible && !resultWasVisible)
            showResult();
        resultWasVisible = visible;
    });

    QQmlComponent settingsComponent(&engine, QUrl(qrcPath(QStringLiteral("SettingsWindow.qml"))));
    if (settingsComponent.isError()) {
        std::fprintf(stderr, "cannot load the settings window: %s\n",
            qPrintable(settingsComponent.errorString()));
        return;
    }
    QObject *const window = settingsComponent.create();
    if (window == nullptr) {
        std::fprintf(stderr, "cannot create the settings window\n");
        return;
    }
    window->setProperty("settingsController", QVariant::fromValue(settingsController.get()));
    settingsWindow = window;
}

void Application::showSettings() {
    if (auto *window = qobject_cast<QWindow *>(settingsWindow)) {
        window->show();
        window->raise();
        window->requestActivate();
    }
}

void Application::showResult() {
    if (resultWindow == nullptr)
        return;
    if (auto *window = qobject_cast<QWindow *>(resultWindow)) {
        const QPoint anchor = automaticPresentation ? resultAnchor : QCursor::pos();
        const QScreen *screen = QGuiApplication::screenAt(anchor);
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;
        const QRect area = screen->availableGeometry();
        const QSize size = window->size();
        QPoint position = area.center() - QPoint(size.width() / 2, size.height() / 2);
        if (automaticPresentation) {
            constexpr int gap = 12;
            int x = anchor.x() + gap;
            int y = anchor.y() + gap;
            if (x + size.width() > area.right() + 1)
                x = anchor.x() - size.width() - gap;
            if (y + size.height() > area.bottom() + 1)
                y = anchor.y() - size.height() - gap;
            position.setX(
                qBound(area.left(), x, qMax(area.left(), area.right() - size.width() + 1)));
            position.setY(
                qBound(area.top(), y, qMax(area.top(), area.bottom() - size.height() + 1)));
        }
        window->setPosition(position);
    }
    // Visibility follows ResultController in QML; writing it here would replace that binding.
}

void Application::reportNotice(const core::Error &error) {
    core::log::write(core::log::Level::Info, "app",
        QStringLiteral("notice: %1 (%2)")
            .arg(QString::fromLatin1(core::stableName(error.code)), error.detail));
}

void Application::runTrigger(
    const QString &flow, const QString &payload, std::function<void(bool, const QString &)> done) {
    if (flow == QLatin1String("clipboard") || flow == QLatin1String("text")) {
        QString text;
        if (flow == QLatin1String("text")) {
            text = payload;
            if (text.isEmpty()) {
                done(false, QStringLiteral("no text was given on stdin"));
                return;
            }
        } else {
            text = readClipboardText(1500);
            if (text.trimmed().isEmpty()) {
                // Say what the platform actually offers: "empty" and "cannot read" need different
                // fixes.
                const QStringList formats = QGuiApplication::clipboard() != nullptr
                    ? QGuiApplication::clipboard()->mimeData()->formats()
                    : QStringList {};
                done(false,
                    QStringLiteral("the clipboard holds no text (formats: %1)")
                        .arg(formats.isEmpty() ? QStringLiteral("none")
                                               : formats.join(QLatin1Char(','))));
                return;
            }
        }

        const core::Result<void> submitted
            = coordinator->submitExplicitText(text, coordinator::Origin::ExplicitClipboard);
        if (!submitted.ok()) {
            done(false, submitted.error().detail);
            return;
        }
        showResult();

        // Report the engine's outcome instead of "submitted", so a failing engine is visible in the
        // terminal.
        const auto finish = [this, done](coordinator::State state) {
            if (state == coordinator::State::ShowingResult) {
                done(true, QString());
            } else {
                const core::Error &error = coordinator->view().lastError;
                done(false,
                    QStringLiteral("%1: %2").arg(
                        QString::fromLatin1(core::stableName(error.code)), error.detail));
            }
        };
        // A provider can fail synchronously (for example, an overlong MyMemory request).
        const coordinator::State state = coordinator->state();
        if (state == coordinator::State::ShowingResult
            || state == coordinator::State::ShowingError) {
            finish(state);
            return;
        }
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection
            = QObject::connect(coordinator.get(), &coordinator::RequestCoordinator::stateChanged,
                coordinator.get(), [connection, finish](coordinator::State next) {
                    if (next != coordinator::State::ShowingResult
                        && next != coordinator::State::ShowingError)
                        return;
                    QObject::disconnect(*connection);
                    finish(next);
                });
        return;
    }
    if (flow == QLatin1String("selection")) {
        // Reading the current selection needs an observation event; there is none outside
        // observation.
        done(false,
            QStringLiteral("trigger selection is not available in this build; enable automatic "
                           "selection translation in the settings window"));
        return;
    }
    done(false, QStringLiteral("unknown trigger flow: %1").arg(flow));
}

}  // namespace

int runGui(const CommandLine &commandLine) {
    core::log::init(core::log::Level::Info);

    const bool readsStdin = commandLine.command == CommandLine::Command::Trigger
        && commandLine.triggerFlow == QLatin1String("text");
    QString stdinText;
    if (readsStdin) {
        QFile input;
        if (input.open(stdin, QIODevice::ReadOnly))
            stdinText = QString::fromUtf8(input.readAll()).trimmed();
    }

    QString forwarded = QStringLiteral("settings");
    if (commandLine.command == CommandLine::Command::Trigger) {
        forwarded = QStringLiteral("trigger %1").arg(commandLine.triggerFlow);
        if (readsStdin)
            forwarded += QLatin1Char(' ') + QString::fromUtf8(stdinText.toUtf8().toBase64());
    }

    QLocalSocket probe;
    probe.connectToServer(instanceSocketName());
    if (probe.waitForConnected(kForwardTimeoutMs)) {
        probe.write(forwarded.toUtf8() + '\n');
        probe.flush();
        probe.waitForBytesWritten(kForwardTimeoutMs);
        // Wait for the instance's verdict: "ok", or "error <reason>". Silent forwarding hid
        // failures.
        QString answer;
        if (probe.waitForReadyRead(kForwardTimeoutMs))
            answer = QString::fromUtf8(probe.readAll()).trimmed();
        else
            answer = QStringLiteral(
                "ok");  // the instance accepted it; its outcome is shown in its window
        probe.disconnectFromServer();
        if (answer.startsWith(QLatin1String("error"))) {
            std::fprintf(stderr, "%s\n", qPrintable(answer.mid(6).trimmed()));
            return 1;
        }
        return 0;
    }

    Application application;
    if (!application.prepare())
        return 1;

    // Ownership is decided by a lock file, which works in every sandbox; the socket only carries
    // requests and may legitimately be unavailable (some container profiles block AF_UNIX).
    QLockFile instanceLock(
        QDir(application.paths.stateDir()).filePath(QStringLiteral("instance.lock")));
    // A crashed instance must not block the next start forever: its lock is reclaimed once the
    // recorded process is gone (or, as a fallback, after this many milliseconds).
    instanceLock.setStaleLockTime(30000);
    if (!instanceLock.tryLock(200)) {
        std::fprintf(stderr, "another instance is already running\n");
        return 1;
    }

    // A crashed instance leaves its socket behind; the lock above already proved we own it.
    QLocalServer::removeServer(instanceSocketName());
    QLocalServer server;
    if (!server.listen(instanceSocketName())) {
        core::log::write(core::log::Level::Info, "app",
            QStringLiteral("cannot accept forwarded requests (%1); start flows from this instance")
                .arg(server.errorString()));
    }
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&application, &server] {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [&application, socket] {
                const QString request = QString::fromUtf8(socket->readAll()).trimmed();
                const QPointer<QLocalSocket> guardedSocket(socket);
                const auto reply = [guardedSocket](bool ok, const QString &message) {
                    if (!guardedSocket)
                        return;
                    const QString line
                        = ok ? QStringLiteral("ok\n") : QStringLiteral("error %1\n").arg(message);
                    guardedSocket->write(line.toUtf8());
                    guardedSocket->flush();
                    guardedSocket->waitForBytesWritten(300);
                    guardedSocket->disconnectFromServer();
                };

                if (!request.startsWith(QLatin1String("trigger "))) {
                    application.showSettings();
                    reply(true, {});
                    return;
                }
                const QString rest = request.mid(8);
                const QString flow = rest.section(QLatin1Char(' '), 0, 0);
                const QString encoded = rest.section(QLatin1Char(' '), 1);
                // Text travels over a local socket only, in one process pair of the same user; it
                // is base64 so the request stays a single line. Nothing is logged or stored.
                const QString payload = encoded.isEmpty()
                    ? QString()
                    : QString::fromUtf8(QByteArray::fromBase64(encoded.toUtf8()));
                application.runTrigger(flow, payload, reply);
            });
        }
    });

    core::log::write(core::log::Level::Info, "app",
        QStringLiteral("instance started (pid %1, platform %2, socket %3)")
            .arg(QCoreApplication::applicationPid())
            .arg(QGuiApplication::platformName(),
                server.isListening() ? QStringLiteral("ready") : QStringLiteral("unavailable")));

    if (commandLine.command == CommandLine::Command::Trigger) {
        const QString flow = commandLine.triggerFlow;
        // Explicit stdin flow: lets the translation path be exercised without a clipboard at all.
        const QString payload = flow == QLatin1String("text") ? stdinText : QString();
        // Runs once the event loop is up: the platform (and the clipboard, on Wayland) is only
        // usable then.
        QTimer::singleShot(0, qApp, [&application, flow, payload] {
            application.runTrigger(flow, payload, [](bool ok, const QString &message) {
                if (!ok)
                    std::fprintf(stderr, "%s\n", qPrintable(message));
                std::fflush(stderr);
                QCoreApplication::exit(ok ? 0 : 1);
            });
        });
    } else if (commandLine.command == CommandLine::Command::Settings) {
        application.showSettings();
    }

    return QGuiApplication::exec();
}

}  // namespace app
