# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres
to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Before `1.0.0`, minor versions may include
breaking changes; those are marked `BREAKING` and always come with a migration note.

## [Unreleased]

### Translation and interface improvements

- Use TLS 1.2 for provider connections and bypass configured proxies for loopback services.
- Add an explicit text-entry workflow, source-language selection, editable engine URLs and optional
  LibreTranslate credentials; restore automatic selection subscriptions and show a usable result window.
- Use provider-side language detection where supported, enforce MyMemory's byte limit, and report service
  failures without including response bodies in errors. LibreTranslate now defaults to a local instance;
  the obsolete unofficial Bing endpoint is no longer offered as a new GUI preset.


### Fixed

- Engine errors now name the failing step and host ("token request to edge.microsoft.com: HTTP 404"), and
  an HTML answer suggests pointing the engine at your own instance. A bare "HTTP 404" could belong to either
  request of a two-step engine.
- `engine add <preset> <url>` configures a self-hosted instance (for example a local LibreTranslate), instead
  of forcing the OpenAI-compatible shape.

- `engine test --all` now covers `bing-free`, and every run prints the proxy actually in effect first: two
  silent edit slips meant the shipped binary neither listed Bing nor showed the proxy, which made a 429
  investigation look like "all engines are broken".
- A non-JSON answer identifies HTML without displaying the server response, which may contain private data.

- Proxy support: Qt never read the system proxy, so every engine failed on a proxied network even though the
  proxy worked. `network.proxy` (system/manual/none) is applied once per process, settable with
  `sniplyngo proxy …` and in the settings window, with the effective proxy reported instead of assumed.
- Bing free translation (`bing-free`): edge token fetch + Azure translate call, token cached for nine minutes,
  verified against a local stub with per-request responses.

- Engine management from the command line, so a headless session or a broken UI cannot block setup:
  `engine list [--json]`, `engine use <preset>`, `engine add <id> <url> <model>`, `engine target <code>`,
  and `engine test [<id>|--all]` which sends one word through each candidate engine and reports which one
  actually answers on this network.

### Fixed

- Engine errors now name the failing step and host ("token request to edge.microsoft.com: HTTP 404"), and
  an HTML answer suggests pointing the engine at your own instance. A bare "HTTP 404" could belong to either
  request of a two-step engine.
- `engine add <preset> <url>` configures a self-hosted instance (for example a local LibreTranslate), instead
  of forcing the OpenAI-compatible shape.

- Settings window: the language chooser's delegate referenced an undefined id, so every row logged
  `ReferenceError: control is not defined` and the chooser was unusable. A new QML test opens the popup,
  collects QML warnings and fails on any of them — verified by reintroducing the bug and watching it fail.
- The settings window no longer traps the user on one engine: the "New engine" control was inside the
  collapsible add panel, so configuring an engine hid both the panel and the only way to add another one.
  Engine rows now select on click, each row has a remove action, and newly added engines become active.
  Language codes are shown as names ("Chinese (Simplified)", not "zh-Hans").
- The settings window scrolls, so expanded sections cannot push controls out of reach.

- The GUI now prefers the Wayland platform on Wayland sessions (`QT_QPA_PLATFORM=wayland;xcb`). Qt defaults
  to xcb whenever `DISPLAY` is set, which put the application on the XWayland clipboard and lost Wayland
  selections entirely. The chosen platform is logged at startup, and the clipboard reader also tries
  `mimeData()` because the two paths differ inside the platform plugin.

### Fixed

- Engine errors now name the failing step and host ("token request to edge.microsoft.com: HTTP 404"), and
  an HTML answer suggests pointing the engine at your own instance. A bare "HTTP 404" could belong to either
  request of a two-step engine.
- `engine add <preset> <url>` configures a self-hosted instance (for example a local LibreTranslate), instead
  of forcing the OpenAI-compatible shape.

- `engine test --all` now covers `bing-free`, and every run prints the proxy actually in effect first: two
  silent edit slips meant the shipped binary neither listed Bing nor showed the proxy, which made a 429
  investigation look like "all engines are broken".
- A non-JSON answer identifies HTML without displaying the server response, which may contain private data.

- Proxy support: Qt never read the system proxy, so every engine failed on a proxied network even though the
  proxy worked. `network.proxy` (system/manual/none) is applied once per process, settable with
  `sniplyngo proxy …` and in the settings window, with the effective proxy reported instead of assumed.
- Bing free translation (`bing-free`): edge token fetch + Azure translate call, token cached for nine minutes,
  verified against a local stub with per-request responses.

- `sniplyngo trigger text`: translates text read from stdin, so the pipeline can be exercised without a
  clipboard. The command now reports the engine's real outcome (translation or error code) and exits non-zero
  on failure, locally and over the forwarding socket.
- Clipboard reads wait for the Wayland round trip (bounded) and report the MIME formats they saw when empty.
- MyMemory resolves `auto` source locally (CJK → `zh-CN`, otherwise `en`) because it requires a source language; translating with an empty target language is refused with `SettingsInvalid` instead of sending
  a meaningless request.

- Keyless translation engines so the application is usable without buying anything:
  `SimpleTranslateProvider` with `mymemory`, `libretranslate` and `google-free` types, per-endpoint language
  tags, HTTP-status error mapping, non-streaming delivery, and preset buttons in the settings window.
  Covered by 11 provider cases against a local stub server (request shape, parsing, quota-in-200, malformed
  answers, status codes, cancellation) plus 1 settings case for presets.
- The stub HTTP server now resets its per-connection state, so a test can drive more than one request.

- Settings surface: `SettingsController` (provider list and CRUD, target language, automatic/pause,
  consent lifecycle with a destination fingerprint, credential presence without ever exposing the key) plus
  `SettingsWindow.qml` and `ConsentSheet.qml`, covered by 9 controller cases and an offscreen QML test.
- GUI runtime (`app::runGui`): single-instance guard via lock file, request forwarding over a local socket,
  QML windows embedded as resources, `trigger clipboard` wired through the coordinator, and honest
  degradation when observation or the socket is unavailable. The coordinator now accepts a missing backend
  so explicit flows keep working on sessions without selection support.

- Result window: `ResultController` (state keys, plain text, retry/copy/original actions, 20 Hz delta
  coalescing, clipboard only on explicit action) and `ResultWindow.qml` + `Theme.qml` tokens, with an
  offscreen QML load test proving the bindings follow the controller and survive a null controller.
- Desktop keyring support: `SecretServiceStore` (OpenSession, SearchItems, Unlock, CreateItem with
  replace, GetSecret, Delete) selected whenever `org.freedesktop.secrets` is registered, with session-only
  storage kept as the announced fallback. Verified against a fake keyring that also asserts the wire
  signatures (`a{ss}`, `a{sv}(oayays)b`); a real GNOME Keyring/KWallet session remains unverified.

- M1 implementation start: `sniplyngo_core` (Result/Error policy tables, monotonic generation, cancellable
  delay, XDG paths, log with no user-text API) and `sniplyngo_settings` (schema, clamping validation,
  provider rules, consent bound to a destination fingerprint, atomic 0600 storage, corrupt-file recovery,
  unknown-key preservation). Six unit-test executables, all passing; `-Wall -Wextra -Wpedantic` clean and
  `clang-format`-clean, so the CI build job — previously skipped — now runs.
- Repository build skeleton: root `CMakeLists.txt` with per-module static libraries and `CMakePresets.json`
  (`dev`, `ci`), plus a `format` target.
- AT-SPI selection backend (`src/platform/atspi/`): event subscription on a dedicated thread, eager read with a
  single cached result (libatspi's loop cannot be woken), password/protected roles ignored, own-application
  events ignored, zero-length ranges dropped, size limit applied at resolve time, and an honest pre-check of
  the accessibility bus because `atspi_init()` aborts the process on an unreachable bus. The pure decision
  logic is unit-tested; the libatspi glue is compile-checked and awaits a live-session run.
- Live AT-SPI integration test: a fake accessibility service speaking the real wire protocol, served over a
  private D-Bus daemon on TCP (with a test-only `AI_ADDRCONFIG` shim for loopback-only network namespaces).
  It exercises the libatspi glue for real and produced concrete protocol facts (`siiva{sv}` signal signature,
  `ii` out-arguments for ranges, fully qualified interface names, `atspi_init` only once per process,
  `atspi_exit` breaks later subscriptions). Pause keeps one subscription and stops processing, because
  libatspi 2.44 cannot re-subscribe — documented in `04-selection.md` §7.2 and `05-coordinator.md`.
- X11 selection backend (`src/platform/x11/`): XFixes PRIMARY ownership notifications on a dedicated thread
  with a wake pipe for commands, on-demand conversion (UTF8_STRING, INCR assembly, size caps), the measured
  `CurrentTime` strategy, pointer-based anchoring and `_NET_WM_PID` source identification. Protocol decisions
  are unit-tested; the conversion path still needs a real X session to be called verified.
- `sniplyngo watch --duration MS [--show-text]`: diagnostic that subscribes through the chosen backend and
  prints what it sees (application, character count, anchor availability), so the backend can be exercised
  without the UI.
- First runnable binary: `sniplyngo caps|diag|version|help` prints a capability report (session, portal
  versions, chosen selection backend with its stated limits, credential availability, and notes) as text or
  JSON, without reading any user data. Commands that need the UI or selection backends exit with code 2 and
  say why.
- `sniplyngo_secrets`: `SecretStore` interface, session-only store (memory, cleared on shutdown) and a factory
  that reports persistent storage as unimplemented instead of pretending. Credential failures never carry
  the secret itself.
- `sniplyngo_selection` interfaces and the backend-selection policy (`BackendSelector`, with a session-level
  truth table test); `sniplyngo_provider` with an incremental SSE parser, versioned prompt builder and an
  OpenAI-compatible client tested against a local stub HTTP server (request shape, chunk-split streaming,
  non-streaming responses, 401/403/429/500/302 mapping, cancellation, first-byte deadline); and
  `sniplyngo_coordinator` implementing the documented state machine with generation-based staleness, the
  consent gate, silent-outcome policy and retry retention. Ten test executables, all passing.

- M0 feasibility probe (`prototypes/m0-selection-probe/`): XFixes `PRIMARY` self-test and watcher, AT-SPI
  watcher (libatspi) and event probe (QtDBus), capability report, JSONL measurement records, plus a results
  template. Builds cleanly; no complete real-session measurements exist yet.
- M0 probe fixes after the first real run: XFixes events are matched as `first_event + subtype` (matching the
  bare constant silently produced zero events), the AT-SPI probe is now a separate Qt-free binary built with a
  sanitised environment because a conda/system runtime mix made `atspi_init()` fail, `RegisterEvent` tries both
  wire signatures, and every AT-SPI path pre-checks `org.a11y.Bus.GetAddress` instead of aborting inside
  `dbind`. `build.sh` refuses to build with a foreign RPATH or foreign `libdbus`/`libglib`.
- Second M0 run fixes: the X11 self-test no longer shares one xcb connection between two threads (XCB is
  single-threaded; this cost 4 of 5 conversions), failures now report whether the selection is still owned,
  and `diag` probes libdbus against the session bus and the accessibility bus and captures libatspi's own GLib
  messages instead of trusting a bare `atspi_init()` return code.
- Third M0 run fixes: `atspi_init()` returns 0 on success (the probe read it as a boolean, so a working
  accessibility stack was reported unavailable three times); the X11 self-test prints the real PRIMARY owner
  window and whether it belongs to this process, counts X protocol errors (a refused conversion is an error
  event, not a timeout) and retries conversion once with `CurrentTime`.
- Fourth M0 run: `atspi_init()` confirmed working; the probe now prints selection-range offsets and character
  counts (terminals reported ranges with no text), supports `--events object` to find out whether an
  application emits any object event at all, honours its own deadline despite `atspi_event_quit()` not
  stopping `atspi_event_main()` in at-spi2-core 2.44, and ships `selftest-exit` to prove the deadline path
  terminates.
- Module-level design layer under `docs/design/`: module map, dependency rules, cross-module conventions and
  one document per module (core, settings/consent, secrets, selection observation, coordinator, provider, UI,
  capture/OCR outline), each with status, interfaces, states, error mapping and test plan.
- Revised the product direction to automatic selection translation, with explicit observation and remote
  transmission consent, a pause control, and an explicit paste alternative.
- Added ADR 0005 and documented candidate X11, AT-SPI and Wayland data-control approaches with a real
  desktop verification matrix; all remain unimplemented and untested.
- Top-level design documentation: architecture, module contracts, capability model, data flows,
  performance budgets, error taxonomy, roadmap, and risks (`docs/DESIGN.md`).
- UI/UX specification: interaction model, design tokens, window geometry, popup states, keyboard contract,
  accessibility and localization rules (`docs/ui-ux-spec.md`).
- Linux integration notes: global shortcut ladder, text acquisition ladder, capture backends, window
  anchoring, fractional scaling, sandbox and dependency matrices (`docs/linux-integration.md`).
- Architecture decision records 0001–0004: application stack, selection-first acquisition,
  provider-agnostic translation, Flatpak as primary channel (`docs/adr/`).
- Repository standards: `README.md` (+ Simplified Chinese translation), `LICENSE` (GPL-3.0-or-later),
  `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, this changelog, editor/format configuration,
  issue and pull request templates, CI workflow, dependency update configuration.

### Notes

- No application code exists yet. Feasibility prototypes are the next milestone; see
  [`docs/DESIGN.md` §13](docs/DESIGN.md#13-roadmap).

[Unreleased]: https://github.com/WangZhiChong2003/SnipLingo/commits/main
