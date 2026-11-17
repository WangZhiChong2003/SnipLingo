#include "app/cli.h"

namespace app {

QString usageText() {
    return QStringLiteral(
        "sniplyngo — translate selected text on the Linux desktop\n"
        "\n"
        "  sniplyngo                     open or activate the application\n"
        "  sniplyngo caps [--json]       report what this session supports (no user data)\n"
        "  sniplyngo diag [--json]       caps plus settings and credential diagnostics\n"
        "  sniplyngo settings            open settings\n"
        "  sniplyngo trigger <flow>      ask a running instance to start a flow\n"
        "                                flow: selection | region | clipboard | text\n"
        "                                (text reads the text to translate from stdin)\n"
        "  sniplyngo watch [--duration MS] [--show-text]\n"
        "                                watch for selection events and print what the backend "
        "sees\n"
        "                                (diagnostic; --show-text prints selection content)\n"
        "  sniplyngo engine list [--json]         show configured engines and the target language\n"
        "  sniplyngo engine use <id|preset>       add a keyless preset if needed and make it "
        "active\n"
        "                                presets: google-free | bing-free | mymemory | "
        "libretranslate\n"
        "  sniplyngo engine add <id> <url> <model> add an OpenAI-compatible engine (e.g. local "
        "Ollama)\n"
        "  sniplyngo engine target <code>         set the target language (en, zh-Hans, ...)\n"
        "  sniplyngo engine test [<id>|--all]     send one word through the engine(s) and report\n"
        "  sniplyngo proxy                        show the proxy in effect\n"
        "  sniplyngo proxy system|none            use the environment proxy, or connect directly\n"
        "  sniplyngo proxy manual <url>           e.g. http://127.0.0.1:7890 or "
        "socks5://127.0.0.1:1080\n"
        "  sniplyngo version             print the version\n"
        "\n"
        "Exit codes: 0 success, 2 the requested action is not available in this build.\n");
}

bool requiresGui(const QStringList &arguments) {
    if (arguments.isEmpty())
        return true;  // no command: open the application window
    const QString first = arguments.first();
    return first == QLatin1String("settings") || first == QLatin1String("trigger");
}

CommandLine parseCommandLine(const QStringList &arguments) {
    CommandLine parsed;
    if (arguments.isEmpty())
        return parsed;  // Settings: the application window

    const QString first = arguments.first();
    const QStringList rest = arguments.mid(1);

    if (first == QLatin1String("help") || first == QLatin1String("--help")
        || first == QLatin1String("-h")) {
        parsed.command = CommandLine::Command::Help;
        return parsed;
    }
    if (first == QLatin1String("version") || first == QLatin1String("--version")) {
        parsed.command = CommandLine::Command::Version;
        return parsed;
    }
    if (first == QLatin1String("caps") || first == QLatin1String("diag")) {
        parsed.command = first == QLatin1String("caps") ? CommandLine::Command::Caps
                                                        : CommandLine::Command::Diag;
        for (const QString &argument : rest) {
            if (argument == QLatin1String("--json")) {
                parsed.json = true;
                continue;
            }
            parsed.error = QStringLiteral("unexpected argument: %1").arg(argument);
            return parsed;
        }
        return parsed;
    }
    if (first == QLatin1String("settings")) {
        parsed.command = CommandLine::Command::Settings;
        if (!rest.isEmpty())
            parsed.error = QStringLiteral("settings takes no arguments");
        return parsed;
    }
    if (first == QLatin1String("trigger")) {
        parsed.command = CommandLine::Command::Trigger;
        if (rest.size() != 1) {
            parsed.error = QStringLiteral(
                "trigger needs exactly one flow: selection, region, clipboard or text");
            return parsed;
        }
        const QString flow = rest.first();
        if (flow != QLatin1String("selection") && flow != QLatin1String("region")
            && flow != QLatin1String("clipboard") && flow != QLatin1String("text")) {
            parsed.error = QStringLiteral("unknown flow: %1").arg(flow);
            return parsed;
        }
        parsed.triggerFlow = flow;
        return parsed;
    }
    if (first == QLatin1String("engine")) {
        parsed.command = CommandLine::Command::Engine;
        if (rest.isEmpty()) {
            parsed.error = QStringLiteral("engine needs a subcommand: list, use, target or test");
            return parsed;
        }
        parsed.engineAction = rest.first();
        const QStringList tail = rest.mid(1);
        if (parsed.engineAction == QLatin1String("list")) {
            for (const QString &argument : tail) {
                if (argument == QLatin1String("--json"))
                    parsed.json = true;
                else
                    parsed.error = QStringLiteral("unexpected argument: %1").arg(argument);
            }
            return parsed;
        }
        if (parsed.engineAction == QLatin1String("test")) {
            for (const QString &argument : tail) {
                if (argument == QLatin1String("--all"))
                    parsed.all = true;
                else if (parsed.engineArgument.isEmpty())
                    parsed.engineArgument = argument;
                else
                    parsed.error = QStringLiteral("unexpected argument: %1").arg(argument);
            }
            return parsed;
        }
        if (parsed.engineAction == QLatin1String("add")) {
            if (tail.size() != 3) {
                parsed.error = QStringLiteral("engine add needs an id, a base URL and a model");
                return parsed;
            }
            parsed.engineArgument = tail.join(QLatin1Char(' '));
            return parsed;
        }
        if (parsed.engineAction == QLatin1String("use")
            || parsed.engineAction == QLatin1String("target")) {
            if (tail.size() != 1) {
                parsed.error = QStringLiteral("engine %1 needs exactly one argument")
                                   .arg(parsed.engineAction);
                return parsed;
            }
            parsed.engineArgument = tail.first();
            return parsed;
        }
        parsed.error = QStringLiteral("unknown engine subcommand: %1").arg(parsed.engineAction);
        return parsed;
    }
    if (first == QLatin1String("proxy")) {
        parsed.command = CommandLine::Command::Proxy;
        if (rest.isEmpty())
            return parsed;
        parsed.proxyMode = rest.first();
        if (parsed.proxyMode == QLatin1String("manual")) {
            if (rest.size() != 2) {
                parsed.error = QStringLiteral("proxy manual needs a URL");
                return parsed;
            }
            parsed.proxyUrl = rest.at(1);
        } else if (parsed.proxyMode != QLatin1String("system")
            && parsed.proxyMode != QLatin1String("none")) {
            parsed.error = QStringLiteral("unknown proxy mode: %1").arg(parsed.proxyMode);
        } else if (rest.size() != 1) {
            parsed.error = QStringLiteral("proxy %1 takes no arguments").arg(parsed.proxyMode);
        }
        return parsed;
    }
    if (first == QLatin1String("watch")) {
        parsed.command = CommandLine::Command::Watch;
        for (int i = 0; i < rest.size(); ++i) {
            const QString argument = rest.at(i);
            if (argument == QLatin1String("--show-text")) {
                parsed.showText = true;
                continue;
            }
            if (argument == QLatin1String("--duration")) {
                if (i + 1 >= rest.size()) {
                    parsed.error = QStringLiteral("--duration needs a value in milliseconds");
                    return parsed;
                }
                bool ok = false;
                const int value = rest.at(++i).toInt(&ok);
                if (!ok || value < 100 || value > 600000) {
                    parsed.error = QStringLiteral("--duration must be 100..600000 ms");
                    return parsed;
                }
                parsed.durationMs = value;
                continue;
            }
            parsed.error = QStringLiteral("unexpected argument: %1").arg(argument);
            return parsed;
        }
        return parsed;
    }

    parsed.error = QStringLiteral("unknown command: %1").arg(first);
    return parsed;
}

}  // namespace app
