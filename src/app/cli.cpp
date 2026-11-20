#include "app/cli.h"

#include <QByteArray>

#include <unistd.h>

namespace app {

namespace {

// The help banner is the application mark: a rounded indigo tile holding two opposing arrows, with
// the wordmark beside it. The icon is a 26x16 pixel grid rendered with half blocks, so each
// terminal row carries two pixel rows; kIconPixels maps every pixel to one of the logo colours, and
// '.' leaves the terminal background visible.
struct LogoColor {
    int red;
    int green;
    int blue;
    int index256;
};

constexpr LogoColor kIndigo {88, 102, 226, 62};
constexpr LogoColor kRim {150, 165, 255, 111};
constexpr LogoColor kWhite {255, 255, 255, 231};
constexpr LogoColor kMint {162, 248, 217, 158};
// Lighter than the tile so the wordmark stays legible on dark and light terminals alike.
constexpr LogoColor kWordmark {124, 139, 255, 105};

constexpr int kIconWidth = 26;
constexpr int kIconLines = 8;  // the pixel grid has two rows per terminal line

const char *const kIconPixels[] = {
    "...rrrrrrrrrrrrrrrrrrrr...",
    ".rriiiiiiiiiiiiiiiiiiiirr.",
    ".riiiiiiiiiiiiiiwiiiiiiir.",
    "riiiiiiiiiiiiiiiwwwiiiiiir",
    "riiiiwwwwwwwwwwwwwwwwwiiir",
    "riiiiwwwwwwwwwwwwwwwiiiiir",
    "riiiiiiiiiiiiiiiwwwiiiiiir",
    "riiiiiiiimiiiiiiwiiiiiiiir",
    "riiiiiimmmiiiiiiiiiiiiiiir",
    "riiimmmmmmmmmmmmmmmmmiiiir",
    "riiiiimmmmmmmmmmmmmmmiiiir",
    "riiiiiimmmiiiiiiiiiiiiiiir",
    "riiiiiiiimiiiiiiiiiiiiiiir",
    ".riiiiiiiiiiiiiiiiiiiiiir.",
    ".rriiiiiiiiiiiiiiiiiiiirr.",
    "...rrrrrrrrrrrrrrrrrrrr...",
};

// The wordmark is drawn one character cell per font pixel: four columns per glyph keep the lockup
// within 80 columns, five rows keep the letter shapes close to the square icon pixels.
struct Glyph {
    char letter;
    const char *rows[5];
};

constexpr Glyph kWordmarkGlyphs[] = {
    {'S', {".###", "#...", ".##.", "...#", "###."}},
    {'N', {"#..#", "##.#", "#.##", "#..#", "#..#"}},
    {'I', {".###", "..#.", "..#.", "..#.", ".###"}},
    {'P', {"###.", "#..#", "###.", "#...", "#..."}},
    {'L', {"#...", "#...", "#...", "#...", "####"}},
    {'G', {".###", "#...", "#.##", "#..#", ".###"}},
    {'O', {".##.", "#..#", "#..#", "#..#", ".##."}},
};

constexpr char kWordmarkText[] = "SNIPLINGO";
constexpr int kWordmarkRows = 5;
// The icon is eight lines tall, so the five wordmark rows start two lines down to stay centred.
constexpr int kWordmarkTopRow = 2;

static_assert(kIconLines * 2 == int(sizeof(kIconPixels) / sizeof(kIconPixels[0])),
    "each terminal line carries two icon pixel rows");

const LogoColor *logoColor(char pixel) {
    switch (pixel) {
    case 'i':
        return &kIndigo;
    case 'r':
        return &kRim;
    case 'w':
        return &kWhite;
    case 'm':
        return &kMint;
    default:
        return nullptr;
    }
}

QString colorEscape(const LogoColor &color, bool foreground, bool trueColor) {
    const int layer = foreground ? 38 : 48;
    if (trueColor) {
        return QStringLiteral("\x1b[%1;2;%2;%3;%4m")
            .arg(layer)
            .arg(color.red)
            .arg(color.green)
            .arg(color.blue);
    }
    return QStringLiteral("\x1b[%1;5;%2m").arg(layer).arg(color.index256);
}

QStringList wordmarkLines() {
    QStringList lines;
    constexpr int letters = int(sizeof(kWordmarkText)) - 1;
    for (int row = 0; row < kWordmarkRows; ++row) {
        QString line;
        for (int index = 0; index < letters; ++index) {
            if (index > 0)
                line += QLatin1Char(' ');
            for (const Glyph &glyph : kWordmarkGlyphs) {
                if (glyph.letter != kWordmarkText[index])
                    continue;
                for (const char *pixel = glyph.rows[row]; *pixel != '\0'; ++pixel)
                    line += *pixel == '#' ? QChar(0x2588) : QLatin1Char(' ');
                break;
            }
        }
        lines.append(line);
    }
    return lines;
}

// Half blocks paint the upper pixel with the foreground and the lower pixel with the background,
// which keeps the icon at half the height of the pixel grid.
QString logoText(bool trueColor) {
    const QStringList wordmark = wordmarkLines();
    QString text;
    for (int line = 0; line < kIconLines; ++line) {
        const char *upper = kIconPixels[2 * line];
        const char *lower = kIconPixels[2 * line + 1];
        text += QStringLiteral("  ");
        for (int column = 0; column < kIconWidth; ++column) {
            const LogoColor *top = logoColor(upper[column]);
            const LogoColor *bottom = logoColor(lower[column]);
            if (!top && !bottom) {
                text += QLatin1String("\x1b[0m ");
                continue;
            }
            text += QLatin1String("\x1b[0m");
            if (top)
                text += colorEscape(*top, true, trueColor);
            if (bottom)
                text += colorEscape(*bottom, false, trueColor);
            text += top ? QStringLiteral("▀") : QStringLiteral("▄");
        }

        const int wordmarkRow = line - kWordmarkTopRow;
        if (wordmarkRow >= 0 && wordmarkRow < kWordmarkRows) {
            text += QLatin1String("\x1b[0m   ");
            text += colorEscape(kWordmark, true, trueColor);
            text += wordmark.at(wordmarkRow);
        }
        text += QLatin1String("\x1b[0m\n");
    }
    return text;
}

}  // namespace

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
        "  sniplyngo engine use <id|preset>       add a preset if needed and make it "
        "active\n"
        "                                presets: google-free | bing-free | youdao | "
        "huoshanweb | mymemory | libretranslate | deepl\n"
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

bool helpColorSupported() {
    if (qEnvironmentVariableIsSet("NO_COLOR"))
        return false;
    if (qgetenv("TERM") == QByteArrayLiteral("dumb"))
        return false;
    // Piped help output stays plain so that files, pagers without colour support and test logs do
    // not have to strip escape sequences.
    return ::isatty(STDOUT_FILENO) != 0;
}

QString helpText(bool color) {
    if (!color)
        return usageText();
    const QByteArray colorTerm = qgetenv("COLORTERM");
    const bool trueColor = colorTerm.contains(QByteArrayLiteral("truecolor"))
        || colorTerm.contains(QByteArrayLiteral("24bit"));
    return logoText(trueColor) + QLatin1Char('\n') + usageText();
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
