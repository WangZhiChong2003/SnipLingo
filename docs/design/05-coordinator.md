# 05 — Request Coordination (`sniplyngo_coordinator`)

| | |
| --- | --- |
| Milestone | M1 |
| Status | **Designed** |
| Depends on | `01 core`, `02 settings`, `04 selection` (interface), `06 provider` (interface), `03 secrets` (via 06) |
| Used by | 07 (the only consumer of coordinator signals) |
| Document owner | maintainer |
| Implementation | **Done** — `src/coordinator/`, `tests/coordinator_test.cpp` (11 behaviours, including staleness, consent gate, silence and retention) |

## 1. Scope

The single place that decides *when* work happens: it receives selection events, applies the stability delay,
gates automatic transmission on consent, starts and cancels provider requests, and owns the one generation that
protects the shared result window. It contains no platform code, no HTTP and no QML.

## 2. Responsibilities

- Enforce the automatic-mode state machine (disabled / paused / active) and apply pause by actually
  unsubscribing from the backend.
- Coalesce bursts of selection events with one cancellable delay.
- Resolve text, check limits, and drop work for superseded selections.
- Enforce the consent gate for automatic remote transmission and surface `ConsentRequired`/`ConsentStale`.
- Run provider requests, forward streamed deltas, and treat every completion as "current or discard".
- Guarantee that exactly one result window content exists at a time, across all flows.
- Keep user text only as long as the window shows it.

## 3. Non-responsibilities

- No selection mechanics (04), no HTTP details (06), no rendering or wording (07), no consent record storage
  (02), no capability probing (04).
- No queue of pending selections, no history, no cache, no retry timer.

## 4. Public interface

```cpp
// src/coordinator/request_coordinator.h
namespace coordinator {

enum class State {
    Disabled,          // automatic mode disabled by the user or not yet configured
    Paused,            // automatic mode enabled but observation is stopped
    Idle,              // observing, nothing in flight
    WaitingStability,  // event received, delay running (no text read yet)
    Resolving,         // asking the backend for the selected text
    Translating,       // provider request in flight (streaming or not)
    ShowingResult,     // a completed translation is displayed
    ShowingError       // a user-visible failure is displayed
};

enum class Origin { AutomaticSelection, ExplicitPaste, ExplicitClipboard };

struct ResultView {
    quint64 viewId = 0;            // presentation token; UI ignores stale ids
    State state = State::Idle;
    QString translatedText;        // plain text only
    QString originalText;          // empty when not disclosable (protected/empty source)
    QString providerLabel;         // "api.example.com · gpt-4o-mini"
    bool streaming = false;
    bool originalCollapsed = true;
    core::Error lastError;         // valid when state == ShowingError
    Origin origin = Origin::AutomaticSelection;
};

} // namespace coordinator

// Forward declarations of the collaborating modules (04, 06, 02).
namespace selection { class SelectionBackend; }
namespace provider { class Provider; }
namespace settings { class SettingsStore; }

namespace coordinator {

class RequestCoordinator : public QObject {
    Q_OBJECT
public:
    RequestCoordinator(selection::SelectionBackend *backend,
                       provider::Provider *provider,
                       settings::SettingsStore *settings,
                       QObject *parent = nullptr);

    State state() const;
    bool automaticEnabled() const;
    bool observationActive() const;      // backend subscribed and not paused

    // User intent (from 07)
    core::Result<void> enableAutomatic();       // requires provider configured; consent handled by UI first
    void disableAutomatic();
    core::Result<void> setPaused(bool);         // unsubscribes / resubscribes
    core::Result<void> submitExplicitText(QString text, Origin origin);
    void dismissResult();
    void retryLastRequest();
    core::Result<void> recheckConsent();        // called after the UI records or revokes consent

signals:
    void stateChanged(coordinator::State);
    void viewChanged(const coordinator::ResultView &view);
    void consentRequired(const QString &destinationLabel, bool staleConsent);
    void notice(const core::Error &error);   // non-window notices (e.g. backend unavailable)
};

} // namespace coordinator
```

## 5. State machine

| From | Event | Conditions | To | Actions |
| --- | --- | --- | --- | --- |
| `Disabled` | `enableAutomatic()` | provider configured | `Idle` | `backend->subscribe()`, persist `automaticEnabled=true` |
| `Disabled` | `enableAutomatic()` | no provider | `Disabled` | return `ProviderNotConfigured` |
| `Paused` | `enableAutomatic()` | provider configured | `Idle` | `subscribe()`, persist paused=false |
| `Idle` | `selectionEvent` | `event.seq > lastSeenSeq` | `WaitingStability` | `gen.next()`, start delay |
| `Idle` | `selectionEvent` | stale/duplicate seq | `Idle` | ignore |
| `WaitingStability` | `selectionEvent` | newer seq | `WaitingStability` | restart delay, `gen.next()` |
| `WaitingStability` | delay elapsed | — | `Resolving` | `backend->resolveLatest(seq, limits, cb)` |
| `WaitingStability` | `setPaused(true)` / `dismiss` | — | `Paused` / `Idle` | cancel delay, `gen.invalidate()`, `backend->cancelPending()` |
| `Resolving` | resolve ok | `isCurrent(gen)` | `Translating` | consent gate → provider request |
| `Resolving` | resolve ok | consent missing/stale | `Idle` | `consentRequired(...)`, drop text |
| `Resolving` | resolve error | silent code | `Idle` | log debug, leave window untouched |
| `Resolving` | resolve error | provider/acquisition error worth showing | `ShowingError` | `viewChanged` with error |
| `Translating` | delta | `isCurrent(gen)` | `Translating` | update view text |
| `Translating` | finished | current | `ShowingResult` | final view |
| `Translating` | failed | current | `ShowingError` | error view + actions |
| `Translating` | `selectionEvent` | newer seq | `WaitingStability` | abort provider request, start delay |
| `Translating` | `setPaused(true)` / `dismiss` | — | `Paused` / `Idle` | abort, invalidate |
| `ShowingResult`/`ShowingError` | `selectionEvent` | newer seq | `WaitingStability` | start new cycle |
| `ShowingResult`/`ShowingError` | `dismissResult()` | — | `Idle` (or `Paused`) | invalidate, clear retained text |
| `ShowingError` | `retryLastRequest()` | text retained | `Translating` | same generation policy, manual retry only |
| `Idle`/`Paused`/`Showing*` | `disableAutomatic()` | — | `Disabled` | `unsubscribe()`, invalidate |

Invariants (asserted in debug builds and by tests):

1. `state()==Idle` implies `backend->state()==Running`.
2. `state()==Paused` or `Disabled` implies no backend subscription.
3. At most one provider request is in flight.
4. `viewId` is monotonically increasing; the UI must ignore any view older than the last one it received.
5. Automatic flows never send text without `settings.mayAutoSend()` being true at that moment.

## 6. Generation and staleness

- One `GenerationSource` covers automatic selection, explicit paste and (later) OCR. The coordinator is its only
  owner.
- Every asynchronous result carries the generation it started with; a completion whose generation is not
  current is discarded and logged at debug with the module name only.
- `gen.next()` happens on: a new event, an explicit submit, retry, pause, disable, dismiss.
- Cancellation is attempted first (`cancelPending()`, `provider->cancel(gen)`), then the generation check
  guarantees correctness even when cancellation races.

## 7. Consent gate

Evaluated immediately before starting a provider request, never earlier:

```
automatic flow:
  allowed  ⇔ settings.selection.automaticEnabled
          && !settings.selection.paused
          && (provider is local || consent.fingerprint == provider.fingerprint())
  otherwise: emit consentRequired(label, stale?) once per enable/settings change, drop the text, go Idle.

explicit flow (paste/clipboard):
  always allowed — the user just expressed intent for this specific text.
  The result window still shows the destination label.
```

Consequences:

- No text is ever queued while consent is missing: the selection is gone when the user confirms, and the next
  selection triggers normally.
- Changing the endpoint while automatic mode is on: `settings.changed` → coordinator re-evaluates and, if the
  fingerprint no longer matches, returns to `Idle` and emits `consentRequired(stale=true)`; the next selection
  is dropped rather than sent.
- The coordinator never writes consent. It only reads `SettingsStore` and reacts to `changed`.

## 8. Text and memory lifetime

| Data | Kept while | Cleared on |
| --- | --- | --- |
| Selected/typed text | from resolve success until the result window is dismissed or replaced | `dismissResult()`, new event, pause, disable, shutdown |
| Translation (streamed) | same | same |
| Original text (for the toggle) | same | same |
| `lastSeenSeq` per backend | process lifetime (a number) | restart |
| Anything else | never persisted, never logged | — |

Retry keeps the last request's text only because the window is still visible with that content; `Retry` after
dismissal does nothing (and the action is not offered).

## 9. Concurrency and threading

- Lives on the UI thread. All provider and backend notifications arrive through queued connections.
- The stability delay uses `core::CancellableDelay` (no `QTimer::singleShot`).
- No blocking call is made from any slot: resolution and provider work are asynchronous by interface.
- Shutdown: `disableAutomatic()` → `unsubscribe()` → abort provider → join the backend thread with a bounded
  wait. If the wait times out, log `Internal` and continue exiting (never hang the desktop session).

## 10. Error policy

| Error code | Window? | Notes |
| --- | --- | --- |
| `SelectionEmpty`, `SelectionNotText`, `SelectionProtected`, `SelectionCancelled` | no | debug log only; the old result stays visible; no per-event dialog storm |
| `SelectionTimeout` | no | debug log + (optional, once per session) an inline hint in the app window |
| `SelectionTooLarge` | yes, as an actionable view | "selection too long (N > limit)" + `Translate shortened text` (explicit) + `Cancel`; truncation only on explicit user action |
| `SelectionBackendAbsent`, `SelectionBackendDenied`, `SelectionBackendFailed` | app window notice | automatic mode becomes `Disabled` with the reason and the paste alternative |
| `ConsentRequired`, `ConsentStale` | app window prompt | one prompt, no repeated dialogs |
| `ProviderNotConfigured` | app window | link to settings |
| `ProviderAuthFailed`, `ProviderRateLimited`, `ProviderTimeout`, `ProviderNetwork`, `ProviderUnavailable`, `ProviderBadResponse` | result window error state | Retry + Settings; the failing destination is named |
| `ProviderCancelled` | no | superseded by newer work |

The coordinator emits codes only; wording belongs to 07.

## 11. Configuration consumed

`selection.automaticEnabled`, `selection.paused`, `selection.stabilityDelayMs`, `selection.maxSelectionChars`,
`selection.resolveTimeoutMs`, provider config and consent (all via `SettingsStore`). Changing any of them while
running triggers re-evaluation of state and (for delay/limits) applies to the next cycle.

## 12. Dependencies and boundaries

Allowed: `01 core`, `02 settings`, `04 selection` interface, `06 provider` interface, Qt Core.
Forbidden: Qt Quick/Widgets, QtDBus, xcb, wayland-client, libsecret, settings file I/O, any platform header.

## 13. Test plan

Unit (`tests/unit/coordinator/`), using `FakeSelectionBackend`, `FakeProvider`, `FakeSecretStore`, a test clock:

| Test | Asserts |
| --- | --- |
| `state_machine_test` | the table in §5 row by row (table-driven; a new row without a test fails review) |
| `no_initial_translation_test` | enabling and resuming never produce a request without a new event |
| `pause_unsubscribes_test` | pause ⇒ `unsubscribe()` called once, no callback thereafter, resume ⇒ exactly one `subscribe()` |
| `consent_gate_test` | remote + no consent ⇒ zero network calls, one `consentRequired`; local ⇒ no prompt; reversal after revocation |
| `stale_completion_test` | late resolve and late provider completion for an old generation never change the view |
| `streaming_replace_test` | new event during streaming aborts the request and clears the partial text |
| `silent_error_test` | empty/protected/cancelled produce no window and no notice |
| `too_large_test` | over-limit produces an actionable view and no automatic truncation |
| `retry_test` | retry only while the text is retained; no automatic retry on failure |
| `memory_lifetime_test` | view text empty after dismiss; no retained text after pause |
| `shutdown_test` | shutdown during resolve, during streaming, and while paused all complete under the bounded wait |

Integration (offscreen): coordinator + fake backend + real provider client against a local stub server.

### Pause semantics (as implemented)

`setPaused(true)` calls `backend->unsubscribe()`, which for the AT-SPI backend means *stop processing*, not
tear down the subscription: at-spi2-core 2.44 does not serve a second subscription in the same process
(measured, `04-selection.md` §7.2), so deregistering would leave the product permanently deaf after a single
pause. The user-visible contract — nothing is read, cached or translated while paused, and resuming waits for
a new selection — is unchanged; the D-Bus event subscription itself stays alive. A full teardown remains open
work and would require isolating libatspi in a helper process.

## 14. Open questions

1. Whether a new selection should abort an in-flight provider request or let it finish and be replaced: current
   decision is abort (saves cost, matches §4.3 of the top-level design). Measure user-perceived flicker in M1.
2. `SelectionTimeout` currently shows nothing; if M0 shows frequent timeouts on common applications, a one-time
   inline hint is justified — decide with data.
3. Whether `enableAutomatic()` should verify a working provider (one reachability test) before subscribing, or
   stay action-only. Current decision: stay action-only; the first real selection reveals problems.

## 15. Change log

| Date | Change |
| --- | --- |
| 2026-11-16 | Initial module design; state machine, consent gate and generation policy specified. |
