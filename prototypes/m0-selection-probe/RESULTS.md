# M0 results

Fill this file from real runs. Never paste selection content, credentials or personal paths.
Exit codes: `0` observed, `1` nothing observed, `2` capability unavailable.

---

## Run 0 — sandboxed build check (no desktop access, no feasibility value)

Environment blocks `AF_UNIX` sockets (`socket(AF_UNIX)` → `EPERM`), so neither D-Bus nor X11 is reachable
although `XDG_SESSION_TYPE=wayland` and `DISPLAY=:0` are set. Purpose was only to prove that every mode
reports its own unavailability instead of crashing. All modes exited `2` with a specific reason, and
`caps --json` stays free of addresses and user data.

---

## Run 1 — GNOME Wayland, first attempt (2026-11-16)

| Field | Value |
| --- | --- |
| Session | `XDG_SESSION_TYPE=wayland`, `XDG_CURRENT_DESKTOP=ubuntu:GNOME`, `DISPLAY=:0` (XWayland) |
| Accessibility bus | session bus reachable: **yes**, a11y bus address: **present** |
| Portal `ScreenCast` | **4** |
| Portal `Screenshot` | **2** |
| Portal `Secret` | **1** |
| Portal `GlobalShortcuts` | **not exported** (`-1`) |
| Tools | `wl-paste` present; `xclip`, `xsel`, `dotool`, `ydotool`, `grim`, `slurp` missing |
| `x11 selftest --count 5` | **0 pass / 5 fail** — "no ownership notification" |
| `x11 watch` (60 s) | **0 events**, 0 resolved |
| `atspi list` / `atspi watch` (libatspi, Qt build) | `atspi_init failed`, exit `2` |
| `atspi dbus-events` (QtDBus, 60 s) | a11y bus connect **ok**, `AddMatch` **ok**, `RegisterEvent` **rejected** (`signature "sa{sv}" … doesn't exist`), 0 events |

### What this run established

1. **Global shortcuts are not available through the portal on this desktop.** `org.freedesktop.portal.GlobalShortcuts`
   is not exported at all, so shortcuts must come from a desktop binding that invokes the CLI (see
   `docs/linux-integration.md` §4). Any design that assumes the portal is the default path was optimistic.
2. **The Screenshot portal is version 2 here.** No `target`/`AvailableTargets`, i.e. no area target: a portal
   capture would be full-screen plus our own crop. Relevant to milestone M3 only (capture is deferred).
3. **`RegisterEvent` is not uniformly `(s, a{sv})`.** at-spi2-core 2.44 exports the one-argument form; a
   QtDBus client must probe (or introspect) rather than assume. The probe now tries both and prints which
   form the bus accepted.
4. **The probe had a real bug: XFixes events are `(first_event + subtype)`.** The code compared
   `response_type` with `XCB_XFIXES_SELECTION_NOTIFY`, which is `0`, so *no* selection event could ever
   match — the 5/5 self-test failure and the 0-event watch were caused by this, not by the desktop.
   Fixed: the subscription records `first_event` and matching uses `base + subtype`, with a counter for
   unexpected event types so a future mismatch is visible instead of silent.
5. **Runtime provenance, not desktop capability, blocked the libatspi mode.** In the Qt build the process
   loaded `libdbus-1` and `libglib-2.0` from an activated conda environment while `libatspi.so.0` came from
   the system; `atspi_init()` then failed even though GDBus/QtDBus reached the same accessibility bus.
   A measurement binary must not mix runtimes: the AT-SPI probe is now a separate **Qt-free** target built
   with the system compiler and no RPATH (`build.sh` fails if that precondition is violated), and it prints
   the loaded library paths whenever `atspi_init()` fails.
6. **Calling `atspi_init()` against an unreachable bus aborts the process** inside at-spi2-core's `dbind`.
   Both binaries now pre-check `org.a11y.Bus.GetAddress` (GDBus in the Qt-free probe, QtDBus elsewhere) and
   report instead of aborting.

### Status after Run 1

No feasibility conclusion is possible yet: the X11 mechanism check was invalidated by bug (4), and the
libatspi path was blocked by (5). Both are fixed; Run 2 is required.

---

## Run 2 — GNOME Wayland, second attempt (2026-11-16)

Build split worked: Qt probe with the conda toolchain, AT-SPI probe with `/usr/bin/c++`, provenance gate
passed (`no RPATH, all runtime libraries resolve from the system`).

| Field | Value |
| --- | --- |
| `caps` | unchanged from Run 1: `GlobalShortcuts` not exported, `ScreenCast` 4, `Screenshot` 2, `Secret` 1 |
| `x11 selftest --count 5` | XFixes event **base 86**; **1 pass / 4 fail**; the pass took **1501.4 ms**; failures all `SelectionNotify timeout (owner did not answer)` |
| `diag`: GDBus pre-flight | **ok** |
| `diag`: loaded libraries | all `/usr` (`libdbus-1.so.3.19.13`, `libglib-2.0.so.0.7200.4`, `libatspi.so.0.0.1`) |
| `diag`: `atspi_init()` | **FAILED** |
| `list` / `watch` | `atspi_init failed`, exit `2` |

### What this run established

1. **The XFixes fix worked**: events are now received at all (previously zero). The remaining 4/5 failures had
   a different cause, and the single "pass" after exactly the 1500 ms deadline was the tell: the response was
   consumed late rather than never.
2. **Root cause of the X11 failures: two threads shared one xcb connection.** `publish()` ran on the main
   thread and used the owner's connection while the owner thread was reading events from it. XCB connections
   are single-threaded; interleaving drops or delays replies. The owner now publishes from its own thread
   (request counter + mutex), and failure output additionally reports whether PRIMARY is still owned, which
   separates "ownership not taken" from "conversion broken".
3. **The runtime-mixing theory is falsified.** With a clean all-system runtime, `atspi_init()` still fails,
   while GDBus reaches the same accessibility bus. So the remaining causes are inside libatspi/libdbus or the
   bus configuration — not our build. `diag` now records: libdbus session connect, libdbus connect to the
   accessibility bus address, and libatspi's own GLib log messages.

## Run 3 — GNOME Wayland, third attempt (2026-11-16)

| Field | Value |
| --- | --- |
| `x11 selftest --count 5` | **0 pass / 5 fail**, every failure `SelectionNotify timeout`, `primary still owned=yes` |
| `diag`: GDBus pre-flight | ok |
| `diag`: libdbus session bus | **ok** (`:1.259`) |
| `diag`: libdbus a11y bus | **ok** (registered) |
| `diag`: `atspi_init()` | reported FAILED — **but this was a probe bug** |
| GLib messages from libatspi | none |

### Two root causes found

1. **`atspi_init()` return convention was read backwards.** The declaration is `int atspi_init(void)`
   returning **0 on success** and non-zero on failure; the probe tested it as a boolean, so a working AT-SPI
   stack was reported as "unavailable" in Runs 1–3. That also explains the absence of GLib error messages: it
   had succeeded. Fixed (`== 0` means success), and `diag` now prints the raw return value plus
   `atspi_is_initialized()`. Lesson recorded in `docs/design/04-selection.md` §12: verify a library's return
   convention before turning it into an `ok/failed` verdict.
2. **The X11 self-test may be measuring the wrong thing on a Wayland session.** Ownership is confirmed
   (`primary still owned=yes`) yet no conversion is answered. `GetSelectionOwner` may be returning a window
   that is *not* ours — XWayland/mutter mirrors the compositor clipboard and can take PRIMARY over. The
   self-test now prints the actual owner window id and whether it belongs to this process
   (`owned by us=no — mediated`), counts X protocol errors per run (a refused conversion is an error event,
   previously indistinguishable from a timeout) and retries once with `CurrentTime`, which some owners and
   clipboard bridges require.

### Status

- AT-SPI: blocked only by the inverted check — next run tells us whether the stack actually works here.
- X11: mechanism status unknown until the owner identity is printed; on a Wayland session the read path that
  matters is `x11 watch` with a real application owning PRIMARY, not our own self-owned selection.

## Run 4 — GNOME Wayland, fourth attempt (2026-11-16)

`atspi_init()` now reports `return 0 (success)`, `atspi_is_initialized() = yes`. It works here.

### Coverage (`list --depth 2`, 30 applications, 426 ms)

| Application | text objects / scanned |
| --- | --- |
| `Google Chrome` | 1 / 1 |
| `code` (VS Code) | 2 / 2 |
| `Codex` | 3 / 3 |
| `qq` | 4 / 4 |
| `org.gnome.Nautilus` | 0 / 10 |
| `gnome-terminal-server` | 0 / 8 |
| `gnome-shell`, `gnome-system-monitor`, portals, daemons | 0 |

Note the terminal shows **no** text object at depth 2 yet it is the only application that emitted events —
"text objects found" is a poor predictor in both directions, which is why the matrix needs behaviour, not
scan results.

### Events (`watch`, `object:text-selection-changed`, 90 s, selections in browser / editor / terminal / GTK / Qt)

| Observation | Detail |
| --- | --- |
| Applications that emitted events | **only `gnome-terminal-server`** (role `terminal`) |
| Applications that emitted nothing | Chrome, VS Code, Codex, QQ, Nautilus — despite exposing text objects |
| Event pattern per drag | **two events**: one `ranges=1 chars=0`, then one `no selection ranges` |
| Text resolved | **always empty** (`chars=0`, identical digest) |
| `app_ms` / `text_ms` | 0.0–0.2 / 0.3–1.7 ms — resolution is fast when it returns something |
| `anchor` from `GetRangeExtents` | `(0,0)` — unusable |
| Deadline | listener outlived `--duration` by 100+ s; `atspi_event_quit()` did not stop the loop |

### Defect found by this run

`atspi_event_quit()` did not terminate `atspi_event_main()` in at-spi2-core 2.44, so the probe had to be
killed manually. The listener now runs on a detached worker, the deadline prints the summary and then exits
unconditionally (`_Exit`), and `selftest-exit` proves the deadline path terminates even when the loop never
returns (verified: 400 ms request → 408 ms wall clock).

### Interpretation (needs one confirmation run before it becomes a conclusion)

- **Terminal events carry no text.** Either the reported range is zero-length (`[start, end)` with
  `start == end`) or VTE returns an empty string for it. The probe now prints
  `range[i] = [start, end) len=… of characterCount=…`, which distinguishes the two in a single run.
- **Chromium-family and Qt applications emitted no selection events at all.** Absence in one session is
  evidence, not proof: it may mean the app emits a different event type. `--events object` (all object
  events) answers it directly.

## Run 5 — GNOME Wayland, fifth attempt (2026-11-16)

Two answers, both decisive.

| Question | Result |
| --- | --- |
| Does a terminal selection carry text? | **Yes** — with `--show-text` the resolved text matched the selection. The earlier `chars=0` events were intermediate drag events with a zero-length range, not a broken backend. |
| Do browser/editor apps emit *any* object event? | `--events object` **floods immediately with no user action**: the desktop shell and daemons emit object events continuously. The diagnostic was unusable as printed. |

Consequences recorded in the design:

1. **The terminal path works**, so a settle delay after the event plus ignoring zero-length ranges is the
   product fix — not a different backend. The coordinator's stability delay (default 350 ms, to be measured)
   covers the intermediate events.
2. **A wildcard event subscription is not viable.** Registering `object` produced continuous traffic with
   nobody touching the keyboard. A product must subscribe to exactly one event type
   (`object:text-selection-changed`) and may additionally filter by source application. This is now evidence,
   not a precaution.
3. The probe aggregates and filters: `--group` prints an `app | event type | count` table instead of a flood,
   `--app` restricts by application, `--max-print` caps per-event output (default 200).

## Run 6 — GNOME Wayland, sixth attempt (2026-11-16)

### A. Browser / editor / Qt coverage with aggregation

`watch --events object --group --app "chrome,code,codex,qq"`, 45 s, selecting in Chrome page text, VS Code
and QQ:

| | |
| --- | --- |
| Events total / ignored by the app filter | **7388 / 7380** — the shell and daemons alone produce thousands of object events |
| Events from the four filtered applications | `object:state-changed:active` × 2 each (window focus), `code` also `object:property-change:accessible-name` × 1 and `object:state-changed:iconified` × 1 |
| Text-related events from those applications | **none** — no `object:text-selection-changed`, no `object:text-changed` |

So in the tested configuration those applications report focus changes only. Two explanations remain and are
distinguishable: (a) their content accessibility tree is not enabled, or (b) they never emit selection events
for page/editor text. Chromium and Electron gate their accessibility tree on the desktop-wide accessibility
status, so `diag` now prints `org.a11y.Status.IsEnabled`, `org.a11y.Status.ScreenReaderEnabled` and the GTK
`toolkit-accessibility` setting. If those are `false`, (a) is the likely answer and the desktop switch is the
variable to change — with the privacy consequence recorded in the design.

### B. Terminal again reports zero-length ranges

```
range[0] = [3049, 3049) len=0 of characterCount=3230
range[0] = [2990, 2990) len=0 of characterCount=3171
range[0] = [2915, 2915) len=0 of characterCount=3096
```

`characterCount > 0` proves the text object is healthy, and the offsets move *backwards* while dragging, i.e.
they track a caret rather than a selection. Run 5 had produced correct text from the same terminal, so the
difference is timing, not capability: either VTE publishes the range after the event, or the drag never
committed. `--retry-ms N` now re-reads an empty/zero-length selection after N milliseconds and prints both
attempts, which settles it in one run.

## Run 7 — GNOME Wayland, seventh attempt (2026-11-16)

| Field | Value |
| --- | --- |
| `org.a11y.Status.IsEnabled` / `ScreenReaderEnabled` | **not exposed** on this GNOME (property call errors) |
| `toolkit-accessibility` (gsettings) | `false` before the test |
| Terminal with `--retry-ms 200` | still zero-length ranges for slow drag **and** for Shift+Left; `text_ms` ≈ 208 ms (retry sleep included) |
| Chrome with `toolkit-accessibility=true` | still only `object:state-changed:active` × 2 — the switch alone did not make page text appear |

Two further facts established while investigating:

1. **Methodology defect**: a watcher running inside the terminal being measured captures the tty echo of
   the tester's keys (`^[[1;2D` for Shift+Left) in its own output. Measurement runs must happen in a
   different terminal or window than the source application.
2. **The Qt build available here has no AT-SPI bridge** (`strings libQt6Gui.so.6 | grep -c org.a11y` → 0),
   so a Qt-based test app cannot serve as an accessibility provider, and a Qt application built this way
   also cannot expose its own UI to assistive technology.

Because manual selection rounds were producing inconsistent results (Run 5 read terminal text correctly,
Runs 6–7 did not), measurement no longer depends on hand selection: see below.

## Run 8 — automatic harness (replaces manual rounds)

```bash
A=build/m0-atspi/snip-atspi-probe
$A diag                                   # NEW: accessibility status of the desktop
$A watch --duration 25000 --show-text --retry-ms 200
#   terminal: drag-select a word slowly and pause before releasing; then double-click a word;
#             then Shift+Left a few times. One method per selection, 2 s apart.
$A watch --duration 40000 --events object --group --app "chrome,code,qq"
#   with the accessibility switch in the state reported by diag
```

If `diag` reports `IsEnabled`/`ScreenReaderEnabled`/`toolkit-accessibility` as `false`, repeat the Chrome run
once with accessibility enabled, then revert it:

```bash
gsettings get org.gnome.desktop.interface toolkit-accessibility
gsettings set org.gnome.desktop.interface toolkit-accessibility true   # diagnostic only
#   ... rerun the Chrome grouping run ...
gsettings set org.gnome.desktop.interface toolkit-accessibility false
```

| Observation | Consequence |
| --- | --- |
| `--retry-ms 200` turns `chars=0` into real text | VTE publishes the range after the event: the product needs a re-read after the settle delay, not a new backend |
| Retry stays empty for every selection method | VTE only exposes some selection modes here; record which ones in the matrix |
| Chrome emits text events once accessibility is enabled | The product needs an explicit, disclosed step to enable desktop accessibility; never do it silently |
| Chrome still emits no text events | Browser page text is not obtainable through AT-SPI here; browsers rely on the paste path |

`run-m0.sh` runs everything unattended in ~20 s and writes `RESULTS-auto.md` with a computed verdict:

1. build (both binaries, provenance gate),
2. capability report and AT-SPI prerequisites,
3. X11 mechanism self-test (no interaction),
4. **AT-SPI closed loop against a GTK publisher it starts itself** — a text view that selects a different
   word every 900 ms and prints what it selected, so every event has a known expected range and no human
   selection or private content is involved,
5. if the product event type yields nothing, an automatic **event-type probe** (`--events object --group`)
   against the same publisher, so the report always says whether the provider is silent or the
   subscription is wrong.

The verdict distinguishes three outcomes: read path works / events but no text (our read logic) / no events
at all (provider bridge or subscription).

## Run 8 — automatic harness (2026-11-16) — decisive

`run-m0.sh`, one command, no interaction. `RESULTS-auto.md` holds the full transcript.

| Measure | Result |
| --- | --- |
| Build + provenance gate | ok, no RPATH, all runtime libraries from the system |
| X11 self-test | **5 / 5 PASS**, 28 chars each, **0 X errors** — but every iteration took **~1501 ms** |
| AT-SPI events from the controlled GTK provider | **11** |
| Events carrying text | **11 / 11**, text matched the publisher log word for word |
| Zero-length ranges | 0 |
| `GetRangeText` resolve time | p50 **1.3 ms**, p95 2.3 ms |
| `GetRangeExtents` (anchor) | present and plausible (`(153,60)`, `(191,60)`, …) |
| Subscription/registration | `RegisterEvent ok`, one event type |

### What this settles

1. **The AT-SPI read path is correct.** A known-good GTK provider delivered every event with exact text and
   usable coordinates in ~1.3 ms. Therefore the terminal's zero-length ranges and the Chromium-family silence
   are *application* properties, not defects in our code. Recorded in
   [ADR 0006](../../docs/adr/0006-m0-backend-outcome.md).
2. **The X11 conversion has a 1.5 s stall with the event timestamp.** All five iterations passed only via the
   `CurrentTime` retry, which means the first attempt never answered within the deadline. The probe now uses
   a short first deadline (250 ms) and reports which attempt won
   (`[event-time/250ms answered in 12.3 ms]` vs `[CurrentTime/1500ms answered in 1.1 ms]`), so the next run
   shows whether the stall disappears and what the real profile is.
3. **No further manual rounds are needed for the architecture question.** Coverage questions (which
   applications behave) belong to the M1 verification matrix and can be recorded from real usage.

### Verdict text

```
AT-SPI read path: WORKS.
X11 mechanism: WORKS (5/5 conversions answered).
```

## Run 9 — KDE Wayland (needed for the matrix)

| Field | Value |
| --- | --- |
| Plasma + KWin version | |
| `caps` portal versions | |
| `diag` / `list` / `watch` results | |
| `x11 watch` results | |

## Run 10 — packaged (Flatpak) build, once it exists

| Field | Value |
| --- | --- |
| Channel and version | |
| `caps` differences vs native | |
| Event counts vs native | |

---

## Conclusions (settled by Run 8)

1. **M1 backend (Wayland):** AT-SPI `object:text-selection-changed` via libatspi — read path proven.
2. **M1 backend (X11):** XFixes `PRIMARY` with a two-attempt conversion (short event-time deadline, then
   `CurrentTime`).
3. **Zero-length ranges:** ignore them; they are caret events, not selections.
4. **One event type, one backend at a time:** a wildcard subscription measured thousands of events per
   minute with no user action.
5. **Coverage, not architecture, is the remaining unknown:** VTE terminals expose zero-length ranges
   (3 of 4 attempts), Chromium-family/Electron apps emit no text events with `toolkit-accessibility` either
   off or on. Both are recorded as limitations with the paste flow as the documented alternative.
6. **Portals on this desktop:** `GlobalShortcuts` absent, `Screenshot` v2 (no area target), `ScreenCast` 4,
   `Secret` 1. Shortcuts must come from a desktop binding that runs the CLI.
7. **Product requirements derived from measurement:** ≤250 ms first conversion deadline; settle delay after
   the event; re-read once for empty ranges; never 1.5 s blocking; never a broad event subscription.

1. **Chosen M1 backend (Wayland):** _AT-SPI / data-control / none_
2. **Chosen M1 backend (X11):** _XFixes PRIMARY / AT-SPI / none_
3. **Stability delay:** measured drag behaviour suggests __ ms (default hypothesis 350 ms).
4. **Character limit:** observed realistic selections suggest __ (default 4000).
5. **Open questions closed by data:** list them here, and name the ones that stay open with a reason.
