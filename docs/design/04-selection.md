# 04 — Selection Observation and Text Resolution (`sniplyngo_selection`)

| | |
| --- | --- |
| Milestone | M0 (prototype), M1 (one backend) |
| Status | AT-SPI and X11 backends implemented; coverage varies by desktop and application |
| Depends on | `01 core`, Qt Core; backends add QtDBus/xcb/wayland-client |
| Used by | 05 (event stream + text resolution) |
| Document owner | maintainer |
| Implementation | `src/selection/`, `src/platform/atspi/` and `src/platform/x11/`; data-control remains unimplemented |

## 1. Scope

Turn "the user selected something in another application" into text, without a shortcut, without reading the
ordinary clipboard and without injecting input. This module owns *acquisition only*: it reports events, it
resolves text on request, and it reports honestly when it cannot.

## 2. Responsibilities

- Provide one backend per session, selected by a documented policy, and report which one and why.
- Emit a `SelectionEvent` for each *new* selection in an observable source application.
- Resolve the text belonging to a specific event, asynchronously, under a size limit and a deadline.
- Suppress its own events, protected/password text, and any resolution that has been superseded.
- Report three separate things: interface presence, access result, and observed behaviour.

## 3. Non-responsibilities

- No debounce/coalescing and no translation trigger (module 05 owns timing and policy).
- No consent, no provider, no window, no OCR.
- No clipboard reads, no synthetic key events, no pointer/keyboard monitoring, no screenshots.
- No text normalisation beyond trimming surrounding whitespace (see §6.4).

## 4. Public interface

```cpp
// src/selection/types.h
namespace selection {

struct SelectionSource {
    QString appName;        // accessible app name or X11 WM_CLASS instance; may be empty
    QString backendId;      // "atspi" | "x11-primary" | "wlr-data-control"
    // Deliberately absent: window title, process path, pid of the source app.
    // Titles are user data; pids invite cross-referencing. Diagnostics use appName only.
};

struct SelectionEvent {
    quint64 seq = 0;                    // per-backend monotonic; 0 is invalid
    SelectionSource source;
    QDateTime observedAt;               // monotonic-ish timestamp of observation
    std::optional<QRect> anchorLogical; // screen coordinates in Qt logical pixels, if the backend knows them
};

struct ResolveLimits {
    int maxChars = 4000;
    int timeoutMs = 1500;
};

struct SelectionText {
    QString text;                       // already trimmed; internal whitespace untouched
    bool truncatedBySource = false;     // backend had to drop content (e.g. INCR over limit)
    std::optional<QRect> anchorLogical;
};

struct BackendProbe {
    bool interfacePresent = false;      // protocol/interface exists in this session
    bool accessGranted = false;         // this process may use it
    QString identifier;                 // "atspi", "x11-primary", "wlr-data-control"
    QString humanName;                  // "AT-SPI accessibility events"
    QString reason;                     // non-sensitive explanation of the current state
};

enum class BackendState {
    Uninitialised, Subscribing, Running, Stopped, Denied, Failed
};

} // namespace selection
```

```cpp
// src/selection/selection_backend.h
namespace selection {

using ResolveCallback = std::function<void(quint64 seq, core::Result<SelectionText>)>;

// One instance per process, living (and running its I/O) on its own worker thread.
// Signals delivered to receivers in the UI thread through queued connections.
class SelectionBackend : public QObject {
    Q_OBJECT
public:
    ~SelectionBackend() override = default;

    virtual BackendProbe probe() = 0;                 // no user-visible side effects, no text access
    virtual core::Result<void> subscribe() = 0;             // start observing; MUST NOT emit for a pre-existing selection
    virtual void unsubscribe() = 0;                   // stop observing and release the subscription
    virtual void resolveLatest(quint64 seq, ResolveLimits, ResolveCallback) = 0;
    virtual void cancelPending() = 0;                 // drop in-flight resolution; do not emit for it

    BackendState state() const;
    QString backendId() const;

signals:
    void eventObserved(const selection::SelectionEvent &event);
    void stateChanged(selection::BackendState state, const QString &reason);
    void probeChanged(const selection::BackendProbe &probe);
};

} // namespace selection
```

```cpp
// src/selection/backend_selector.h
struct SelectionSession {
    bool waylandSession = false;
    bool x11Session = false;
    bool accessibilityBusReachable = false;
    QStringList waylandDataControlGlobals;   // empty when no Wayland connection
};

class BackendSelector {
public:
    // "auto" or an explicit id from settings. Never starts two backends.
    static core::Result<QString> choose(const SelectionSession &, const QString &configured);
    static QString explain(const SelectionSession &, const QString &chosen);  // diagnostics text, no user data
};
```

Ownership: the application creates exactly one backend via a factory, moves it to its worker thread, connects
`eventObserved` to the coordinator, and destroys it on shutdown (`unsubscribe()` → thread quit → `wait()`).

## 5. Event model

1. **No initial event.** `subscribe()` never emits for the selection that already exists (AT-SPI: no initial
   enumeration, XFixes/data-control: swallow the first notification/synthetic offer produced by subscribing).
   This is what makes "enable" and "resume" safe without extra policy.
2. **One backend only.** The selector never runs two observers, so events never need cross-source correlation.
3. **`seq`** increments per emitted event on the backend's thread. It exists to correlate a resolve request
   with an event, not to order events across backends.
4. **Self-suppression.** Events whose source application is this process are dropped before emission. Detection
   is backend-specific (§6) and must be verified; if it fails, the coordinator still cannot loop, because the
   result window is not an accessible text selection source, but it is still asserted in tests.
5. **No text in the event.** Events are cheap and carry no content; text is only materialised by an explicit
   `resolveLatest()` call from the coordinator, after the stability delay. This keeps memory and privacy
   bounded when the user drags across a page.
6. **Protected and unobservable content emits nothing.** A password text object is not an event; it is an
   observation refusal. The coordinator therefore cannot leak it by accident.

## 6. Resolution semantics

### 6.1 Requesting

`resolveLatest(seq, limits, cb)` is called at most once per stability window. If a resolution for an older
`seq` is still running, the backend records the newest `seq` as *pending* (single slot) and starts it when the
running call returns. There is never a queue of selections, and no memory of selections whose
`seq` was superseded.

### 6.2 Deadline

Each backend enforces `limits.timeoutMs` on its own thread and reports `SelectionTimeout` if the source has
not delivered text. Where the underlying API cannot be interrupted (a blocking D-Bus call to a hung
application), the call is abandoned logically: the result is discarded, the pending slot is dropped, and the
blocked thread recovers when the library returns. This limitation is documented per backend and measured in
M0; it must not block the UI.

### 6.3 Size limit

`maxChars` applies to the *selected text*, before trimming. Over the limit ⇒ `SelectionTooLarge`. The module
never truncates silently; the coordinator decides whether to offer an explicit "translate shortened text"
action (05 §7).

### 6.4 Text shaping rules

- Trim leading/trailing whitespace and NUL characters.
- Do **not** join lines, remove hyphenation, collapse internal newlines or normalise Unicode: the user's
  selection is the user's text (`../DESIGN.md` §4.2.5). Translation prompt design handles layout, not this
  module.
- Do not classify script, do not skip translation, do not de-duplicate repeated selections.

### 6.5 Multiple ranges

Some toolkits expose a selection as several ranges (browser multi-element selection). Rule: read the ranges in
ascending order, concatenate with a single `\n` between non-adjacent ranges, count the result against
`maxChars`. If any range is unreadable, fail with `SelectionNotText`/`SelectionTimeout` rather than returning a
partial selection.

## 7. Backends

### 7.1 `x11-primary` — XFixes `PRIMARY` (M0 candidate)

Threading: own `xcb_connection_t` on a dedicated thread (never Qt's connection, to avoid interference).

Subscribe:

1. `xcb_connect()`; fail ⇒ `BackendFailed` with the display name.
2. `xcb_xfixes_query_version(5, 0)`; fail ⇒ `interfacePresent=false`.
3. `xcb_xfixes_select_selection_input(root, atoms.primary, XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER
   | ..._SELECTION_WINDOW_DESTROY | ..._SELECTION_CLIENT_CLOSE)`.
4. Create a private, unmapped requestor window; swallow the first notification that follows subscription.

Observe: event loop on `xcb_wait_for_event`, woken for commands through a self-pipe/`eventfd` polled together
with `xcb_get_file_descriptor`. `XFixesSelectionNotify` with `owner != 0` and `owner != previousOwner` ⇒ emit a
`SelectionEvent` with `anchorLogical` from `xcb_query_pointer` (X11 gives a true pointer position; converted
with `QGuiApplication::screenAt()` and the screen's device pixel ratio for logical coordinates).

Resolve:

1. `xcb_convert_selection(requestor, primary, atoms.utf8String, atoms.sniplyngoSelection, event.timestamp)`.
   If the owner rejects with a timestamp error, retry once with `XCB_CURRENT_TIME`, then fail.
2. Wait for `SelectionNotify` (`property != None`) with the deadline; `None` ⇒ `SelectionNotText` (owner refuses
   the target).
3. `xcb_get_property`; `type == atoms.incr` ⇒ INCR transfer: accumulate `PropertyNotify` chunks until the final
   zero-length chunk, aborting with `SelectionTooLarge` when the cap is exceeded. Any other type: validate that
   the returned type is a text type, then read as UTF-8 (fall back to `STRING`/Latin-1 only if the target was
   converted to it, and mark the text as best-effort).
4. Ask `TARGETS` first only when the initial `UTF8_STRING` conversion fails, to distinguish "no text" from
   "no UTF-8".

Event matching: X11 delivers extension events as `first_event + subtype`, where `first_event` comes from
`xcb_get_extension_data(..., &xcb_xfixes_id)->first_event`. Comparing `response_type` against the bare
`XCB_XFIXES_SELECTION_NOTIFY` constant (which is `0`) matches nothing and fails silently — that mistake
produced zero events in the probe's first real run. The implementation must record the base at subscribe time
and count unexpected event types so a future mismatch is visible.

The X11 backend's protocol decisions are unit-tested (`tests/x11_protocol_test.cpp`). Its subscription,
conversion and repeated start/stop were also tested against a nested Xephyr X server with an `xclip` owner;
`tests/x11_backend_live_test.cpp` can repeat this when `SNIPLYNGO_X11_TEST_DISPLAY` names a test display.

Conversion strategy (measured): on the tested Wayland session every conversion that used the XFixes event
timestamp stalled for the full deadline, while the same request with `CurrentTime` answered immediately. The
implementation therefore attempts the event timestamp with a **short** deadline (~250 ms) and then retries
with `CurrentTime`, reporting which attempt won. A single 1.5 s blocking attempt is not acceptable for an
interactive tool, and the worst case must stay in the hundreds of milliseconds.

Known limits: XFixes reports a change of selection ownership; per ICCCM an application need not re-acquire
ownership for a modification it regards as the same selection, so repeated drags in one application can be
invisible. Non-`UTF8_STRING` owners and `INCR`-averse owners are supported best-effort. XWayland bridging
depends on the compositor and source application; successful events from one GTK application on GNOME
Wayland do not establish general coverage (`../linux-integration.md` §9).

### 7.2 `atspi` — AT-SPI `object:text-selection-changed` (M0 candidate)

Threading and transport — **decision required in M0**, both paths specified:

| Option | Shape | Trade-off |
| --- | --- | --- |
| **A. QtDBus client** (preferred) | Connect to the accessibility bus (`org.a11y.Bus.GetAddress` on the session bus), `org.a11y.atspi.Event.Listener.RegisterEvent("object:text-selection-changed", {})` on its own `QDBusConnection`, receive `TextSelectionChanged` signals, read text through `org.a11y.atspi.Text` | per-call timeouts are ours; no GLib main loop; must hand-parse AT-SPI reference types (`(so)` pairs) and variants |
| **B. libatspi** | `atspi_init()` + `atspi_event_listener_register` + `atspi_event_main_thread()` on a dedicated thread | mature, handles registry details; its event loop is blocking and D-Bus call timeouts are the library's default (too long), so deadline handling is weaker |

Version variance in the wire protocol (observed): at-spi2-core 2.44 exports `RegisterEvent(s)`, not the
`(s, a{sv})` form some documentation implies. A raw client must introspect the registry or try both forms;
libatspi hides the difference, which is a further argument for option B.

Both live behind the same `SelectionBackend`; the choice is an implementation detail of `src/platform/atspi/`.
M0 measures: event delivery, resolution latency, deadline behaviour against a hung app, and behaviour with
accessibility disabled.

Facts established while building the M0 probe (against at-spi2-core 2.44 headers, see
`prototypes/m0-selection-probe/RESULTS.md`):

- Option B's API shape is `AtspiText *atspi_accessible_get_text_iface(obj)`; the `atspi_text_*` calls take
  `AtspiText*`. `GetSelection` returns `AtspiRange*` (`start_offset`, `end_offset`), `GetRangeExtents`
  returns `AtspiRect*`, and the loop is `atspi_event_main()`/`atspi_event_quit()`. `AtspiEvent` carries
  `source`, so application attribution is available without extra work.
- Option A has a structural gap: a QtDBus signal slot receives only the signal arguments, **not** the
  message sender. Attribute-free event counting is possible; resolving the source application or the text
  object is not, without a second mechanism. This tilts the decision toward option B unless further work is
  justified.
- Calling `atspi_init()` when the accessibility bus is unreachable aborts the process inside at-spi2-core's
  `dbind`. Both the probe and the product must pre-check `org.a11y.Bus.GetAddress` before initialising.

Observe:

1. Register only `object:text-selection-changed`. Do not subscribe to text-change, keystroke, pointer or
   property events.
2. From the signal's `source` object (`bus name` + `object path`), read the accessible role. Role
   `password text` (and any toolkit "protected" state that can be verified) ⇒ drop the event.
3. Determine the owning application name for `SelectionSource::appName`; drop the event when it is this
   application.
4. Optionally read `GetRangeExtents(start, end, SCREEN)` for the selected range to fill `anchorLogical`. Treat
   it as a hint: coordinates are toolkit-reported and unverified under Wayland (`../linux-integration.md` §6).

Resolve:

1. `Text.GetNSelections()`; `0` ⇒ `SelectionEmpty` (a selection-changed event without text is common).
2. For each selection index: `Text.GetSelection(i)` → `(start, end)`; validate `0 ≤ start < end ≤
   CharacterCount`, `end-start` against `maxChars`.
3. `Text.GetText(start, end)`; concatenate per §6.5.
4. All calls use our own deadline; on timeout return `SelectionTimeout` and drop any late value.

Observed on one GNOME Wayland session (probe Runs 4–5): `atspi_init()` works.
`object:text-selection-changed` arrived **only** from the VTE terminal; its text resolved correctly once the
selection had settled, while dragging produced intermediate events with zero-length ranges. Chrome, VS Code,
Codex and QQ exposed text objects but emitted no selection event in the tested configuration, and
`GetRangeExtents` returned `(0,0)` for the terminal (anchor unusable).

Two design consequences already taken from that:

- Ignore zero-length ranges and keep the stability delay: intermediate drag events are normal, not errors, and
  they must never produce a request.
- Subscribe to exactly one event type. A wildcard subscription (`object`) flooded continuously with no user
  action from shell and daemon processes, so broad registration is both a privacy and a CPU hazard.

On a later GNOME Wayland test, a controlled GTK publisher resolved five successive selections. An isolated
Chrome instance did not emit selection events for synthetic drags under its default accessibility settings;
the same test emitted events with `--force-renderer-accessibility`, and three selections reached a local test
provider. Enabling accessibility exposes application content to accessibility clients. SnipLingo does not
change the user's desktop or browser accessibility settings. These tests do not establish coverage for other
applications, browsers or desktops.

Known limits: requires an accessible application (`AT-SPI` exposure, which some toolkits, terminals, canvas
content and sandboxed apps lack; browsers may need accessibility enabled). Coverage per application is exactly
what the verification matrix exists for. Multi-element browser selections and iframes need separate
verification.

### 7.3 `wlr-data-control` / `ext-data-control-v1` — privileged primary-selection events (M2 candidate)

Only when the compositor advertises the global and grants it to this client; GNOME is not expected to.
Requires a Wayland protocol XML in-tree and build-time code generation (`wayland-scanner`).

Subscribe: own `wl_display` connection on a dedicated thread; `wl_registry` roundtrip; require
`zwlr_data_control_manager_v1` version ≥ 2 (primary-selection event) or `ext_data_control_manager_v1`;
bind a `wl_seat`, create the data-control device, and **discard** the first `primary_selection` event that
arrives from device setup (it may describe a stale selection).

Observe: each subsequent `primary_selection` (or `selection` cleared) produces an event; keep the offer alive
only until resolution (offers are per-event objects).

Resolve: choose the first supported MIME type in order
`text/plain;charset=utf-8`, `text/plain`, `UTF8_STRING`; create a pipe, `receive(mime, write_fd)`, close the
write end locally, read the read end with `poll()` under the deadline, cap at `maxChars * 4` bytes plus one to
detect the over-limit case, decode as UTF-8. Missing MIME ⇒ `SelectionNotText`.

Stop: `wl_display_prepare_read`/`wl_display_cancel_read` discipline plus an `eventfd` waker so `unsubscribe()`
and shutdown do not hang the thread.

### 7.4 Backend selection policy

| Session | Configured `auto` order | Rationale |
| --- | --- | --- |
| X11 | `x11-primary`, then `atspi` | XFixes PRIMARY is simpler and broadly available on X11; AT-SPI adds coverage only if M0 shows a gap |
| Wayland | available `x11-primary`, then `atspi`, then `wlr-data-control` | A usable XWayland bridge covered real selections that emitted no AT-SPI events on the tested GNOME desktop. Coverage still depends on the compositor and source application. Data-control is not implemented. |

Rules: an explicit setting wins; if the configured backend is unavailable, report `SelectionBackendAbsent` or
`SelectionBackendDenied` and stay unsubscribed — **do not silently switch**. With `auto`, the selector reports
the chosen backend and the reason, and the UI shows it.
The XFixes connection and version are probed without reading selection content. This establishes interface
availability, not whether a given Wayland application shares selections through XWayland. The user can
explicitly choose AT-SPI when that offers better coverage.

## 8. Capability reporting

`BackendProbe` keeps three levels apart, and the UI must show the distinction
(`../linux-integration.md` §1, `../ui-ux-spec.md` §1.1):

| Level | Example | Reported as |
| --- | --- | --- |
| Interface present | `zwlr_data_control_manager_v1` in the registry; accessibility bus reachable; XFixes extension | `interfacePresent` |
| Access granted | data-control global bound; `RegisterEvent` succeeded; selection input selectable | `accessGranted` |
| Behaviour with a source app | "Chrome on GNOME Wayland, native build: emitted events" | matrix row, never inferred |

`probe()` performs no text reads and no user-visible actions. A real selection test is a separate, explicitly
user-initiated action owned by the UI (07), which calls a diagnostic entry point that reports *whether an event
arrived*, not *what it contained*.

## 9. Errors

| Condition | Code |
| --- | --- |
| No candidate backend for this session | `SelectionBackendAbsent` |
| Global present but not grantable / accessibility bus denied | `SelectionBackendDenied` |
| Connect, bind, register or event-loop setup failed | `SelectionBackendFailed` |
| Owner refused conversion / no text MIME | `SelectionNotText` |
| Zero ranges or empty text | `SelectionEmpty` |
| Password role / protected object | `SelectionProtected` |
| Over `maxChars` | `SelectionTooLarge` |
| Deadline exceeded | `SelectionTimeout` |
| Superseded by a newer event or unsubscribed | `SelectionCancelled` |

None of these carry text in `detail`.

## 10. Configuration consumed

`selection.backend`, `selection.maxSelectionChars`, `selection.resolveTimeoutMs` (module 02). Stability delay
is **not** here: it is the coordinator's policy.

## 11. Dependencies and boundaries

Allowed: `01 core`, Qt Core; per backend `QtDBus`, `xcb`/`xcb-xfixes`, `wayland-client` + generated protocol
code, optional `libatspi`.
Forbidden: Qt Quick/Widgets, settings file access, provider or coordinator headers (only `01` types).

### Implementation note: delayed read

The event callback retains the most recent accessible object and its sequence, without reading selection
text. After the coordinator's stability delay, `resolveLatest()` schedules the range and text reads on the
libatspi event loop and returns the result on the Qt thread. This allows applications to publish an event
before their final selection range is ready. A newer event supersedes the retained source. AT-SPI-enabled
builds set `QT_NO_GLIB` before creating the Qt application, so Qt's GLib dispatcher does not compete with the
libatspi worker for the default GLib context.

### Measured libatspi behaviour (live harness)

The backend is now exercised by `tests/atspi_backend_live_test.cpp` against a fake accessibility service over
a private D-Bus daemon on TCP — real `libatspi` wire protocol, no desktop. What that established:

- Signal signature is `siiva{sv}`; emitting one `i` short makes dbind reject the event silently.
- `Text.GetSelection` replies with **two out arguments** (`ii`), not a struct `(ii)`; `GetRangeExtents`
  likewise. `GetInterfaces` must return the **fully qualified** interface names
  (`org.a11y.atspi.Accessible`, `org.a11y.atspi.Text`).
- `atspi_init()` returns non-zero when the library is *already* initialised, so it may only be called once
  per process; `atspi_exit()` breaks every later subscription, so it is not called at all.
- Pause deregisters the listener and ignores events already in flight. The corrected harness tests repeated
  registration and monotonically increasing event sequences after resume. Its cache is disabled because the
  fake service does not implement `org.a11y.atspi.Cache`.

### Measurement precondition

A probe that mixes runtimes measures nothing. The first real run loaded `libdbus-1` and `libglib-2.0` from a
conda environment while `libatspi` came from the system, and `atspi_init()` failed although the accessibility
bus was reachable through QtDBus and GDBus. Any harness for this module must: link without an RPATH into
third-party prefixes, load exactly one `libdbus`/`libglib` set, print the library provenance it actually
loaded when initialisation fails, and pre-check `org.a11y.Bus.GetAddress` — calling `atspi_init()` on an
unreachable bus aborts the process inside `dbind`.

Second data point: with a clean all-system runtime the failure persisted while GDBus reached the same bus, so
a mixed runtime was not the cause. The harness therefore also probes libdbus directly (session bus, then the
accessibility bus address from `org.a11y.Bus.GetAddress`) and captures libatspi's own GLib log messages. Do
not conclude "AT-SPI unavailable" from a return code alone; require the message that explains it.

## 12. Test plan

Contract suite (`tests/contract/selection_backend_test.cpp`) runs against every backend and against
`FakeSelectionBackend`:

| Case | Asserts |
| --- | --- |
| subscribe-then-idle | no event for a pre-existing selection |
| burst | N events emitted promptly; no text resolution happens without a resolve call |
| resolve happy path | text equals the selected content, trimmed, internal whitespace preserved |
| resolve over limit | `SelectionTooLarge`, nothing truncated |
| resolve timeout | `SelectionTimeout` within deadline + margin; late value discarded |
| supersede | resolving seq 1 then requesting seq 2 yields exactly one callback (for seq 2) |
| unsubscribe during resolve | no callback; no crash; thread joins |
| protected | no event, no resolve, no log content |
| self-source | our own window never produces an event |
| stop/start cycles ×50 | no fd leak, no thread leak, no duplicate subscriptions |

Unit tests: text shaping rules (§6.4), range concatenation, INCR accounting, MIME selection order, UTF-8 decode
of split multibyte sequences, `BackendSelector` truth table.

Fakes: `FakeSelectionBackend` (deterministic events, programmable delays/failures) is the only backend used by
coordinator/UI tests.

Manual (M0/M1, recorded in `../linux-integration.md` §9): real desktops × source applications × channels, each
row stating desktop, compositor, source app, channel, result and reproduction notes — never the selected text.

## 13. Open questions (M0 must answer)

Instrumented by [`prototypes/m0-selection-probe/`](../../prototypes/m0-selection-probe/README.md):
`x11 selftest`, `x11 watch`, `atspi list`, `atspi watch`, `atspi dbus-events`, results recorded in its
`RESULTS.md`. No real-session run exists yet; the probe currently reports only its own unavailability in the
sandboxed build environment.

1. Which of AT-SPI option A (QtDBus) or B (libatspi) is viable, and what is the resolution latency and
   hung-application behaviour of each? *API evidence already favours B (see §7.2); latency data pending.*
2. Does AT-SPI emit `text-selection-changed` for the applications that matter (browser page text, editable
   fields, GTK/Qt native, terminals), and what does `GetText` return for multi-element selections and iframes?
3. Does Chromium/Firefox need an accessibility flag, and must that be a documented user step?
4. *Answered for the mechanism, open for per-application coverage:* the self-test now passes 5/5 with zero X
   errors, but every conversion needed the `CurrentTime` retry. Repeated-selection behaviour in real
   applications and XWayland sources still belongs to the M1 verification matrix.
5. Which compositors expose `ext-data-control-v1`/`zwlr-data-control` v2 to a normal client, and does the
   exposed primary selection correspond to what the user selected in the source app?
6. How large can a selection be in practice before `maxSelectionChars=4000` is the wrong default?
7. Is `GetRangeExtents` usable as an anchor under Wayland, or is it only a hint on X11?

## 14. Change log

| Date | Change |
| --- | --- |
| 2026-11-16 | Initial module design; event model, resolution semantics and three candidate backends specified. |
