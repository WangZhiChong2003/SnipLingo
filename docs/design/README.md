# Module Designs

Second design layer under [`../DESIGN.md`](../DESIGN.md). The top-level document decides *what* the product
is and *which* platform approaches are candidates; the documents here decide *how each module is built*:
interfaces, data types, lifecycles, threading, error mapping, configuration and test seams.

No application code exists yet. Every interface in this directory is a design proposal of the level of
detail needed to write the module and its tests; the M0 prototype may invalidate parts of it. Items that
depend on an unverified platform behaviour are listed in each document's *Open questions* section and are
**not** presented as settled.

## Status legend

| Status | Meaning |
| --- | --- |
| **Implemented** | Code exists, builds warning-free and its unit tests pass in CI. |
| **Designed** | Interface, states, errors and tests are specified. Implementable without further design work. |
| **Prototype-dependent** | Specified, but at least one decision must be confirmed by the M0 prototype before it is frozen. |
| **Outline only** | Deliberately deferred; only the preconditions and the shape of the interface are recorded. |

## Modules

| # | Module | CMake target | Milestone | Status | Document |
| --- | --- | --- | --- | --- | --- |
| 01 | Core types, errors, logging, paths, timing | `sniplyngo_core` | M1 | **Implemented** | [01-core.md](01-core.md) |
| 02 | Settings and consent records | `sniplyngo_settings` | M1 | **Implemented** | [02-settings.md](02-settings.md) |
| 03 | Secret storage | `sniplyngo_secrets` | M1 | **Secret Service keyring + session fallback implemented**; real desktop keyring unverified | [03-secrets.md](03-secrets.md) |
| 04 | Selection observation and text resolution | `sniplyngo_selection`, `sniplyngo_platform_atspi` | M0 → M1 | **AT-SPI backend implemented and verified against a live D-Bus fake**; X11/data-control pending | [04-selection.md](04-selection.md) |
| 05 | Request coordination | `sniplyngo_coordinator` | M1 | **Implemented** | [05-coordinator.md](05-coordinator.md) |
| 06 | Translation provider | `sniplyngo_provider` | M1 | **Implemented** (OpenAI-compatible client; key comes through a callback until 03 lands) | [06-provider.md](06-provider.md) |
| 07 | User interface | `sniplyngo_ui` | M1 | **Result window, settings window, consent sheet and runtime hosting implemented**; tray and shortcut UI pending | [07-ui.md](07-ui.md) |
| 08 | Capture and OCR | `sniplyngo_capture`, `sniplyngo_ocr` | M3 | Outline only | [08-capture-ocr.md](08-capture-ocr.md) |

Deliberately absent: a plugin system, a history store, a translation cache, a scheduler, an IPC daemon
protocol, and a Windows adapter layer. Each would need its own decision record first (`../DESIGN.md` §1.4).

## Dependency direction

```
                    ┌──────────────┐
                    │ 07 ui (QML)  │   presentation, controllers, windows
                    └──────┬───────┘
                    ┌──────▼───────┐
                    │ 05 coordinator│  generation, consent gate, state machine
                    └──┬────┬───┬──┘
          ┌────────────┘    │   └────────────┬──────────────┐
   ┌──────▼──────┐   ┌──────▼──────┐   ┌─────▼──────┐  ┌────▼─────┐
   │04 selection │   │06 provider  │   │03 secrets  │  │02 settings│
   └──────┬──────┘   └──────┬──────┘   └─────┬──────┘  └────┬─────┘
          │                 │                │              │
          └─────────────────┴────────┬───────┴──────────────┘
                              ┌──────▼──────┐
                              │  01 core    │
                              └─────────────┘
   08 capture/ocr (M3) depends on 01 + 05 only. Platform backends (AT-SPI, X11,
   Wayland, libsecret) are implementation details behind 03/04/08 interfaces.
```

Rules enforced by review, not by tooling:

1. `core` depends on Qt Core only. It must not know about selections, providers, windows or settings fields.
2. `04`–`06` never include `07`; the UI observes them, never the reverse.
3. Only `04`, `03` and `08` may include platform/system libraries (AT-SPI D-Bus, xcb, wayland-client,
   libsecret). Translation and coordination stay platform-independent.
4. `05` is the only module that decides *when* work happens. Backends and providers never start work on
   their own initiative.
5. No module branches on desktop names (`XDG_CURRENT_DESKTOP`) to choose behaviour; they consume the
   capability/status values produced by `04`.
6. Only `02` reads or writes `settings.json`; other modules receive plain value objects.

## Cross-module conventions

| Concern | Convention |
| --- | --- |
| Fallible result | `core::Result<T>` (`std::variant<T, Error>`); no exceptions across module boundaries; no silent `std::optional` failure |
| Error identity | `core::ErrorCode` enum + non-sensitive `detail` string. Selected text, recognized text, credentials and window titles must never appear in `detail` |
| Staleness | one `Generation` counter for the single shared result window, owned by `05`. Every async completion carries the generation it was started with and is dropped if stale |
| Threading | UI thread owns QML, settings, network and D-Bus portal calls. Each selection backend owns one worker thread. No blocking call on the UI thread |
| Lifetime | QObject parent ownership everywhere; cross-thread delivery through queued signals only; callbacks must outlive their QObject through `QPointer`/`QMetaObject::Connection` |
| Capability | `Probe` (what exists) ≠ `Access` (can this app use it) ≠ `Behaviour` (works with a given source app). Reports keep the three apart |
| User text | in memory only; never logged, never persisted unless a future opt-in feature says so explicitly |
| Configuration | passed in as immutable value objects; modules do not read environment variables except `01` (paths, logging) |
| i18n | no user-visible string inside `01`–`06`; they emit `ErrorCode` and typed payloads. `07` owns all wording |
| Tests | every module has fakes at its own boundary, and each backend has contract tests plus a documented real-desktop check |

## Source layout

```text
src/
  core/        01  result/error, log, xdg paths, generation, cancellable delay
  settings/    02  schema, store, consent record, migration
  secrets/     03  SecretStore interface, libsecret backend, session-only backend
  selection/   04  SelectionBackend interface, BackendSelector, event types, limits
  platform/
    atspi/          AT-SPI backend (QtDBus or libatspi — M0 decision)
    x11/            XFixes PRIMARY backend (xcb)
    wayland/        data-control backend (wayland-client + protocol XML)
  coordinator/ 05  RequestCoordinator, consent gate, state machine
  provider/    06  Provider interface, OpenAI-compatible client, SSE parser, prompt builder
  secrets/     03  SecretStore interface, session-only store, factory
  app/             capability report, CLI parsing, session probing, wiring (no UI yet)
  ui/          07  QML windows/components/theme + controllers registered into QML
  capture/     08  (M3)
  ocr/         08  (M3)
tests/             one executable per test file (see tests/CMakeLists.txt); fakes in tests/fakes/
  unit/            pure logic per module
  contract/        one suite per backend interface, run against fakes and real backends
  fakes/           FakeSelectionBackend, FakeProvider, FakeSecretStore, FakeClock
  fixtures/        non-personal test text; no captured screen content is committed
```

## Milestone mapping

| Milestone | Modules touched | Exit criterion for this design layer |
| --- | --- | --- |
| M0 feasibility | 04 prototypes (throwaway harness) | Documents 04/03 record which backend and secret path are viable; open questions answered with measurements |
| M1 vertical slice | 01, 02, 03, 04 (one backend), 05, 06, 07 | Automatic selection → result window works end-to-end on one X11 and one Wayland configuration, with consent, pause and stale-request tests |
| M2 coverage | 04 (second backend) | A second backend only if a measured coverage gap justifies it; documents updated before code |
| M3 capture/OCR | 08 | 08 leaves *Outline only* status with a full design and a verified permission/temporary-file story |
| M4 release | all | No document claims support that the verification matrix does not show |

## How these documents evolve

- A module-level decision that affects one module is edited in that module document, with the date and the
  reason recorded in its *Change log* section.
- A decision that changes a module boundary, the generation model, or the consent model needs an ADR in
  [`../adr/`](../adr/) first; the top-level [`../DESIGN.md`](../DESIGN.md) is then updated.
- Prototype results replace open questions; a question is never deleted silently — it becomes a decision or
  an explicit limitation.
