# M0 selection probe

Disposable feasibility instrumentation for the highest-risk part of the product: **getting selected text out
of another application without a shortcut, a clipboard read or input injection.** It exists to answer the open
questions in [`docs/design/04-selection.md`](../../docs/design/04-selection.md) §13 with measurements from a
real desktop, not from documentation.

Not part of the release build: own CMake projects, no CI wiring. Delete it once M1 has a supported backend.

## Two binaries, and why

| Binary | Links | Modes | Why separate |
| --- | --- | --- | --- |
| `snip-probe` | Qt 6 (Core, DBus) + xcb | `caps`, `x11 selftest`, `x11 watch`, `atspi dbus-events` | Report generation, X11 work, and the QtDBus experiment |
| `snip-atspi-probe` | GLib / libatspi only, **system runtime** | `diag`, `list`, `watch` | The first real run showed the Qt build loading conda `libdbus`/`libglib` while `libatspi` came from the system, and `atspi_init()` failed. A measuring tool must not mix runtimes. |

| Mode | Question it answers |
| --- | --- |
| `snip-probe caps` | Which session, portals and tools exist here? Is the accessibility bus reachable? |
| `snip-probe x11 selftest` | Does XFixes ownership notification + selection conversion work, with no human and no app? |
| `snip-probe x11 watch` | Which applications publish PRIMARY changes, how fast is text resolved, does INCR occur? |
| `snip-probe atspi dbus-events` | Can a plain QtDBus client receive the events (option A vs B)? |
| `snip-atspi-probe diag` | Bus pre-flight, `atspi_init()` result, and which libdbus/libatspi/libglib were actually loaded |
| `snip-atspi-probe list` | Which applications expose accessible text (coverage *hint*) |
| `snip-atspi-probe watch` | Do events arrive per application, what do ranges/coordinates look like, what are the latencies? |

## Methodology note

Run the watcher from a **different** terminal or window than the one you select text in. Otherwise the tty
echoes your keys (`^[[1;2D` for Shift+Left) into the watcher's own output, and the events you are measuring
are produced by the terminal that is hosting the measurement.

## Privacy rules (read before running)

- Selection content is **never printed** unless you pass `--show-text`, and **never stored**.
- `--out FILE` records only sequence numbers, timings, application name, role, character count and an
  8-hex-digit non-cryptographic correlation hash.
- Do not use `--show-text` while selecting real private content.
- Reports contain desktop names, portal versions and library paths only — safe to attach to an issue.

## Build

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
                 qt6-base-dev libglib2.0-dev libatspi2.0-dev \
                 libxcb1-dev libxcb-xfixes0-dev

# QT_PREFIX only if Qt is outside the default prefix (e.g. a conda environment)
QT_PREFIX=/path/to/qt prototypes/m0-selection-probe/build.sh
```

`build.sh` builds both binaries and then **verifies** that the AT-SPI probe has no RPATH and resolves
`libdbus`/`libglib`/`libatspi` from the system. It refuses to continue otherwise, because a mixed runtime
produces a false "AT-SPI unavailable" result (observed in Run 1). If the check fails, build from a shell where
conda is not activated, or pass `QT_PREFIX` for the Qt binary only.

Without `libatspi2.0-dev` the AT-SPI binary is skipped and the Qt probe still builds.

## One command (preferred)

```bash
prototypes/m0-selection-probe/run-m0.sh
```

Builds both binaries, runs the capability report, the AT-SPI prerequisites, the X11 self-test and an
**AT-SPI closed loop against a GTK publisher the script starts itself** (so no selection has to be made by
hand), then writes `RESULTS-auto.md` with a computed verdict. ~40 s, unattended.

## Manual run plan (only when a specific question remains)

Record everything in [`RESULTS.md`](RESULTS.md). Exit codes: `0` observed, `1` nothing observed, `2`
capability unavailable — a `2` is a result, not a probe failure.

```bash
B=build/m0/snip-probe
A=build/m0-atspi/snip-atspi-probe

$B caps                                        # portals, session, a11y bus
$B x11 selftest --count 5                      # mechanism proof, no interaction needed
$A diag                                        # provenance + atspi_init
$A list --depth 2                              # coverage hint per application
$A watch --duration 90000 --out /tmp/atspi.jsonl      # select text while this runs
$B x11 watch --duration 60000 --out /tmp/x11.jsonl    # select text while this runs
$B atspi dbus-events --duration 60000          # option A experiment
```

Selection sequence to use for both watch runs: browser page text, an editable field, a terminal, a native GTK
app, a native Qt app, then repeated drag-selection inside one application (the X11 ownership-notification
question). Then repeat once with a password field, once with a multi-element page selection.

## Environment findings so far (Run 1)

- `GlobalShortcuts` portal **not exported** on the tested GNOME session; `ScreenCast` 4, `Screenshot` 2,
  `Secret` 1. Screenshot v2 has no area target → portal capture would be full-screen plus our own crop.
- AT-SPI `RegisterEvent` is `(s)` on at-spi2-core 2.44, not `(s, a{sv})`.
- XFixes selection events must be matched as `first_event + subtype`; matching the bare constant silently
  yields zero events (this bug invalidated the first self-test run).
- `atspi_init()` aborts the process when the accessibility bus is unreachable, so both binaries pre-check
  `org.a11y.Bus.GetAddress`.

Details and the rerun checklist: [`RESULTS.md`](RESULTS.md).

## What each observation means for the design

| Observation | Design consequence |
| --- | --- |
| `x11 selftest` passes | The X11 primary path is mechanically sound; `04-selection.md` §7.1 can be frozen |
| `x11 selftest` fails on conversion | Do not promise automatic selection on X11 |
| AT-SPI events arrive for ≥ 2 of {browser page, editable, terminal, native app} | AT-SPI becomes the M1 backend on Wayland |
| AT-SPI events arrive but text is empty/partial | Coverage is narrower than hoped; the paste fallback becomes the default expectation |
| `atspi dbus-events` receives events | Option A delivers events, but sender attribution stays unsolved (QtDBus slots do not expose the message sender) |
| `diag` shows `atspi_init` failing with a foreign libdbus path | Environment problem, not a desktop limitation — fix the build, then re-measure |
| Resolve latency p95 > 200 ms | Revisit the stability delay and the latency budget in `05-coordinator.md` |

## Known limitations of the probe

- No hung-application test, so libatspi's internal D-Bus timeouts are not measured (documented in the module
  design instead of pretended here).
- No compositor window-placement measurement (a UI question, `07-ui.md` §15).
- Wayland data-control (M2 candidate) is not implemented: it needs in-tree protocol XML and a compositor that
  exposes the global.
- The probes do not filter this application's own windows; that filter is a product requirement
  (`04-selection.md` §5.4).
