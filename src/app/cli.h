// Command-line surface (docs/DESIGN.md §15 appendix A). Parsing lives apart from main() so it is
// testable.
#pragma once

#include <QString>
#include <QStringList>

namespace app {

struct CommandLine {
    enum class Command { Help, Version, Caps, Diag, Settings, Trigger, Watch, Engine, Proxy };

    Command command = Command::Settings;  // no arguments: open/activate the application
    bool json = false;
    QString triggerFlow;  // "selection" | "region" | "clipboard" | "text" (stdin)
    int durationMs = 10000;  // `watch`
    bool showText = false;  // `watch`: diagnostic only, prints selection content
    // `engine` subcommand: "list" | "use" | "target" | "test"
    QString engineAction;
    QString engineArgument;
    bool all = false;  // `engine test --all`
    QString proxyMode;  // `proxy` subcommand: "system" | "none" | "manual"
    QString proxyUrl;
    QString error;  // non-empty when the arguments are invalid

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

CommandLine parseCommandLine(const QStringList &arguments);

// True when the command needs a GUI application object (a window or the platform clipboard).
// Decided from the raw arguments, because the application type must be chosen before parsing runs.
[[nodiscard]] bool requiresGui(const QStringList &arguments);
QString usageText();

}  // namespace app
