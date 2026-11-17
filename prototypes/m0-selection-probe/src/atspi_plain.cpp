// snip-atspi-probe — AT-SPI feasibility probe WITHOUT Qt.
//
// Why a separate binary: the first real run showed the Qt build resolving libdbus-1 and libglib
// from a conda environment while libatspi came from the system, and atspi_init() then failed. A
// measuring tool must not mix two runtimes; this target links only GLib/libatspi so `ldd` is
// all-system.
//
// Modes:
//   list  [--depth N] [--json]                 accessible applications + text-interface coverage
//   hint watch [--duration MS] [--show-text] [--out FILE] diag bus pre-flight (GDBus) + atspi_init
//   + loaded libraries
//
// Privacy: selection content is never printed unless --show-text is given; --out stores lengths and
// a non-cryptographic correlation hash only, never text.
#include <algorithm>
#include <atspi/atspi.h>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dbus/dbus.h>
#include <fstream>
#include <gio/gio.h>
#include <glib.h>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_interrupted = 0;

void onSignal(int) {
    g_interrupted = 1;
}

class Stopwatch {
public:
    Stopwatch()
        : m_start(std::chrono::steady_clock::now()) { }

    double ms() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start)
            .count();
    }

private:
    std::chrono::steady_clock::time_point m_start;
};

// FNV-1a 64-bit: correlation token for repeated selections, not a security hash. Truncated to 8
// hex.
std::string digest8(const std::string &text) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(buffer, 8);
}

void logToStdout(
    const gchar *domain, GLogLevelFlags level, const gchar *message, gpointer /*data*/) {
    const char *tag = "INFO";
    if (level & G_LOG_LEVEL_ERROR)
        tag = "ERROR";
    else if (level & G_LOG_LEVEL_CRITICAL)
        tag = "CRITICAL";
    else if (level & G_LOG_LEVEL_WARNING)
        tag = "WARNING";
    std::printf("  [glib %s %s] %s\n", tag, domain ? domain : "-", message ? message : "");
    std::fflush(stdout);
}

std::string takeGError(GError **error) {
    if (!error || !*error)
        return {};
    std::string message = (*error)->message ? (*error)->message : "unknown error";
    g_error_free(*error);
    *error = nullptr;
    return message;
}

bool flag(int argc, char **argv, const char *name) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0)
            return true;
    return false;
}

const char *stringArg(int argc, char **argv, const char *name) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0)
            return argv[i + 1];
    return nullptr;
}

int intArg(int argc, char **argv, const char *name, int fallback) {
    const char *value = stringArg(argc, argv, name);
    if (!value)
        return fallback;
    return std::atoi(value);
}

// Chromium and Electron enable their accessibility tree based on this desktop-wide status; without
// it they may expose almost nothing and emit no text events at all. Reading it is part of the
// diagnosis.
void printAccessibilityStatus() {
    std::printf("desktop accessibility status:\n");

    GError *error = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!bus) {
        std::printf("  org.a11y.Status: unavailable (%s)\n", takeGError(&error).c_str());
    } else {
        for (const char *property : {"IsEnabled", "ScreenReaderEnabled"}) {
            GVariant *reply = g_dbus_connection_call_sync(bus, "org.a11y.Status",
                "/org/a11y/status", "org.freedesktop.DBus.Properties", "Get",
                g_variant_new("(ss)", "org.a11y.Status", property), G_VARIANT_TYPE("(v)"),
                G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
            if (!reply) {
                const std::string reason = takeGError(&error);
                // GNOME does not always export org.a11y.Status; gsettings is then authoritative.
                std::printf("  org.a11y.Status.%s = <not exposed: %s>\n", property,
                    reason.empty() ? "unknown" : reason.c_str());
                continue;
            }
            GVariant *boxed = nullptr;
            g_variant_get(reply, "(v)", &boxed);
            const gboolean value = g_variant_get_boolean(boxed);
            std::printf("  org.a11y.Status.%s = %s\n", property, value ? "true" : "false");
            g_variant_unref(boxed);
            g_variant_unref(reply);
        }
        g_object_unref(bus);
    }

    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = source
        ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE)
        : nullptr;
    if (schema && g_settings_schema_has_key(schema, "toolkit-accessibility")) {
        GSettings *settings = g_settings_new("org.gnome.desktop.interface");
        std::printf("  toolkit-accessibility (gsettings) = %s\n",
            g_settings_get_boolean(settings, "toolkit-accessibility") ? "true" : "false");
        g_object_unref(settings);
    } else {
        std::printf("  toolkit-accessibility (gsettings) = <schema unavailable>\n");
    }
    if (schema)
        g_settings_schema_unref(schema);
    std::printf("  note: Chromium/Electron key their content accessibility tree off this status;\n"
                "        when it is false they may expose focus events only.\n");
    std::fflush(stdout);
}

void printLoadedLibraries() {
    std::printf("loaded libraries (provenance matters for AT-SPI):\n");
    std::ifstream maps("/proc/self/maps");
    std::string line;
    std::vector<std::string> seen;
    while (std::getline(maps, line)) {
        for (const char *needle : {"libatspi", "libdbus-1", "libglib-2.0", "libgobject-2.0"}) {
            if (line.find(needle) == std::string::npos)
                continue;
            const auto slash = line.find('/');
            if (slash == std::string::npos)
                continue;
            const std::string path = line.substr(slash);
            bool duplicate = false;
            for (const std::string &existing : seen)
                duplicate = duplicate || existing == path;
            if (duplicate)
                continue;
            seen.push_back(path);
            std::printf("  %s\n", path.c_str());
        }
    }
    if (seen.empty())
        std::printf("  (none found — is libatspi actually loaded?)\n");
}

// GDBus (GLib) pre-flight: proves that the session bus and org.a11y.Bus work independently of
// libatspi's own libdbus path. A failure here is an environment problem; a success followed by
// atspi_init() failure points at the libdbus side.
bool gdbusPreflight(std::string *address, std::string *error) {
    GError *gerror = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &gerror);
    if (!bus) {
        *error = "session bus unreachable via GDBus: " + takeGError(&gerror);
        return false;
    }
    GVariant *reply = g_dbus_connection_call_sync(bus, "org.a11y.Bus", "/org/a11y/bus",
        "org.a11y.Bus", "GetAddress", nullptr, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 3000,
        nullptr, &gerror);
    if (!reply) {
        *error = "org.a11y.Bus.GetAddress failed: " + takeGError(&gerror);
        g_object_unref(bus);
        return false;
    }
    const char *value = nullptr;
    g_variant_get(reply, "(&s)", &value);
    *address = value ? value : "";
    g_variant_unref(reply);
    g_object_unref(bus);
    return true;
}

bool textInterfacePresent(AtspiAccessible *accessible) {
    AtspiText *text = atspi_accessible_get_text_iface(accessible);
    if (!text)
        return false;
    g_object_unref(text);
    return true;
}

std::string roleName(AtspiAccessible *accessible) {
    GError *error = nullptr;
    gchar *role = atspi_accessible_get_role_name(accessible, &error);
    takeGError(&error);
    std::string name = role ? role : "?";
    g_free(role);
    return name;
}

int countTextObjects(
    AtspiAccessible *root, int depth, int *scanned, const Stopwatch &budget, int budgetMs) {
    if (depth <= 0 || budget.ms() > budgetMs)
        return 0;
    GError *error = nullptr;
    const int children = atspi_accessible_get_child_count(root, &error);
    takeGError(&error);
    int found = 0;
    for (int i = 0; i < children && i < 30; ++i) {
        GError *childError = nullptr;
        AtspiAccessible *child = atspi_accessible_get_child_at_index(root, i, &childError);
        takeGError(&childError);
        if (!child)
            continue;
        ++(*scanned);
        if (textInterfacePresent(child))
            ++found;
        found += countTextObjects(child, depth - 1, scanned, budget, budgetMs);
        g_object_unref(child);
    }
    return found;
}

struct WatchState {
    std::ofstream record;
    bool showText = false;
    bool group = false;  // aggregate instead of printing every event
    int maxPrint = 200;  // per-event printing cap; a wildcard subscription floods
    std::vector<std::string> eventTypes;
    std::vector<std::string> appFilters;  // case-insensitive substrings; empty means all apps
    int retryMs = 0;  // re-read a zero-length/empty selection after this delay
    std::map<std::string, int> groupCounts;  // "app | event type" -> count
    int filteredOut = 0;
    int suppressed = 0;
    int events = 0;
    std::vector<double> appTimes;
    std::vector<double> textTimes;
    Stopwatch total;
};

WatchState g_state;

std::string lowercase(std::string value) {
    for (char &c : value)
        c = char(g_ascii_tolower(c));
    return value;
}

std::string accessibleAppName(AtspiAccessible *source, double *elapsedMs) {
    Stopwatch watch;
    std::string app = "<unknown>";
    GError *error = nullptr;
    AtspiAccessible *application = atspi_accessible_get_application(source, &error);
    takeGError(&error);
    if (application) {
        gchar *name = atspi_accessible_get_name(application, &error);
        takeGError(&error);
        if (name) {
            app = name;
            g_free(name);
        }
        g_object_unref(application);
    }
    if (elapsedMs)
        *elapsedMs = watch.ms();
    return app;
}

bool appSelected(const std::string &app) {
    if (g_state.appFilters.empty())
        return true;
    const std::string name = lowercase(app);
    for (const std::string &filter : g_state.appFilters)
        if (name.find(filter) != std::string::npos)
            return true;
    return false;
}

void onSelectionChanged(AtspiEvent *event, void * /*userData*/) {
    const int seq = ++g_state.events;
    const double observedAt = g_state.total.ms();

    const char *eventType = (event && event->type) ? event->type : "?";
    const int detail1 = event ? event->detail1 : 0;

    std::string app = "<unknown>";
    std::string role = "?";
    std::string errorText;
    double appMs = 0.0;

    if (event && event->source)
        app = accessibleAppName(event->source, &appMs);

    // Application filter: a wildcard subscription otherwise drowns in desktop-shell events.
    if (!appSelected(app)) {
        ++g_state.filteredOut;
        return;
    }

    // Grouping mode: count only, no text reads, no per-event output.
    if (g_state.group) {
        ++g_state.groupCounts[app + " | " + eventType];
        return;
    }

    if (g_state.events > g_state.maxPrint) {
        ++g_state.suppressed;
        return;
    }
    std::string text;
    int ranges = 0;
    int chars = 0;
    int anchorX = -1;
    int anchorY = -1;
    double textMs = 0.0;

    if (!event || !event->source) {
        errorText = "event without source";
    } else {
        AtspiAccessible *source = event->source;
        role = roleName(source);

        const bool selectionEvent = std::strstr(eventType, "text-selection-changed") != nullptr;
        if (!selectionEvent) {
            // Diagnostic mode (--events object): report that the application emitted *something*
            // without reading any text. This is how we find out whether an app emits no events at
            // all.
            std::printf("seq=%d t=%.1fms app=%s role=%s type=%s detail1=%d\n", seq, observedAt,
                app.c_str(), role.c_str(), eventType, detail1);
            std::fflush(stdout);
            return;
        }
        if (g_ascii_strcasecmp(role.c_str(), "password text") == 0) {
            errorText = "protected (password text) — not resolved";
        } else {
            Stopwatch textWatch;
            AtspiText *textIface = atspi_accessible_get_text_iface(source);
            if (!textIface) {
                errorText = "source exposes no text interface";
            } else {
                std::vector<int> starts;
                std::vector<int> ends;
                int characterCount = -1;

                auto readOnce = [&](bool printOffsets) {
                    ranges = 0;
                    text.clear();
                    errorText.clear();
                    starts.clear();
                    ends.clear();
                    GError *readError = nullptr;
                    const int selections = atspi_text_get_n_selections(textIface, &readError);
                    const std::string selectionsError = takeGError(&readError);
                    if (selections < 0) {
                        errorText = "GetNSelections failed: " + selectionsError;
                        return;
                    }
                    if (selections == 0) {
                        errorText = "no selection ranges (event without text)";
                        return;
                    }
                    ranges = selections;
                    characterCount = atspi_text_get_character_count(textIface, &readError);
                    takeGError(&readError);
                    for (int i = 0; i < selections; ++i) {
                        AtspiRange *range = atspi_text_get_selection(textIface, i, &readError);
                        const std::string rangeError = takeGError(&readError);
                        if (!range) {
                            errorText = "GetSelection failed: " + rangeError;
                            break;
                        }
                        const int start = range->start_offset;
                        const int end = range->end_offset;
                        g_free(range);
                        starts.push_back(start);
                        ends.push_back(end);
                        if (printOffsets) {
                            std::printf("      range[%d] = [%d, %d) len=%d of characterCount=%d\n",
                                i, start, end, end - start, characterCount);
                            std::fflush(stdout);
                        }
                        gchar *chunk = atspi_text_get_text(textIface, start, end, &readError);
                        const std::string chunkError = takeGError(&readError);
                        if (!chunk) {
                            errorText = "GetText failed: " + chunkError;
                            break;
                        }
                        if (!text.empty())
                            text += "\n";
                        text += chunk;
                        g_free(chunk);
                    }
                    if (errorText.empty())
                        chars = int(text.size());
                };

                readOnce(true);

                // A zero-length range at a caret position means the toolkit had not yet updated the
                // selection when the event fired. Re-reading after a short delay separates "no text
                // available" from "read too early" without changing anything else.
                if (g_state.retryMs > 0 && (chars == 0 || ranges == 0)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(g_state.retryMs));
                    readOnce(false);
                    std::printf("      retry after %d ms: ranges=%d chars=%d%s%s\n",
                        g_state.retryMs, ranges, chars, starts.empty() ? "" : " range=",
                        starts.empty() ? ""
                                       : ("[" + std::to_string(starts.front()) + ", "
                                             + std::to_string(ends.front()) + ")")
                                             .c_str());
                    std::fflush(stdout);
                    if (chars > 0)
                        errorText.clear();
                }

                if (errorText.empty() && chars > 0 && !starts.empty()) {
                    GError *rectError = nullptr;
                    AtspiRect *rect = atspi_text_get_range_extents(textIface, starts.front(),
                        ends.front(), ATSPI_COORD_TYPE_SCREEN, &rectError);
                    if (rect) {
                        anchorX = rect->x;
                        anchorY = rect->y;
                        g_free(rect);
                    } else {
                        takeGError(&rectError);
                    }
                }
                g_object_unref(textIface);
            }
            textMs = textWatch.ms();
            g_state.textTimes.push_back(textMs);
        }
        g_state.appTimes.push_back(appMs);
    }

    if (errorText.empty()) {
        std::printf(
            "seq=%d t=%.1fms app=%s role=%s type=%s ranges=%d chars=%d digest=%s app_ms=%.1f "
            "text_ms=%.1f anchor=(%d,%d)\n",
            seq, observedAt, app.c_str(), role.c_str(), eventType, ranges, chars,
            digest8(text).c_str(), appMs, textMs, anchorX, anchorY);
        if (g_state.showText) {
            std::string preview = text.substr(0, 80);
            for (char &c : preview)
                if (c == '\n')
                    c = ' ';
            std::printf("    text: \"%s\"\n", preview.c_str());
        }
    } else {
        std::printf("seq=%d t=%.1fms app=%s role=%s SKIPPED: %s\n", seq, observedAt, app.c_str(),
            role.c_str(), errorText.c_str());
    }
    std::fflush(stdout);

    if (g_state.record.is_open()) {
        std::string escapedApp = app;
        for (char &c : escapedApp)
            if (c == '"' || c == '\\')
                c = '_';
        g_state.record << "{\"backend\":\"atspi\",\"seq\":" << seq
                       << ",\"observe_ms\":" << observedAt << ",\"app\":\"" << escapedApp
                       << "\",\"role\":\"" << role << "\",\"ranges\":" << ranges
                       << ",\"chars\":" << chars << ",\"digest\":\"" << digest8(text)
                       << "\",\"app_ms\":" << appMs << ",\"text_ms\":" << textMs
                       << ",\"anchor_x\":" << anchorX << ",\"anchor_y\":" << anchorY
                       << ",\"error\":\"" << errorText << "\"}\n";
        g_state.record.flush();
    }
}

double percentile(const std::vector<double> &values, double p) {
    if (values.empty())
        return 0.0;
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const size_t index = size_t(p / 100.0 * double(sorted.size() - 1) + 0.5);
    return sorted[index < sorted.size() ? index : sorted.size() - 1];
}

void printWatchSummary() {
    std::printf("\n== atspi summary ==\n");
    std::printf("events observed          : %d\n", g_state.events);
    if (g_state.filteredOut > 0)
        std::printf("ignored by --app filter  : %d\n", g_state.filteredOut);
    if (g_state.suppressed > 0)
        std::printf("not printed (--max-print): %d\n", g_state.suppressed);
    if (!g_state.groupCounts.empty()) {
        std::vector<std::pair<std::string, int>> rows(
            g_state.groupCounts.begin(), g_state.groupCounts.end());
        std::sort(rows.begin(), rows.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });
        std::printf("\napp | event type | count\n");
        for (const auto &row : rows)
            std::printf("%s | %d\n", row.first.c_str(), row.second);
    }
    if (!g_state.appTimes.empty())
        std::printf("app lookup ms p50/p95    : %.1f / %.1f\n", percentile(g_state.appTimes, 50),
            percentile(g_state.appTimes, 95));
    if (!g_state.textTimes.empty())
        std::printf("text resolve ms p50/p95  : %.1f / %.1f\n", percentile(g_state.textTimes, 50),
            percentile(g_state.textTimes, 95));
    if (g_state.events == 0)
        std::printf(
            "no events: run 'list' first; the source app may not expose accessibility text\n");
    std::printf("note: libatspi keeps its own D-Bus timeouts, so hung-application deadlines are "
                "not measured\n");
    std::fflush(stdout);
}

// Simulates a loop that never returns, to prove the probe's own deadline still exits. A measurement
// tool that can hang is worse than one that reports less.
int runExitSelfTest(int durationMs) {
    std::thread never([] {
        for (;;)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    });
    never.detach();

    Stopwatch window;
    while (!g_interrupted && window.ms() < durationMs)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::printf("selftest-exit: deadline reached after %.0f ms (requested %d ms), exiting\n",
        window.ms(), durationMs);
    std::fflush(stdout);
    std::_Exit(0);
}

int runDiag() {
    std::string address;
    std::string error;
    const bool preflight = gdbusPreflight(&address, &error);
    std::printf("GDBus pre-flight (session bus + org.a11y.Bus): %s\n", preflight ? "ok" : "FAILED");
    if (!preflight)
        std::printf("  reason: %s\n", error.c_str());

    const char *atspiBus = std::getenv("AT_SPI_BUS");
    const char *atspiBusAddress = std::getenv("AT_SPI_BUS_ADDRESS");
    std::printf("AT_SPI_BUS=%s AT_SPI_BUS_ADDRESS=%s\n", atspiBus ? atspiBus : "<unset>",
        atspiBusAddress ? atspiBusAddress : "<unset>");
    printLoadedLibraries();
    printAccessibilityStatus();

    if (!preflight)
        return 2;

    // libdbus is what libatspi actually uses; GLib's GDBus succeeding says nothing about it.
    {
        DBusError dbusError;
        dbus_error_init(&dbusError);
        DBusConnection *session = dbus_bus_get(DBUS_BUS_SESSION, &dbusError);
        if (session) {
            std::printf("libdbus session bus   : ok (%s)\n", dbus_bus_get_unique_name(session));
            dbus_connection_unref(session);
        } else {
            std::printf("libdbus session bus   : FAILED — %s\n",
                dbusError.message ? dbusError.message : "unknown");
            dbus_error_free(&dbusError);
        }

        dbus_error_init(&dbusError);
        DBusConnection *a11y = dbus_connection_open_private(address.c_str(), &dbusError);
        if (a11y) {
            std::printf("libdbus a11y bus      : ok%s\n",
                dbus_bus_register(a11y, &dbusError) ? " (registered)" : "");
            dbus_connection_close(a11y);
            dbus_connection_unref(a11y);
        } else {
            std::printf("libdbus a11y bus      : FAILED — %s\n",
                dbusError.message ? dbusError.message : "unknown");
            dbus_error_free(&dbusError);
        }
    }

    // libatspi explains itself through GLib log messages; capture them instead of guessing.
    g_log_set_default_handler(logToStdout, nullptr);
    std::printf("calling atspi_init() with GLib logging captured below\n");
    std::fflush(stdout);

    // atspi_init() is `int atspi_init(void)` returning 0 on success and non-zero on failure.
    // Reading it as a boolean inverts the result; the previous "atspi_init failed" reports were
    // this bug.
    const int initResult = atspi_init();
    std::printf("atspi_init(): return %d (%s), atspi_is_initialized() = %s\n", initResult,
        initResult == 0 ? "success" : "FAILURE", atspi_is_initialized() ? "yes" : "no");
    const bool initialised = (initResult == 0);
    if (!initialised) {
        std::printf(
            "  hint: libatspi talks over libdbus, not GLib. If the GLib pre-flight above works "
            "but\n"
            "        this fails, compare the libdbus path listed above with the system one\n"
            "        (/usr/lib/x86_64-linux-gnu/libdbus-1.so.3): two loaded copies or a conda\n"
            "        copy break the connection even though the bus itself is fine.\n");
        return 2;
    }
    std::printf("desktops: %d\n", atspi_get_desktop_count());
    atspi_exit();
    return 0;
}

int runList(int depth, bool asJson) {
    std::string address;
    std::string error;
    if (!gdbusPreflight(&address, &error)) {
        std::printf("atspi: accessibility bus unavailable — %s\n", error.c_str());
        return 2;
    }
    if (atspi_init() != 0) {
        std::printf("atspi: atspi_init failed (run 'snip-atspi-probe diag' for details)\n");
        return 2;
    }

    Stopwatch budget;
    const int desktops = atspi_get_desktop_count();
    if (!asJson) {
        std::printf("desktops: %d, scan depth: %d\n", desktops, depth);
        std::printf("%-30s %-8s %-18s %s\n", "application", "pid", "role", "textObjects/scanned");
    } else {
        std::printf("{\"apps\":[");
    }

    int appCount = 0;
    for (int d = 0; d < desktops; ++d) {
        AtspiAccessible *desktop = atspi_get_desktop(d);
        if (!desktop)
            continue;
        GError *gerror = nullptr;
        const int children = atspi_accessible_get_child_count(desktop, &gerror);
        takeGError(&gerror);
        for (int i = 0; i < children && budget.ms() < 20000; ++i) {
            GError *childError = nullptr;
            AtspiAccessible *app = atspi_accessible_get_child_at_index(desktop, i, &childError);
            takeGError(&childError);
            if (!app)
                continue;
            gchar *name = atspi_accessible_get_name(app, &gerror);
            takeGError(&gerror);
            const std::string appName = name ? name : "?";
            g_free(name);
            const int pid = atspi_accessible_get_process_id(app, &gerror);
            takeGError(&gerror);
            const std::string role = roleName(app);
            int scanned = 0;
            const int textObjects = countTextObjects(app, depth, &scanned, budget, 20000);
            ++appCount;
            if (asJson) {
                if (appCount > 1)
                    std::printf(",");
                std::printf("{\"name\":\"%s\",\"pid\":%d,\"role\":\"%s\",\"textObjects\":%d,"
                            "\"scanned\":%d}",
                    appName.c_str(), pid, role.c_str(), textObjects, scanned);
            } else {
                std::printf("%-30s %-8d %-18s %d/%d\n", appName.c_str(), pid, role.c_str(),
                    textObjects, scanned);
            }
            g_object_unref(app);
        }
        g_object_unref(desktop);
    }

    if (asJson)
        std::printf("]}\n");
    else
        std::printf(
            "\napps: %d (scan %d ms). textObjects>0 is a coverage hint, not proof that selecting\n"
            "text in that app emits object:text-selection-changed.\n",
            appCount, int(budget.ms()));

    atspi_exit();
    return appCount > 0 ? 0 : 1;
}

int runWatch(int durationMs, bool showText, const char *outPath) {
    std::string address;
    std::string error;
    if (!gdbusPreflight(&address, &error)) {
        std::printf("atspi: accessibility bus unavailable — %s\n", error.c_str());
        return 2;
    }
    if (atspi_init() != 0) {
        std::printf("atspi: atspi_init failed (run 'snip-atspi-probe diag' for details)\n");
        return 2;
    }

    if (showText)
        std::fprintf(stderr,
            "warning: --show-text prints selection content; do not use with sensitive text\n");
    if (outPath)
        g_state.record.open(outPath, std::ios::app);
    g_state.showText = showText;

    if (g_state.eventTypes.empty())
        g_state.eventTypes.push_back("object:text-selection-changed");

    AtspiEventListener *listener = atspi_event_listener_new(onSelectionChanged, nullptr, nullptr);
    if (!listener) {
        std::printf("atspi: cannot create listener\n");
        atspi_exit();
        return 2;
    }
    bool anyRegistered = false;
    for (const std::string &eventType : g_state.eventTypes) {
        GError *gerror = nullptr;
        const bool ok = atspi_event_listener_register(listener, eventType.c_str(), &gerror);
        std::printf("atspi: register %-34s : %s%s\n", eventType.c_str(), ok ? "ok" : "FAILED",
            ok ? "" : (" — " + takeGError(&gerror)).c_str());
        anyRegistered = anyRegistered || ok;
    }
    if (!anyRegistered) {
        atspi_exit();
        return 2;
    }

    std::printf(
        "atspi: listening for %zu event type(s) (%d ms)\n", g_state.eventTypes.size(), durationMs);
    std::printf("atspi: select text now (browser page, editable field, terminal, native app)\n");
    std::fflush(stdout);

    // atspi_event_main() blocks. At least one at-spi2-core build ignored atspi_event_quit() from
    // another thread, so the listener outlived --duration and the process had to be killed. Ask
    // politely, then leave: a probe must always terminate.
    std::thread loopThread([] { atspi_event_main(); });
    loopThread.detach();

    Stopwatch window;
    while (!g_interrupted && window.ms() < durationMs)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    printWatchSummary();
    atspi_event_quit();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::fflush(nullptr);
    std::_Exit(g_state.events > 0 ? 0 : 1);

    return g_state.events > 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv) {
    const char *mode = (argc > 1) ? argv[1] : "diag";
    std::signal(SIGINT, onSignal);

    if (std::strcmp(mode, "list") == 0)
        return runList(intArg(argc, argv, "--depth", 2), flag(argc, argv, "--json"));
    if (std::strcmp(mode, "watch") == 0) {
        if (const char *events = stringArg(argc, argv, "--events")) {
            std::string list(events);
            size_t start = 0;
            while (start <= list.size()) {
                const size_t comma = list.find(',', start);
                const std::string one = list.substr(
                    start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!one.empty())
                    g_state.eventTypes.push_back(one);
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
        }
        g_state.group = flag(argc, argv, "--group");
        g_state.maxPrint = intArg(argc, argv, "--max-print", 200);
        g_state.retryMs = intArg(argc, argv, "--retry-ms", 0);
        if (const char *apps = stringArg(argc, argv, "--app")) {
            std::string list(apps);
            size_t start = 0;
            while (start <= list.size()) {
                const size_t comma = list.find(',', start);
                const std::string one = lowercase(list.substr(
                    start, comma == std::string::npos ? std::string::npos : comma - start));
                if (!one.empty())
                    g_state.appFilters.push_back(one);
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
        }
        return runWatch(intArg(argc, argv, "--duration", 30000), flag(argc, argv, "--show-text"),
            stringArg(argc, argv, "--out"));
    }
    if (std::strcmp(mode, "diag") == 0)
        return runDiag();
    if (std::strcmp(mode, "selftest-exit") == 0)
        return runExitSelfTest(intArg(argc, argv, "--duration", 500));

    std::printf(
        "usage: snip-atspi-probe <list|watch|diag> [options]\n"
        "  list  [--depth N] [--json]\n"
        "  watch [--duration MS] [--show-text] [--events TYPE[,TYPE...]] [--out FILE]\n"
        "        [--app NAME[,NAME...]] [--group] [--max-print N] [--retry-ms N]\n"
        "        --retry-ms re-reads an empty/zero-length selection after N ms (race probe)\n"
        "        default event type: object:text-selection-changed\n"
        "        diagnostic: --events object    (all object events; proves whether an app emits "
        "any)\n"
        "  diag\n"
        "  selftest-exit [--duration MS]      proves the deadline exits even if the loop never "
        "returns\n");
    return 0;
}
