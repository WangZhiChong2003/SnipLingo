# SnipLingo — Top-Level Design

| Field | Value |
| --- | --- |
| Status | Draft; platform approaches require prototypes and desktop testing |
| Scope | Linux-first automatic selection translation, explicit alternatives, architecture and release criteria |
| Related | [Linux integration](linux-integration.md), [UI/UX](ui-ux-spec.md), [module designs](design/README.md), [architecture decisions](adr/) |

This document is a design, not a description of an implemented product. The repository has no runnable
application yet. Interface specifications establish possible approaches; they do not establish that an
application, compositor or installation channel supports the complete workflow.

## 1. Product Definition

### 1.1 Problem and goal

SnipLingo aims to show a translation automatically after a user selects text, without requiring a copy,
shortcut or app switch each time. Linux is the first target; Windows is a future port, not a v1 promise.
The selected text, rather than the clipboard or a screenshot, is the primary input.

Automatic selection observation requires an explicit first-run choice. When the user has enabled it and
configured a provider, selecting text is the trigger. The app offers an obvious pause/resume control and
stops observing on exit. Before enabling automatic use of a remote provider, the user is told that each
eligible selection can be sent to that provider and may incur costs. No automatic remote request is made
until the user confirms that behavior. A configured local provider is an alternative.
Consent is specific to the configured destination: changing a remote endpoint requires confirmation again
before automatic text transmission resumes.

### 1.2 Workflows

| Flow | Trigger | Input | Intended behavior |
| --- | --- | --- | --- |
| A. Automatic selection | A new selection event while automatic mode is enabled | The selection associated with that event | After a short stability delay, acquire it, translate it, and show a passive result window. |
| B. Explicit text | User opens the app and pastes text, or explicitly chooses clipboard translation | User-provided text | Translate the supplied text; never infer that an old clipboard value is the current selection. |
| C. Region OCR | User explicitly requests capture and selects a region | Captured image | Recognize and translate it, subject to the capture backend and user permission. |

A and B are the first milestone. C is a later fallback for unselectable text; it is not guaranteed to be
available on every desktop. A configurable manual shortcut may initiate B or C where supported, but is not
required for A. No automatic screenshots, synthetic copy, keyboard hooks or continuous clipboard monitoring
are needed for A.

### 1.3 Capability and release claims

- X11 `PRIMARY` / XFixes, Wayland data-control and AT-SPI are candidate event sources, with different
  coverage and permissions. Even X11 cannot guarantee that every application exposes every selection change.
- GNOME Wayland does not have a universal public selection API for unfocused applications. AT-SPI offers a
  promising, application-dependent path; protocol availability alone is not proof of working automatic
  translation. Detailed conditions and sources are in [Linux integration](linux-integration.md).
- A support claim needs a real test for the desktop, compositor version, installation channel, source
  application and text type. An unavailable or unverified automatic path must be reported honestly; the
  user can still open the explicit paste flow.
- Never silently translate clipboard contents as an automatic-selection fallback. Never claim that
  selection-to-popup works identically on all Wayland and X11 applications.

### 1.4 Non-goals

Continuous screenshots or keystroke monitoring, automatic clipboard translation, privilege escalation,
input injection, cloud sync, telemetry, accounts, a plugin framework, and document-layout reconstruction
are outside the initial product. Additional providers, OCR engines, dictionary layouts and history are
future features, not prerequisites for proving the core workflow.

### 1.5 Acceptance criteria

The first usable release must establish a tested support matrix for representative GNOME Wayland, KDE
Wayland and X11 sessions, including native and, if shipped, Flatpak installations. It must provide automatic
selection translation where a tested backend works; explain limitations and provide explicit paste where it
does not; allow pausing observation; honor provider consent; and pass the cancellation and privacy tests in
§11. Unsupported combinations remain labeled as such. Performance targets are set after measurement.

## 2. Design Principles

1. **Automatic when supported, honest when not.** A detected protocol is a prerequisite, not proof that a
   particular app sends usable selection events.
2. **Privacy by explicit choice.** Selection observation and automatic remote transmission are separately
   understandable to the user. Pausing stops observation and pending requests.
3. **Correct input over convenience.** Automatic, clipboard, paste and capture have distinct triggers and
   provenance. Never substitute stale clipboard data or inject input into the source application.
4. **Event driven and cancellable.** Observe only the required selection events, coalesce changes, and prevent
   stale requests from updating the shared result window.
5. **Small, platform-aware implementation.** Start with a measured vertical slice; add backends only after
   they demonstrate useful coverage. Respect compositor and sandbox boundaries.

## 3. Technology Selection

### 3.1 Stack

| Layer | Initial choice | Notes |
| --- | --- | --- |
| Language and build | C++20, CMake and Ninja | CMake presets and application code are planned, not present. |
| UI | Qt 6.4+ / Qt Quick | Prototype native behavior, startup and memory; Qt does not remove compositor limitations. |
| Linux selection | AT-SPI; X11 `PRIMARY`/XFixes; optional Wayland data-control | Integrate only backends justified by a prototype; AT-SPI access and target coverage vary. |
| Translation | One configurable provider, asynchronous HTTP | Local or remote endpoint chosen by the user; no silent provider switching. |
| Credentials | OS-backed secure storage | On Linux, evaluate libsecret and the relevant sandbox backend. Failure is explicit; no plaintext fallback. |
| OCR | Tesseract, later milestone | Requires a separate user-initiated capture workflow. |
| Packaging | Native development build first; Flatpak evaluated in parallel | Distribution choice depends on demonstrated automatic-selection and permission behavior. |

The Qt choice is a reasonable starting point, not evidence that this stack meets the performance budget or
is universally better than other toolkits. See [ADR 0001](adr/0001-application-stack.md).

### 3.2 Portability

Selection observation, screen capture, global shortcuts, secure storage and window placement are platform
boundaries. Translation and OCR do not depend on Linux presentation APIs. Linux XDG paths and libsecret stay
inside platform adapters. A Windows port can later provide its own adapters; no Windows implementation or
compatibility is claimed now.

## 4. Architecture

### 4.1 Process and module boundaries

A single resident application owns settings, selection observation, request coordination and the result
window. Background work must not block the GUI. Keep modules small and introduce interfaces where an actual
platform or backend boundary requires one:

```text
platform selection observers ─┐
explicit paste / clipboard ───┼─> request coordinator ─> provider ─> result window
user-initiated capture ─> OCR ┘
```

Selection observers produce a source-specific event and asynchronously resolve only the relevant selected
text. The coordinator owns the current request and its cancellation. Provider and capture operations also
complete asynchronously: portal requests and selection transfers may wait for another process or a user.
Callbacks and receivers must have explicit lifetimes. The exact C++ interface should be finalized after the
first backend prototype rather than prescribing synchronous return types in advance.

Module-level interfaces, states, error mappings and test seams are specified in
[`design/README.md`](design/README.md) and one document per module in [`design/`](design/). Those documents
carry a status per module: designed, prototype-dependent, or outline only.

### 4.2 Selection observation

1. Automatic mode is inactive until the user enables it. A new source subscription starts without
   translating an already-existing initial selection; resume behaves the same way.
2. Observe only the selected source's selection-change events, not every key, pointer motion, clipboard
   update or screen frame. Ignore events from SnipLingo itself and protected/password controls when
   identifiable. Accessibility exposure is imperfect, so the user can pause before selecting sensitive text.
3. Coalesce a burst of changes from the same source with a short configurable stability delay; the initial
   value is a hypothesis to measure. Cancel the previous pending resolution when a newer selection arrives.
4. Resolve text for the current event with a size limit and timeout; cancel or discard a result if its
   selection, source, application or request is known to have changed. An empty, protected, inaccessible or failed
   selection produces no remote request. Do not log its content.
5. Do not suppress a later deliberate selection merely because its text matches an earlier request. Do not
   join/de-hyphenate user-selected text or skip translation solely from script detection.
6. If more than one event source is available, start with one chosen and reported source. Do not subscribe
   to competing sources and fire duplicate translations; expand coverage only with tested correlation and
   explicit source precedence.

### 4.3 Request lifecycle

One generation identifies the current operation for the **shared result window**, across automatic
selection, paste and OCR. A new operation, pause or dismissal invalidates the old generation. Cancel pending
selection resolution, portal requests and network replies where their APIs allow it; discard late completions
in all cases. OCR cancellation may be cooperative: an already-running inference must not update the UI after
it is invalidated. Shutdown must not destroy a running worker or block the GUI indefinitely.

The coordinator owns request data only as long as necessary. Do not keep a queue of selected text for later
translation. A translation failure is visible but never falls back to a different remote provider.

## 5. Capability Model

A capability report records the actual session and separately reports protocol presence, access, backend
initialization, and demonstrated behavior. `doctor` is planned; it must not read or print private selections
as part of diagnosis. An event source can be available yet not cover a particular application.

| Capability | Candidate or check | Limitation |
| --- | --- | --- |
| Automatic selection on X11 | XFixes selection-owner notifications and `PRIMARY`; AT-SPI | The source app may not publish changes or text. |
| Automatic selection on Wayland | AT-SPI; `ext-data-control-v1` or `wlr-data-control` primary selection where exposed | A compositor, sandbox or source application may deny or omit the required operation. |
| Manual text | User pastes into the app; explicit clipboard action only when supported | The Wayland clipboard is focus- and backend-dependent. |
| Capture | Portal screenshot or a supported native backend | Permissions, selection UI, temporary files and scope vary by backend. |
| Global shortcut | Portal or X11/desktop binding | Binding can be unavailable or rejected; GUI entry remains available. |
| Popup location and focus | Toolkit/compositor behavior | Coordinates and keyboard focus are not universally controllable under Wayland. |

Availability detection must not request screen capture, read a selection, send text, or make a remote
request. A real selection test is a separate user-initiated action. See [Linux integration](linux-integration.md).

## 6. Interaction Design Summary

Automatic results should be readable and nonintrusive: show a passive window without moving the pointer or
forcibly taking focus. Position near the selection where a trustworthy location and supported window API
exist; otherwise let the desktop place the window. Do not claim that the result is always centered or located
on the active monitor.

A passive window cannot receive local keys while another app has focus. `Esc`, copying and other local
shortcuts work once the user focuses the result; an optional registered global action may offer cancellation
where supported. The user can always pause automatic mode and open explicit paste from a normal application
window, including on desktops without a tray. See [UI/UX](ui-ux-spec.md).

## 7. Configuration and Data

Persist only preferences and secure credential references by default. Initial settings include target
language, provider, automatic-selection enabled/paused state, and selection backend choice where needed.
The target language defaults from the desktop locale where possible and remains configurable. Explicit
activation of automatic selection and remote sending is part of onboarding, not implied by a preset URL.

Selected/recognized text and results stay in memory during the operation and are not logged. History and
translation caching are deferred; if introduced, each requires its own opt-in, retention and deletion
policy, including any selected text or metadata they store. Credentials use secure OS storage; if it is
unavailable, explain the failure and allow a session-only credential, rather than silently writing plaintext.

The provider interface accepts source text, source and target language and a request identifier; it reports
success, failure and optionally streamed output. Connect and total timeouts are explicit. User configuration
of a remote endpoint does not itself imply consent to automatically send every selection.

## 8. Privacy and Security Design

- Automatic mode observes selection events only after explicit activation; pause unsubscribes or otherwise
  stops observation and invalidates outstanding work. Do not monitor the normal clipboard, keyboard or
  screen continuously.
- A user explicitly confirms automatic transmission to the selected remote provider before it starts and
  again if the destination changes. Show the destination and provide pause and provider-change controls.
  A local endpoint is an option, but the app cannot guarantee that a separately configured service never
  forwards data.
- Selection and OCR text are untrusted. Render plain text, limit input/output sizes and avoid sensitive
  contents in logs or diagnostics. Do not synthesize Ctrl+C or silently alter the clipboard.
- Capture occurs only on a separate explicit user request. Prefer region-limited capture where actually
  offered. If a backend captures a whole display or may create a temporary file, disclose that before use.
  The app must not itself persist screenshots; portal/backend storage and cleanup guarantees require
  per-backend verification. Do not delete an arbitrary returned URI without verified ownership.
- No plaintext credential fallback, telemetry, silent remote fallback, or privileged device access.

See [SECURITY.md](../SECURITY.md) for the threat model.

## 9. Performance

Low idle CPU and a responsive result window are goals, not measurements. First establish reproducible
baselines for idle CPU/RSS, selection-event-to-window latency, source-resolution latency and provider time to
first result on named desktop/application combinations. Measure any debounce delay separately from network
latency. Do not make provider latency or user permission dialogs part of an unconditional application SLA.
Avoid polling, repeated model loads and unnecessary copies; optimize measured bottlenecks only.

## 10. Error Handling

Distinguish automatic backend unavailable, source app not exposing selection, empty/protected selection,
provider not configured, permission denial, timeout and cancellation. Provide a concrete paste or settings
path where appropriate. An automatic source that fails for a single app must not silently use the clipboard.
Cancellation is not an error. Do not display a distracting error for every empty selection event.

## 11. Testing Strategy

- Unit-test event coalescing, repeated identical selections, input limits, provider parsing and a single
  request generation shared by all flows. Test that pause and shutdown invalidate delayed callbacks.
- Contract-test actual backend behavior and fakes separately, including selection owner changes, empty and
  protected content, source failure, cancellation, stale responses, and no clipboard/network access on an
  unsupported automatic selection.
- Test providers against a local stub, including timeouts, streaming and non-streaming responses. Check that
  no automatic remote request occurs before consent and no private text reaches logs.
- Run real desktop tests for GNOME Wayland, KDE Wayland and X11 with browser web text, editable fields,
  native Qt/GTK apps, terminals and repeated selections in the same app. Record desktop, compositor, source
  app, install channel, focus, scaling and actual result. Include XWayland separately.
- Verify capture permission, cancellation, image ownership and per-monitor geometry on real desktops before
  making claims. Mocked tests and a present protocol do not establish desktop compatibility.

## 12. Packaging and Distribution

Begin with a native development build to test selection and accessibility behavior. Evaluate Flatpak with
real AT-SPI access, data-control exposure, portal backends and credential storage before selecting it as a
primary channel. A sandbox may change capability or require explicit user-facing permissions; do not assume
feature parity. Package metadata, startup integration and supported distribution channels are release work,
not yet implemented. See [ADR 0004](adr/0004-flatpak-primary-channel.md).

## 13. Roadmap

| Milestone | Deliverable and evidence |
| --- | --- |
| M0 — Feasibility (**done**) | AT-SPI and X11 selection prototypes plus an automatic harness; results in [`../prototypes/m0-selection-probe/RESULTS.md`](../prototypes/m0-selection-probe/RESULTS.md) and the backend decision in [ADR 0006](adr/0006-m0-backend-outcome.md): AT-SPI read path proven against a controlled provider (11/11, 1.3 ms), XFixes conversion proven (5/5) with a `CurrentTime` requirement, application coverage recorded as a limitation rather than a promise. |
| M1 — First vertical slice | Explicit paste, one provider, result window, opt-in automatic selection on the backends proven in M0, pause/resume, consent and stale-request tests. |
| M2 — Coverage | Add a second selection backend only where it measurably helps; test XWayland and additional desktops, and document limitations. |
| M3 — Region OCR | Explicit capture and Tesseract, only on tested backends with an accurate permission and temporary-file story. |
| M4 — Release | Localization, accessibility, packaging and real desktop matrix; defer extra providers and features until justified. |

M0 is a risk-reduction experiment, not a claim that the automatically selected backend will work everywhere.

## 14. Risks and Open Questions

- Which combinations of source application, native/Flatpak packaging and Wayland compositor actually expose
  usable AT-SPI selection events and text? Does browser accessibility need to be enabled?
- A stability delay does not prove that the user has finished dragging. Intermediate selections may result
  in multiple requests, including remote requests after consent; measure and disclose this limitation.
- When does X11 `PRIMARY` notify on repeated changes in the same application, and what is the right delay
  before resolving text? How does XWayland bridge selections in each tested session?
- Which compositors expose data-control to this application, and does the offered primary selection cover
  the source applications that matter? A protocol may exist but be restricted to privileged clients.
- Can the passive result window be positioned and interacted with acceptably on each target without taking
  focus? The fallback is compositor placement plus explicit interaction.
- Can any screenshot backend meet the desired persistence and permission guarantees on the target desktop?
  If not, disclose the limitation and offer paste without automatic screenshots.

## 15. Appendices

### A. Planned CLI surface

```text
sniplyngo                       # open or activate the application
sniplyngo settings              # open settings, including pause/resume
sniplyngo trigger clipboard     # explicit clipboard translation where available
sniplyngo doctor [--json]       # non-sensitive capability report
```

Commands are design proposals; none exist yet. Global shortcut and capture commands will be specified when
their backends have been validated.

### B. Terminology

- **Selection observation:** an enabled event subscription for selected text, not general clipboard,
  keyboard or screen monitoring.
- **Automatic mode:** a user-enabled flow that can translate a new eligible selection without a hotkey.
- **Capability:** an interface and access condition, distinct from successful behavior in a particular app.
- **Generation:** an identifier shared across all operations that can replace the result window.
