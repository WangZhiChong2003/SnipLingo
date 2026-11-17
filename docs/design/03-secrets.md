# 03 — Secret Storage (`sniplyngo_secrets`)

| | |
| --- | --- |
| Milestone | M1 (libsecret path), M2 (sandbox path) |
| Status | **Prototype-dependent** — the libsecret path is specified and low-risk; the sandboxed path must be measured on a real Flatpak install |
| Depends on | `01 core`, Qt Core, libsecret (implementation only) |
| Used by | 06 (provider HTTP auth), 07 (credential entry UI) |
| Document owner | maintainer |
| Implementation | **Interface + session-only store done** — `src/secrets/`, `tests/secrets_test.cpp`; the persistent backends (§5.1, §5.3) are not written, and `createSecretStore()` reports them as such instead of pretending |

## 1. Scope

Store and retrieve provider API keys using operating-system secret storage. The module hides *where* the
secret lives; callers only ever see a key identifier and a lookup result.

## 2. Responsibilities

- Report what storage is actually available (`Available`, `SessionOnly`, `Unavailable`) before the UI asks
  for a credential, so the UI can state the consequence before the user types.
- Store, look up and delete a secret by key id.
- Never write a secret to a file, a log, an error message or the settings document.
- Keep the lifetime of a plaintext secret in process memory as short as the API allows.

## 3. Non-responsibilities

- No consent logic (02), no HTTP (06), no UI (07), no encryption design of its own.
- No plaintext fallback file. If no secure store works, the only fallback is a session-only in-memory store,
  and the user is told exactly that.

## 4. Public interface

```cpp
// src/secrets/secret_store.h
namespace secrets {

enum class Availability {
    Available,     // persistent OS-backed storage works
    SessionOnly,   // no persistent store; secrets live until the process exits
    Unavailable    // nothing works, not even session storage (never expected)
};

struct AvailabilityReport {
    Availability availability;
    QString backend;       // "libsecret", "secret-portal", "session", "none"
    QString reason;        // non-sensitive, e.g. "org.freedesktop.secrets not on the session bus"
};

class SecretStore {                       // pure interface; no QObject
public:
    virtual ~SecretStore() = default;
    virtual AvailabilityReport availability() const = 0;

    // Returns SecretNotFound when absent. Timeouts are the backend's responsibility.
    virtual core::Result<QString> lookup(const QString &key) = 0;
    virtual core::Result<void> store(const QString &key, const QString &secret) = 0;
    virtual core::Result<void> remove(const QString &key) = 0;
};

// Factory: probes backends in order, returns the first usable one.
std::unique_ptr<SecretStore> createSecretStore(
    const std::function<void(std::function<void()>)> &runOnWorker);   // see §7

} // namespace secrets
```

Key naming (owned by module 02's `apiKeyRef`, produced by the UI):

```
provider/<provider-id>/apiKey        e.g. provider/openai-main/apiKey
```

Keys are stable identifiers, not secrets; they may appear in logs and diagnostics.

## 5. Backends

### 5.1 `LibsecretStore` (M1 target)

- Secret Service item schema: `io.github.wangzhichong2003.SnipLingo`, attributes
  `{ "application": "SnipLingo", "key": "<key id>" }`, label = key id.
- Uses `secret_password_store_sync`, `secret_password_lookup_sync`, `secret_password_clear_sync` with a
  `SecretSchema` and `SECRET_COLLECTION_DEFAULT`.
- Availability probe: `secret_service_get_sync(SECRET_SERVICE_NONE, nullptr, &error)` plus a trivial
  round-trip on a canary key. Probe runs once at startup (worker thread) and is cached for the process.
- Failure modes and their meaning:

| libsecret result | Meaning | Module result |
| --- | --- | --- |
| service present, item found | normal | success |
| service present, item absent | user never stored a key, or keyring was reset | `SecretNotFound` |
| `SECRET_ERROR_NO_SUCH_OBJECT` / no session bus | no secret service in this session | `SessionOnly` + reason |
| service locked and no prompt available | user dismissed / headless | `SecretStoreUnavailable` for that operation; automatic mode may continue with a session credential the user enters now |

### 5.2 `SessionOnlyStore` (always available)

- `QHash<QString, QString>` in process memory; cleared on exit and on explicit "forget".
- Used when the OS store is unavailable, or when the user chooses "use for this session" in the UI.
- The UI must show a persistent notice while it is in use: the key will be gone after restart.

### 5.2a Secret Service client — implemented, and what it took

`secrets::SecretServiceStore` talks to `org.freedesktop.secrets` over QtDBus: `OpenSession("plain")`,
`SearchItems`, `Unlock`, `CreateItem` with `replace=true`, `Item.GetSecret`, `Item.Delete`, `Session.Close`.
No new dependency, no plaintext fallback; a keyring that needs an interactive prompt yields
`SecretStoreUnavailable` with a reason instead of blocking.

Getting it right took three specific fixes, each now covered by a test:

1. **Attributes must be `a{ss}`.** Passing a `QVariantMap` (`QMap<QString, QVariant>`) marshals as `a{sv}`
   and a real keyring rejects the call. The client now uses `QMap<QString, QString>`.
2. **That map type needs registration.** QtDBus has no built-in marshaller for it; without
   `qDBusRegisterMetaType<QMap<QString, QString>>()` the send aborts with
   `type ... is not registered with D-Bus`.
3. **Reply arguments can arrive wrapped in `QDBusArgument`.** `QVariant::toList()` / `.value<T>()` then
   silently produce empty values, which reads as "no item found" rather than as an error. All reply parsing
   goes through one type-aware helper.

`tests/secrets_service_live_test.cpp` drives a fake keyring over a private bus and asserts the *declared
signatures* (`a{ss}` for `SearchItems`, `a{sv}(oayays)b` for `CreateItem`), the store/lookup/replace/remove
round trip, `SecretNotFound`, prompted writes and a locked collection.

An earlier attempt was withdrawn after I concluded "QtDBus marshalling is unreliable". That conclusion was
wrong: my harness decoded virtual-object arguments with `QVariant::value<T>()` and printed a signature from
`.constData()` of a temporary `QLatin1String`. The client was mostly fine; the test was not. Recorded here so
the mistake is not repeated: verify the harness before doubting the transport.

**Still unverified:** a real desktop keyring (GNOME Keyring / KWallet) — none is reachable from the sandbox
this was developed in, and no automated test can stand in for it. `sniplyngo diag` reports which backend was
selected, and the settings window shows the reason when only session storage is in use.

First-use noise: QtDBus derives a custom struct's signature by marshalling a default-constructed value, so a
one-off `QDBusObjectPath: invalid path` warning may appear. It is Qt's probe, not a failed conversion.

### 5.3 `SecretPortalStore` (M2, not designed yet)

The Flatpak/sandbox path (`org.freedesktop.portal.Secret`) provides a per-application master secret rather
than direct keyring entries. Using it requires an encrypted local store, which is real design work (cipher
choice, nonce handling, file permissions, migration). It is **not** part of M1. The open questions below
record what must be answered before it is scheduled.

## 6. Lifecycle and state

```
startup:  probe()  ─► Available    ─► LibsecretStore
                   ├─► SessionOnly  ─► SessionOnlyStore  (+ UI notice)
                   └─► Unavailable  ─► SessionOnlyStore  (+ UI error banner)

runtime:  store/lookup/remove per operation; no caching of secrets in this module
shutdown: SessionOnlyStore::clear(); no other cleanup
```

Secrets are **not** cached across operations: a lookup returns a fresh copy that the caller is expected to
clear promptly. Provider requests read the secret at request time, so revoking it in the UI takes effect on
the next request.

## 7. Concurrency, threading and lifetime

- libsecret's `*_sync` API blocks on D-Bus and must not run on the UI thread. The factory receives a
  `runOnWorker` invoker; the concrete store executes each call on a shared worker (a dedicated `QThread`
  owned by the application, provided by module 05's infrastructure, not spawned per call).
- `SecretStore` methods are therefore called from the UI thread but complete asynchronously; the
  `std::unique_ptr<SecretStore>` returned by the factory is UI-thread-owned and internally posts to the
  worker. The concrete stores must not touch Qt objects owned by the UI thread from the worker.
- One worker for all secret operations, serialized. No parallel keyring prompts.

Memory hygiene, stated honestly: `QString`/`std::string` may leave copies in the allocator; the module
minimises copies, avoids storing secrets as members, and clears buffers best-effort (`QString::fill(QChar(0))`
before release). It does **not** claim zeroization guarantees; that limitation is documented rather than
hidden behind a custom allocator.

## 8. Error handling

| Situation | Return | UI consequence (07) |
| --- | --- | --- |
| No persistent store | `availability()==SessionOnly` | Notice in provider settings and onboarding before entry |
| Key absent | `SecretNotFound` | "Enter your key" prompt; automatic mode may still be enabled for a local provider |
| Store rejected the write | `ErrorCode::SecretStoreUnavailable` + non-sensitive reason | Offer session-only retry and explain the difference |
| User cancels a keyring prompt | `SecretStoreUnavailable` | No error dialog storm; one inline message with a Retry button |

No code path logs a secret. `writeError` for this module only logs the code, backend name and key id.

## 9. Configuration

| Value | Source |
| --- | --- |
| Backend selection | probing only; no user setting in M1 |
| Key ids | `ProviderConfig::apiKeyRef` (module 02) |
| Session-only opt-in | transient UI choice, not persisted |

## 10. Dependencies and boundaries

Allowed: `01 core`, Qt Core, libsecret (in `LibsecretStore` only).
Forbidden: Qt Network/Quick, xcb, wayland-client, any settings field access beyond a key id.

## 11. Test plan

| Test | Kind | Asserts |
| --- | --- | --- |
| `session_store_test` | unit | store/lookup/remove, clear on destruction, absent key ⇒ `SecretNotFound` |
| `key_naming_test` | unit | key format validation rejects ids that could collide with another provider |
| `availability_probe_test` | unit (fake service) | probe maps each backend condition to the documented availability |
| `no_leak_test` | unit | `store()`/`lookup()`/failure paths never emit the secret to the log sink (captured sink assertion) |
| `libsecret_roundtrip_test` | contract (opt-in, real keyring) | store → lookup → remove against a throwaway key; skipped with a message when no session bus |
| manual | desktop | Flatpak and native: probe result, prompt behaviour, reboot behaviour, revoked key |

## 12. Open questions

1. **Sandbox path (M2):** does `org.freedesktop.portal.Secret` work in the chosen Flatpak runtime, and is a
   portal-backed encrypted file acceptable versus requiring `org.freedesktop.secrets` access? Measure before
   designing §5.3.
2. **Probe cost:** the availability probe runs at startup; confirm it does not delay first paint beyond the
   startup budget, otherwise defer it until the first provider request or the first settings visit.
3. **Session-only semantics for automatic mode:** should automatic *remote* sending be allowed at all with a
   session-only key (key gone after restart, consent record still on disk)? Current decision: yes, but the UI
   states it; revisit if this proves confusing in M1 testing.

## 13. Change log

| Date | Change |
| --- | --- |
| 2026-11-16 | Initial module design; libsecret for M1, sandbox path deferred. |
