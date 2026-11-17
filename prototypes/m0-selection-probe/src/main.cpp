// M0 feasibility probe CLI. Throwaway: not part of the release build.
#include <QCoreApplication>
#include <QStringList>

#include <cstdio>
#include <cstring>

#include "common.h"

int runCaps(bool asJson);
int runX11Watch(int durationMs, bool showText, const QString &outPath, int maxEvents);
int runX11SelfTest(int iterations);
int runAtspiDbusEvents(int durationMs, const QString &outPath);

namespace {

void usage() {
    std::printf(
        "snip-probe — SnipLingo M0 selection feasibility probe (throwaway)\n"
        "\n"
        "  caps [--json]                     session, accessibility bus and portal versions\n"
        "  x11 selftest [--count N]          mechanism check without user interaction\n"
        "  x11 watch [--duration MS] [--show-text] [--out FILE] [--max N]\n"
        "  atspi dbus-events [--duration MS] [--out FILE]\n"
        "\n"
        "AT-SPI application scanning and watching live in the Qt-free binary:\n"
        "  snip-atspi-probe diag | list [--depth N] | watch [--duration MS] [--out FILE]\n"
        "  version\n"
        "\n"
        "Privacy: selection content is never printed unless --show-text is given, and never "
        "stored;\n"
        "--out records lengths and an 8-hex-digit digest only.\n"
        "\n"
        "Exit codes: 0 success, 1 nothing observed, 2 capability unavailable.\n");
}

int intArg(const QStringList &args, const QString &name, int fallback) {
    const int index = int(args.indexOf(name));
    if (index < 0 || index + 1 >= args.size())
        return fallback;
    bool ok = false;
    const int value = args.at(index + 1).toInt(&ok);
    return ok ? value : fallback;
}

QString stringArg(const QStringList &args, const QString &name) {
    const int index = int(args.indexOf(name));
    return (index < 0 || index + 1 >= args.size()) ? QString() : args.at(index + 1);
}

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("snip-probe"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0-m0"));

    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty() || args.first() == QLatin1String("help")
        || args.first() == QLatin1String("--help")) {
        usage();
        return 0;
    }
    if (args.first() == QLatin1String("version") || args.first() == QLatin1String("--version")) {
        std::printf("snip-probe %s\n", qPrintable(QCoreApplication::applicationVersion()));
        return 0;
    }

    const QString command = args.first();
    const bool showText = args.contains(QLatin1String("--show-text"));
    const QString outPath = stringArg(args, QStringLiteral("--out"));

    if (command == QLatin1String("caps"))
        return runCaps(args.contains(QLatin1String("--json")));

    if (command == QLatin1String("x11")) {
        const QString mode = args.value(1);
        if (mode == QLatin1String("selftest"))
            return runX11SelfTest(intArg(args, QStringLiteral("--count"), 5));
        if (mode == QLatin1String("watch"))
            return runX11Watch(intArg(args, QStringLiteral("--duration"), 30000), showText, outPath,
                intArg(args, QStringLiteral("--max"), 200));
        usage();
        return 0;
    }

    if (command == QLatin1String("atspi")) {
        const QString mode = args.value(1);
        if (mode == QLatin1String("dbus-events"))
            return runAtspiDbusEvents(intArg(args, QStringLiteral("--duration"), 30000), outPath);
        std::fprintf(stderr, "use ./snip-atspi-probe for '%s' (Qt-free binary; see --help)\n",
            qPrintable(mode));
        usage();
        return 2;
    }

    std::fprintf(stderr, "unknown command: %s\n\n", qPrintable(command));
    usage();
    return 0;
}
