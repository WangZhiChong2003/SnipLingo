# 01 — Core (`sniplyngo_core`)

| | |
| --- | --- |
| Milestone | M1 (types are needed by every other module) |
| Status | **Designed** |
| Implementation | **Done** — `src/core/`, `tests/core_*_test.cpp` (see §11) |
| Depends on | Qt Core only |
| Used by | all modules |
| Document owner | maintainer |

## 1. Scope

The smallest shared vocabulary the application needs: a fallible result type, one error taxonomy, a
generation counter, non-sensitive logging, XDG paths and a cancellable delay. It contains no product
behaviour. If a helper here is only used by one module, it belongs in that module instead.

## 2. Responsibilities

- Define `Result<T>` and `Error`/`ErrorCode`, the single failure vocabulary of the application.
- Define `Generation`, the staleness token shared by all flows.
- Provide logging that makes it *impossible* to log selected text, recognized text or credentials.
- Resolve XDG base paths once, with a test override.
- Provide a cancellable, single-shot delay used for the selection stability delay and timeouts.
- Provide a monotonic clock wrapper so tests can advance time deterministically.

## 3. Non-responsibilities

- No settings fields, no JSON (module 02), no secrets (03), no selection/provider logic (04–06).
- No user-visible strings. `core` emits codes; the UI (07) owns wording.
- No global mutable state other than the process-wide log sink and paths (both settable once at startup).

## 4. Public interface

```cpp
// src/core/error.h
namespace core {

enum class ErrorCode {
    // --- selection acquisition -------------------------------------------------
    SelectionBackendAbsent,      // no backend for this session at all
    SelectionBackendDenied,      // protocol present, access refused (sandbox/policy)
    SelectionBackendFailed,      // initialisation or subscription failed
    SelectionEmpty,              // nothing selected / owner returned no text
    SelectionNotText,            // selection is an image, file list, etc.
    SelectionProtected,          // password/protected text object
    SelectionTooLarge,           // exceeds configured character limit
    SelectionTimeout,            // owner did not deliver text before deadline
    SelectionCancelled,          // superseded before completion (not an error to show)
    // --- consent and mode ------------------------------------------------------
    AutomaticDisabled,           // automatic mode off
    AutomaticPaused,             // observation stopped by the user
    ConsentRequired,             // remote auto-send not yet confirmed
    ConsentStale,                // destination changed since consent
    // --- provider --------------------------------------------------------------
    ProviderNotConfigured,
    ProviderAuthFailed,
    ProviderRateLimited,
    ProviderUnavailable,         // 5xx / connection refused
    ProviderNetwork,             // DNS, TLS, reset
    ProviderTimeout,
    ProviderBadResponse,         // unparsable or empty content
    ProviderCancelled,
    // --- configuration, secrets, storage --------------------------------------
    SettingsCorrupt,
    SettingsInvalid,
    SecretStoreUnavailable,
    SecretNotFound,
    // --- capture / OCR (M3, declared now so the taxonomy does not churn) ------
    CaptureDenied,
    CaptureUnavailable,
    OcrUnavailable,
    OcrEmpty,
    // --- generic ---------------------------------------------------------------
    Cancelled,
    Internal
};

struct Error {
    ErrorCode code{ErrorCode::Internal};
    // Diagnostic detail. MUST NOT contain selected text, recognized text,
    // credentials, window titles or file paths of captured images.
    QString detail;
};

// Category predicates used by the coordinator and UI; keep the policy in one place.
constexpr bool isSilent(ErrorCode) noexcept;   // no window, no dialog (superseded/empty/disabled)
constexpr bool isRetryable(ErrorCode) noexcept;// a manual Retry action makes sense
const char *stableName(ErrorCode) noexcept;    // for logs/doctor JSON, never localized

} // namespace core
```

```cpp
// src/core/result.h
template <class T>
class Result {
public:
    Result(T value);                 // implicit success
    Result(Error error);             // implicit failure

    bool ok() const noexcept;
    explicit operator bool() const noexcept;
    T &value();                      // precondition: ok(); asserts otherwise
    const T &value() const;
    const Error &error() const;      // precondition: !ok()
    T valueOr(T fallback) const;
};

template <>
class Result<void> { /* ok()/error() only */ };

// Convenience for the common "return early with the error" pattern.
#define SNIPLYNGO_TRY(tmp, expr) /* expands to a checked assignment */ // NOLINT
```

`Result` is intentionally minimal: no monadic combinators, no exception interop. Modules compose with
explicit `if (!r.ok())` checks; the code path stays visible in review.

```cpp
// src/core/generation.h
using Generation = quint64;          // 0 is "no generation"

class GenerationSource {             // single owner: the coordinator (module 05)
public:
    Generation next() noexcept;      // pre-increment; never returns 0
    Generation current() const noexcept;
    bool isCurrent(Generation g) const noexcept;
    void invalidate() noexcept;      // current() becomes stale without issuing a new one
};
```

```cpp
// src/core/log.h
namespace core::log {

enum class Level { Debug, Info, Warning, Error };

// There is deliberately no overload taking user text. Callers pass counts and codes.
void init(Level minimum, const QString &optionalLogFile = {});
void write(Level, std::string_view module, const QString &message);
void writeError(std::string_view module, const core::Error &error);   // logs code + detail

} // namespace core::log
```

Implementation notes:

- Backed by `QLoggingCategory` so `QT_LOGGING_RULES` works; categories are `sniplyngo.<module>`.
- Default sink is stderr (journald under the packaged unit). An optional file sink rotates at 1 MiB × 3 and
  is off by default.
- `message` is a plain `QString` that callers construct from numbers, enums and backend identifiers. The API
  shape is the guard: there is no `logText(QString)` and review rejects interpolating selection content.

```cpp
// src/core/paths.h
class XdgPaths {
public:
    static XdgPaths resolve();                       // honours XDG_* and HOME
    static void overrideForTests(const XdgPaths &);   // test seam, called once

    QString configDir() const;   // $XDG_CONFIG_HOME/sniplyngo
    QString dataDir() const;     // $XDG_DATA_HOME/sniplyngo
    QString stateDir() const;    // $XDG_STATE_HOME/sniplyngo
    QString cacheDir() const;    // $XDG_CACHE_HOME/sniplyngo
    QString settingsFile() const;// configDir() + "/settings.json"
};
```

```cpp
// src/core/delay.h
class CancellableDelay : public QObject {
    Q_OBJECT
public:
    explicit CancellableDelay(QObject *parent = nullptr);
    // Restarting replaces the pending callback; the previous one is never invoked.
    void start(int milliseconds, std::function<void()> onElapsed);
    void cancel();
    bool isActive() const noexcept;
    int remainingMs() const noexcept;
};
```

`CancellableDelay` wraps a `QTimer` with a `QPointer`-checked callback so a callback cannot run against a
destroyed receiver. It is the only timing primitive modules use; nobody calls `QTimer::singleShot` directly
(one greppable rule, one place to inject a test clock).

## 5. Data types

| Type | Mutability | Notes |
| --- | --- | --- |
| `Error` | immutable value | copyable; `detail` is diagnostic only |
| `Result<T>` | move-only when `T` is expensive | never holds both a value and an error |
| `Generation` | value | monotonic within a process; not persisted |
| `XdgPaths` | immutable after resolve | overridable once for tests |

## 6. Lifecycle and state

`core` has no runtime state machine. Startup order is fixed and enforced by the executable:

1. `XdgPaths::resolve()`
2. `log::init()` (level from `SNIPLYNGO_LOG`, default `Info`)
3. settings load (module 02) — may report `SettingsCorrupt`
4. everything else

Shutdown has no `core` work.

## 7. Concurrency, threading and lifetime

- Every type here is safe to copy across threads; `Result` and `Error` are values.
- `GenerationSource` is owned and used by the coordinator's thread only. Other threads receive the value.
- `log::write` is thread-safe (`qCDebug` family is). Message construction happens on the caller's thread.
- `CancellableDelay` is not thread-safe; it lives on the thread that uses it (UI thread).

## 8. Error handling rules

1. `detail` never contains user text or credentials. Backends report *what* failed (owner gone, no TEXT
   target, HTTP 401), not *what* was selected.
2. No module invents a new error string the UI must parse. The UI switches on `ErrorCode`.
3. `SelectionCancelled` and `ProviderCancelled` are not user-visible failures; `isSilent()` returns true.
4. Adding an `ErrorCode` requires updating `isSilent`, `isRetryable`, `stableName` and the UI's switch in the
   same change; the UI switch is exhaustive (compiler-checked).

## 9. Configuration

| Setting | Source | Default |
| --- | --- | --- |
| Log level | `SNIPLYNGO_LOG=debug/info/warning/error` | `info` |
| Optional log file | `SNIPLYNGO_LOG_FILE` | unset (stderr only) |
| Base directories | `XDG_*` environment | XDG specification defaults |

Nothing else in `core` reads the environment or settings.

## 10. Dependencies

Allowed: Qt Core, the C++ standard library, POSIX (`getpid`, `clock_gettime` via `QElapsedTimer`).
Forbidden: Qt Network/DBus/Quick, libsecret, xcb, wayland-client, any platform header.

## 11. Test plan

Unit (`tests/unit/core/`):

| Test | Asserts |
| --- | --- |
| `result_test` | value/error exclusivity, `valueOr`, move-only payloads, assert-on-misuse in debug builds |
| `error_policy_test` | every `ErrorCode` has a `stableName`, exactly the intended ones are silent/retryable (table-driven; adding a code without policy fails the test) |
| `generation_test` | monotonic, never 0, `invalidate()` makes older tokens stale, wrap-around is not reachable in practice |
| `delay_test` | restart replaces the callback, `cancel()` prevents it, destruction drops it, `remainingMs()` monotonic |
| `paths_test` | XDG overrides, missing `HOME` fallback, test override applied once |
| `log_test` | level filtering, category naming, file sink rotation, no overload accepts user text (compile-time via a deleted template) |

No manual/desktop test applies to this module.

## 12. Open questions

None. If a decision here changes, it changes the vocabulary of every module — treat it as an ADR-level
change.

## 13. Change log

| Date | Change |
| --- | --- |
| 2026-11-16 | Initial module design. |
