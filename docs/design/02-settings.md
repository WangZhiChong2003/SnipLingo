# 02 — Settings and Consent (`sniplyngo_settings`)

| | |
| --- | --- |
| Milestone | M1 |
| Status | **Designed** |
| Implementation | **Done** — `src/settings/`, `tests/settings_*_test.cpp` (see §13) |
| Depends on | `01 core` (Result, Error, paths, log), Qt Core |
| Used by | 03 (secret key names), 04 (limits, backend choice), 05 (mode, consent), 06 (provider config), 07 (all UI state) |
| Document owner | maintainer |

## 1. Scope

Own the persisted preference document: one JSON file, one schema version, one place that validates it, and
the consent record that authorises automatic transmission to a remote endpoint. No other module reads or
writes that file.

## 2. Responsibilities

- Load, validate, default, migrate and atomically persist `settings.json`.
- Expose immutable value objects to the rest of the application (`Settings` and its sub-structs).
- Preserve unknown keys written by a newer version on re-save, so a downgrade does not destroy them.
- Hold the consent record and answer `mayAutoSend(provider)` — the *fact* of consent lives here; the
  *decision to act on it* belongs to module 05.
- Emit one change signal with the complete new value; never partial mutations.
- Report corruption explicitly (`SettingsCorrupt`) after preserving the unreadable file for diagnosis.

## 3. Non-responsibilities

- No credential values (module 03); only `apiKeyRef` identifiers.
- No decision to start or stop observation (05), no backend probing (04), no wording (07).
- No history, no cache, no telemetry toggle (none exist; adding one is an ADR-level change).

## 4. Public interface

```cpp
// src/settings/settings.h
namespace settings {

struct ProviderConfig {
    QString id;                  // "openai-main"; [a-z0-9-]{1,32}
    QString type = "openai-compatible";
    QString label;               // user-facing, may be empty
    QString baseUrl;             // http(s) only, no query/fragment
    QString model;               // may be empty for servers that ignore it
    QString apiKeyRef;           // key id in the secret store; never the secret
    bool streaming = true;
    bool markedLocal = false;    // user asserts the endpoint never leaves the machine

    bool isLoopback() const;     // host is 127.0.0.0/8, ::1 or "localhost"
    bool isLocal() const;        // isLoopback() || markedLocal
    QString destinationLabel() const;   // "api.example.com · gpt-4o-mini"
    QString fingerprint() const;        // sha256 of normalised type|host|port|path|model
};

struct SelectionSettings {
    bool automaticEnabled = false;   // explicit onboarding choice; false until then
    bool paused = false;             // user pause; persisted so a restart stays paused
    bool showResultImmediately = false; // false: selection icon expands on hover
    QString backend = "auto";        // "auto" | "atspi" | "x11-primary" | "wlr-data-control"
    int stabilityDelayMs = 350;      // hypothesis; measured in M0/M1
    int maxSelectionChars = 4000;
    int resolveTimeoutMs = 1500;
};

struct LanguageSettings {
    QString source = "auto";         // BCP-47 or "auto"
    QString target;                  // BCP-47, non-empty in a usable configuration
};

struct ProviderSettings {
    QList<ProviderConfig> providers; // one active in M1, list keeps migration cheap
    QString activeId;                // empty means "not configured"
    int firstByteTimeoutMs = 12000;   // covers connect + time to first response byte
    int totalTimeoutMs = 30000;
};

struct ConsentRecord {
    QString fingerprint;             // ProviderConfig::fingerprint() at the time of consent
    QString destinationLabel;        // for the UI: "api.example.com · gpt-4o-mini"
    QString grantedAt;               // ISO-8601 UTC; informational only
    bool valid() const;              // non-empty fingerprint
};

struct UiSettings {
    QString theme = "system";        // "system" | "light" | "dark" | "high-contrast"
    qreal fontScale = 1.0;           // 0.75 .. 1.75
    bool reduceMotion = false;
};

struct Settings {
    int version = 1;
    SelectionSettings selection;
    ProviderSettings provider;
    LanguageSettings language;
    ConsentRecord consent;
    UiSettings ui;

    const ProviderConfig *activeProvider() const;
    // Consent is only required for automatic sending to a non-local destination.
    bool mayAutoSend() const;        // automaticEnabled && !paused && (local || consent matches)
};

enum class Outcome {
    Loaded,            // file read and validated
    LoadedWithDefaults,// missing file or missing keys
    Recovered,         // corrupt file, defaults used, original preserved
};

struct LoadResult {
    Outcome outcome;
    Settings settings;
    QStringList issues;   // non-sensitive validation notes (field names, not values)
};

} // namespace settings
```

```cpp
// src/settings/settings_store.h
class SettingsStore : public QObject {
    Q_OBJECT
public:
    explicit SettingsStore(core::XdgPaths paths, QObject *parent = nullptr);

    LoadResult load();                          // idempotent; sets current()
    core::Result<void> save(const Settings &next);    // validate → write atomically → emit changed
    const Settings &current() const;
    QStringList unknownKeys() const;            // preserved verbatim on save

    // Field-level helpers used by UI controllers; each validates then saves.
    core::Result<void> setAutomaticEnabled(bool);
    core::Result<void> setPaused(bool);
    core::Result<void> setTargetLanguage(const QString &tag);
    core::Result<void> setActiveProvider(QString id);
    core::Result<void> grantConsent(const ProviderConfig &);   // records fingerprint + label
    core::Result<void> revokeConsent();

signals:
    void changed(const settings::Settings &next);
    void loadIssues(const QStringList &issues);           // surfaced once in the UI
};
```

## 5. Schema (v1)

```json
{
  "version": 1,
  "selection": {
    "automaticEnabled": false,
    "paused": false,
    "showResultImmediately": false,
    "backend": "auto",
    "stabilityDelayMs": 350,
    "maxSelectionChars": 4000,
    "resolveTimeoutMs": 1500
  },
  "language": { "source": "auto", "target": "zh-Hans" },
  "provider": {
    "activeId": "openai-main",
    "firstByteTimeoutMs": 12000,
    "totalTimeoutMs": 30000,
    "providers": [
      {
        "id": "openai-main",
        "type": "openai-compatible",
        "label": "OpenAI",
        "baseUrl": "https://api.openai.com/v1",
        "model": "gpt-4o-mini",
        "apiKeyRef": "provider/openai-main/apiKey",
        "streaming": true,
        "markedLocal": false
      }
    ]
  },
  "consent": { "fingerprint": "", "destinationLabel": "", "grantedAt": "" },
  "ui": { "theme": "system", "fontScale": 1.0, "reduceMotion": false }
}
```

Storage rules:

- Directory `$XDG_CONFIG_HOME/sniplyngo` created with mode 0700; file written with mode 0600.
- Write path: validate → `QSaveFile` (write, `commit()`), then `QFile::setPermissions(0600)`. A crash leaves
  either the old or the new file, never a truncated one.
- No partial writes from UI code: every mutation goes through a `set*` helper that writes the whole document.
- Unknown top-level and nested keys are captured at load and re-emitted on save (forward compatibility).
- The file never contains credentials, selected text, recognized text or window titles.

## 6. Validation rules

| Field | Rule | On violation |
| --- | --- | --- |
| `version` | `1` in this build | unknown version ⇒ `SettingsCorrupt` path (recover, preserve) |
| `selection.backend` | one of the four literals | reset to `auto`, note the field |
| `stabilityDelayMs` | 100–2000 | clamp, note the field |
| `maxSelectionChars` | 200–20000 | clamp, note the field |
| `resolveTimeoutMs` | 200–5000 | clamp, note the field |
| `language.target` | non-empty BCP-47-ish (`^[A-Za-z]{2,8}(-[A-Za-z0-9]{1,8})*$`) or empty | empty ⇒ UI blocks automatic mode with `NoTargetLanguage`-style guidance (07) |
| `provider.baseUrl` | absolute `http`/`https`, no query, no fragment | provider entry disabled, note the field |
| `provider.providers[].id` | unique, `^[a-z0-9-]{1,32}$`, used in secret keys | invalid entry dropped, note the field |
| `provider.timeouts` | firstByte 1–120 s, total ≥ firstByte and ≤ 300 s | clamp |
| `ui.fontScale` | 0.75–1.75 | clamp |
| `consent.fingerprint` | hex sha256 or empty | invalid ⇒ treated as **no consent** |

Clamping and defaulting are recorded in `issues` so the UI can say "some values were reset" without
exposing user text. Validation never refuses to load the file; it refuses to *use* invalid provider entries.

## 7. Consent model

Consent is bound to a destination, not to the application:

```
mayAutoSend() = selection.automaticEnabled
             && !selection.paused
             && (activeProvider()->isLocal() || consent.fingerprint == activeProvider()->fingerprint())
```

1. `fingerprint()` normalises: lowercase scheme+host, strip default ports, strip trailing slash, keep path
   and model. Changing host, path, model or type produces a different fingerprint.
2. `grantConsent()` is only called from the UI after the user has seen `destinationLabel()` and confirmed.
   The store does not decide; it records.
3. If the fingerprint no longer matches the active provider, `mayAutoSend()` is false and module 05 surfaces
   `ConsentStale` — automatic transmission stops until reconfirmed. Manual paste translation is unaffected.
4. `revokeConsent()` clears the record; automatic remote sending stops immediately.
5. A local provider (`isLoopback()` or `markedLocal`) does not require the consent record, because no egress
   occurs. The UI still shows the endpoint so the user can verify the claim; the application cannot verify a
   `markedLocal` assertion and says so.

## 8. Lifecycle

```
load() ──► Loaded | LoadedWithDefaults | Recovered ──► current()
   │                                    │
   │                                    └─ preserves settings.json.corrupt-<utc-stamp>
   └─ defaults: version=1, automaticEnabled=false, paused=false, backend=auto, no provider
```

- First run: no file ⇒ `LoadedWithDefaults`, `automaticEnabled=false` (the app starts paused-by-policy, per
  ADR 0005 and `../ui-ux-spec.md` §1.1). The onboarding flow writes the file.
- Subsequent runs: `automaticEnabled` and `paused` are honoured as stored, so a restart is predictable.
- `Recovered`: the unreadable file is copied to `settings.json.corrupt-<stamp>` (no user text is in it),
  defaults are used, and the UI shows one non-blocking notice with the file name.

## 9. Concurrency, threading, lifetime

- Lives on the UI thread; the file I/O is small and synchronous by design (kilobytes, once at startup and on
  explicit changes). No watcher, no polling, no external modification detection in M1.
- `changed` is emitted synchronously after a successful save; receivers copy what they need.
- One `SettingsStore` instance, owned by the application object.

## 10. Error handling

| Situation | Result |
| --- | --- |
| File missing | `Outcome::LoadedWithDefaults`, no error |
| File unreadable/invalid JSON | `Outcome::Recovered`, defaults, preserved copy, `loadIssues` |
| Field invalid | clamp/drop + `issues` entry (field name only) |
| Save fails (permissions, full disk) | `Result` with `ErrorCode::Internal` and a filesystem-level detail; UI shows a persistent banner, because silently losing a consent change is unacceptable |
| `activeId` points to a missing entry | treated as "provider not configured"; 05 raises `ProviderNotConfigured` when needed |

## 11. Configuration surface owned here

Everything in the schema above. Module 04 consumes `SelectionSettings`; 06 consumes `ProviderSettings`;
07 consumes all of it. No module reads `settings.json` directly, and no module caches a copy longer than one
request.

## 12. Dependencies and boundaries

Allowed: `01 core`, Qt Core (`QJsonDocument`, `QSaveFile`, `QFileInfo`, `QCryptographicHash`, `QUrl`,
`QLocale`).
Forbidden: Qt Network/Quick, libsecret, platform libraries, any selection or provider implementation.

The locale-derived default target language is a *suggestion* produced here
(`QLocale::system()` → `zh-Hans`, `en`, …) and left empty when no mapping exists; the UI asks the user
instead of guessing.

## 13. Test plan

Unit (`tests/unit/settings/`):

| Test | Asserts |
| --- | --- |
| `schema_roundtrip_test` | load → save → load produces an identical document |
| `defaults_test` | missing file yields the documented defaults, `automaticEnabled=false` |
| `corrupt_recovery_test` | truncated/invalid JSON ⇒ `Recovered`, preserved copy, defaults usable |
| `unknown_keys_test` | keys added by a "newer" document survive a save cycle |
| `validation_clamp_test` | each bounded field clamps to the documented range and reports the field name |
| `validation_reject_test` | bad `baseUrl`, duplicate ids, bad version follow the documented recovery |
| `consent_test` | fingerprint stability across cosmetic URL changes; invalidation on host/model change; `mayAutoSend()` truth table (enabled/paused/local/remote × consent present/stale/absent) |
| `atomic_write_test` | simulated failure before `commit()` leaves the previous file intact; permissions are 0600 |
| `no_secret_leak_test` | a document containing a `"apiKey"` field never surfaces the value in `issues`, logs or a re-saved file |

## 14. Open questions

1. `stabilityDelayMs=350` is a hypothesis; M0 measures drag-end behaviour per backend and may replace it with
   a per-backend default. The field stays, the default is data.
2. Whether `paused` should survive an application restart is a product question. Current decision: it
   persists (predictable for privacy-sensitive users); revisit if it causes "why is nothing happening"
   confusion during M1 usability checks.

## 15. Change log

| Date | Change |
| --- | --- |
| 2026-11-16 | Initial module design; consent bound to destination fingerprint. |
