# 06 — Translation Provider (`sniplyngo_provider`)

| | |
| --- | --- |
| Milestone | M1 (one OpenAI-compatible provider) |
| Status | **Designed** |
| Depends on | `01 core`, `02 settings` (value objects), `03 secrets` |
| Used by | 05 (single caller) |
| Document owner | maintainer |
| Implementation | **Done for the OpenAI-compatible client** — `src/provider/`, tests against a local stub server. The API key arrives through `ApiKeySource`; wiring it to module 03 is pending |

## Proxy (implemented)

Qt does not read the system proxy unless asked, so a machine behind a proxy failed to reach *every* engine —
the first real-world report was "all engines fail" on a network that works fine through a local proxy. Modes:

| `network.proxy` | Meaning |
| --- | --- |
| `system` (default) | `QNetworkProxyFactory::setUseSystemConfiguration(true)`: on Unix this reads `https_proxy`, `http_proxy`, `all_proxy`, `no_proxy` |
| `manual` | an explicit `http://`, `https://` or `socks5://` URL, with optional user info |
| `none` | connect directly |

Set it with `sniplyngo proxy manual http://127.0.0.1:7890` or in the settings window; `sniplyngo proxy` and the
settings window's "In use:" line report what is actually applied. Loopback providers bypass the proxy.
Connections use TLS 1.2 as requested for the observed proxy environment; servers requiring only TLS 1.3
will not connect with this configuration.

## Keyless engines (implemented)

SnipLingo must be usable without buying anything, so `SimpleTranslateProvider` speaks three one-shot HTTP
APIs that need no credential:

| `type` | Endpoint | Notes |
| --- | --- | --- |
| `mymemory` | `https://api.mymemory.translated.net/get` | Documented API with a free anonymous tier and a per-day limit; quota exhaustion can arrive inside an HTTP 200 and is reported as `ProviderRateLimited` |
| `libretranslate` | configurable, default `http://127.0.0.1:5000` | Requires a running instance; the optional API key is sent in the request body |
| `google-free` | `https://translate.googleapis.com/translate_a/single?client=gtx` | No key, **unofficial and undocumented**; may break or change without notice, and its use is not covered by any agreement |
| `bing-free` | `https://edge.microsoft.com/translate/auth` + `https://api-edge.cognitive.microsofttranslator.com/translate` | Legacy unofficial endpoint; the token URL returned HTTP 404 during the November 2026 check and is not offered for new GUI setups |

Properties of these engines:

- they never stream: one request, one answer, delivered as a single delta followed by completion;
- remote services require approval before automatic transmission; loopback services do not;
- MyMemory accepts at most 500 UTF-8 bytes per request;
- language tags from settings are converted per endpoint (`zh-Hans` → `zh-CN`, but `zh` for
  LibreTranslate), because none of them accept full BCP-47 tags;
- errors are mapped from the HTTP status, with MyMemory's in-body quota message as the one exception.

MyMemory requires an explicit source language. Its automatic mode guesses Chinese when the text contains a CJK
ideograph and English otherwise; select the source explicitly for other languages. Google, Bing and
LibreTranslate receive an automatic-detection request when the user chooses auto.

Local engines (Ollama and any other OpenAI-compatible server on loopback) remain the only way to keep text on
the device; that trade-off is stated in the settings UI next to the keyless presets.

## 1. Scope

Send one text to one configured endpoint and deliver the translation back, streaming where the endpoint supports
it, with explicit timeouts, cancellation and an error mapping the UI can rely on. M1 ships exactly one
implementation (OpenAI-compatible chat completions); the interface exists so a second one does not require
touching the coordinator.

## 2. Responsibilities

- Build a minimal, stable request for the configured endpoint and model.
- Stream deltas when enabled and the response is an event stream; deliver the whole text when it is not.
- Enforce connect, first-byte and total deadlines; abort on cancel.
- Map every failure to a `core::ErrorCode` with a non-sensitive detail.
- Read the API key at request time from `SecretStore` (never cache it).
- Refuse configurations that would expose a credential (plain `http` to a non-loopback host) or that require
  redirects to another origin.

## 3. Non-responsibilities

- No provider selection policy, no retry, no fallback to another endpoint, no cost accounting (`05` decides,
  the user decides).
- No prompt user interface, no history, no cache, no token counting for billing.
- No non-OpenAI wire formats in M1 (DeepL/LibreTranslate would be additional implementations, not options here).

## 4. Public interface

```cpp
// src/provider/provider.h
namespace provider {

struct TranslateRequest {
    quint64 generation = 0;
    QString text;              // already validated by 05 (non-empty, within limit)
    QString sourceLanguage;    // "auto" or BCP-47
    QString targetLanguage;    // BCP-47
    bool streaming = true;     // from ProviderConfig
};

struct Capabilities {
    bool streaming = true;
    bool usesNetwork = true;   // false for loopback endpoints
    int maxInputChars = 8000;  // documented limit for the prompt, not a hard server limit
};

class Sink {                   // implemented by 05; all calls on the UI thread
public:
    virtual ~Sink() = default;
    virtual void onDelta(quint64 generation, QStringView delta) = 0;
    virtual void onFinished(quint64 generation) = 0;
    virtual void onFailed(quint64 generation, core::Error error) = 0;
};

class Provider : public QObject {
    Q_OBJECT
public:
    virtual QString id() const = 0;                     // "openai-compatible"
    virtual Capabilities capabilities() const = 0;
    virtual void translate(const TranslateRequest &request, Sink &sink) = 0;
    virtual void cancel(quint64 generation) = 0;        // abort the reply for that generation, if in flight
};

// Factory keeps 05 free of construction details and gives tests a seam.
std::unique_ptr<Provider> createProvider(const settings::ProviderConfig &config,
                                         const settings::ProviderSettings &timeouts,
                                         secrets::SecretStore &secrets,
                                         QObject *parent);

} // namespace provider
```

Only one request may be in flight; `translate()` while a request is active is a programming error (asserted).
`05` guarantees it by construction.

## 5. Wire protocol (M1: `openai-compatible`)

Request:

```
POST {baseUrl}/chat/completions
Authorization: Bearer {secret}
Content-Type: application/json
Accept: text/event-stream | application/json
```

```json
{
  "model": "<config.model>",
  "temperature": 0,
  "stream": true,
  "messages": [
    { "role": "system", "content": "<PROMPT_VERSION 1 system text>" },
    { "role": "user", "content": "<selected text verbatim>" }
  ]
}
```

Rules:

- `baseUrl` is normalised (trailing slash removed) and must be `http`/`https`; plain `http` is accepted only for
  loopback hosts, otherwise the configuration is rejected with a validation message before any request.
- Automatic redirects are disabled (`QNetworkRequest::ManualRedirectPolicy`); any 3xx is `ProviderBadResponse`,
  because following it could send the credential to another origin.
- No extra parameters are sent: no `top_p`, no `frequency_penalty`, no user id, no metadata. Fewer knobs, fewer
  provider-specific behaviours to debug.
- Empty `model` is omitted from the body (some local servers reject an unknown model).
- The user's text is sent verbatim: no wrapping, no escaping beyond JSON, no truncation (05 has already
  refused over-limit text).

Response handling:

| Content type | Behaviour |
| --- | --- |
| `text/event-stream` | incremental SSE parsing, deltas forwarded, finish on `data: [DONE]` or end of body |
| `application/json` | parse `choices[0].message.content`, emit one delta then finish |
| anything else | `ProviderBadResponse` |

SSE parser rules: split on `\n`, tolerate `\r\n`, ignore `:` comment lines and unknown fields, take `data:`
payloads only, buffer across chunks, decode UTF-8 only after a complete line is assembled, ignore a final partial
line at end of body, treat `[DONE]` as completion. Empty accumulated text at completion ⇒ `ProviderBadResponse`
(a silent empty translation is a bug, not a result).

Response size cap: 64 KiB of accumulated text; exceeding it aborts with `ProviderBadResponse`.

## 6. Prompt

`PromptBuilder` (versioned, unit-tested, no provider-specific variants in M1):

```
You are a translation engine. Translate the user's text into {TargetLanguageName}.
Output only the translation. Preserve line breaks, numbers, identifiers, URLs and inline code
exactly. Do not add explanations, notes, transliterations or quotation marks.
If the text is already in {TargetLanguageName}, return it unchanged.
```

- `TargetLanguageName` comes from a small built-in BCP-47 → English name table
  (`zh-Hans` → "Simplified Chinese", `en` → "English", …). Unknown tags fall back to
  `the language identified by the BCP-47 tag <tag>`, and the UI shows the tag so the user can correct it.
- `sourceLanguage=auto` is expressed only in the system prompt (one sentence) and never as a guessed tag, because
  guessing here would duplicate provider-side detection.
- The prompt version is a constant (`kPromptVersion = 1`) and appears in debug logs as a number, so behaviour
  changes are attributable without logging any text.

## 7. Timeouts, cancellation, threading

| Deadline | Implementation | Result |
| --- | --- | --- |
| Connect + first byte | own `CancellableDelay` armed at send, disarmed on first received byte | `ProviderTimeout` |
| Total | own `CancellableDelay` armed at send, disarmed on finish, plus `QNetworkRequest::setTransferTimeout` for stalls | `ProviderTimeout` |
| User cancel / supersede | `reply->abort()` for that generation | `ProviderCancelled` (silent) |

Threading: `QNetworkAccessManager` on the UI thread; every callback is asynchronous. No worker thread, no blocking
read. `cancel(gen)` only aborts the matching reply; a late `finished` from an aborted reply is ignored.

`setTransferTimeout` is documented as an inactivity timeout, so it complements — it does not replace — the total
deadline above.

## 8. Secrets

`createProvider` stores the `apiKeyRef` only. At request time the provider calls
`secrets::SecretStore::lookup(apiKeyRef)`; a missing secret yields `SecretNotFound`, which 05 maps to
`ProviderNotConfigured` (the user must enter the key). The secret is placed in the `Authorization` header and
then cleared best-effort from the local copy. It never appears in logs, error details, or the settings document.

Endpoints that need no credential (local Ollama, llama.cpp) simply have an empty `apiKeyRef` and get no
`Authorization` header.

## 9. Error mapping

| Condition | Code | Detail (non-sensitive) |
| --- | --- | --- |
| No active provider / missing key | `ProviderNotConfigured` | `activeId` empty or secret absent |
| 401, 403 | `ProviderAuthFailed` | `HTTP 401` |
| 429 | `ProviderRateLimited` | `HTTP 429`, optional `Retry-After` seconds |
| 5xx | `ProviderUnavailable` | `HTTP 503` |
| DNS/TLS/reset/refused | `ProviderNetwork` | `QNetworkReply::errorString()` (contains no user text) |
| Deadline | `ProviderTimeout` | which deadline fired |
| Redirect, wrong content type, malformed JSON, empty content, oversize | `ProviderBadResponse` | the specific parser condition |
| Aborted by a newer request or pause | `ProviderCancelled` | — |

No automatic retry, no fallback endpoint, no silent downgrade from streaming to non-streaming. A non-streaming
response to a streaming request is *accepted* (some proxies buffer or ignore `stream`), which is a documented
capability of the wire format, not a fallback.

## 10. Configuration consumed

`ProviderConfig` (`id`, `baseUrl`, `model`, `apiKeyRef`, `streaming`, `markedLocal`) and
`ProviderSettings` timeouts. Nothing else; the provider never reads `settings.json`.

## 11. Dependencies and boundaries

Allowed: `01 core`, `02 settings` value types, `03 secrets`, Qt Core, Qt Network.
Forbidden: Qt Quick/Widgets, QtDBus, platform libraries, coordinator headers, any persistence.

## 12. Test plan

A local stub HTTP server (`QTcpServer`) in `tests/unit/provider/stub_server.h`; no live endpoints, ever.

| Test | Asserts |
| --- | --- |
| `request_shape_test` | path, headers, JSON body (model omitted when empty; no extra parameters; `temperature:0`) |
| `streaming_happy_test` | deltas in order, one `onFinished`, no duplicate text |
| `sse_chunk_edges_test` | split inside a line, `\r\n`, comments, `data: [DONE]`, trailing partial line, UTF-8 multibyte split across chunks |
| `non_streaming_test` | JSON response to a streaming request yields one delta + finish |
| `error_mapping_test` | 401/403/429/500/302/malformed/empty/oversize each map to the documented code and detail |
| `timeout_first_byte_test` | no bytes ⇒ `ProviderTimeout` within tolerance; reply aborted |
| `timeout_total_test` | slow trickle past total deadline ⇒ `ProviderTimeout` |
| `cancel_test` | `cancel(gen)` aborts; late completion ignored; no error surfaced |
| `secret_handling_test` | missing secret ⇒ `ProviderNotConfigured`; header present when configured; secret never in logs (captured sink) |
| `http_forbidden_test` | `http` + non-loopback host is rejected before any socket activity |
| `redirect_test` | 302 is not followed; credential not sent to a second host |
| `prompt_test` | target-language naming, code/URL preservation sentence, unknown tag fallback, version constant |

## 13. Open questions

1. Some OpenAI-compatible servers reject unknown fields or require `max_tokens`; if M1 testing shows a common
   server that needs one, add it as a *documented* per-config option rather than heuristically.
2. Whether the 64 KiB response cap is right for long essay translations; measure with real text before freezing.
3. Streaming false-positive: proxies that send `text/event-stream` but buffer. If it is common, show a passive
   "waiting" state rather than a spinner that implies progress.

## 14. Change log

| Date | Change |
| --- | --- |
| 2026-11-16 | Initial module design; OpenAI-compatible only, prompt v1, explicit deadlines. |
