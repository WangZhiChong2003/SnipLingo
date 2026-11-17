# ADR 0006 — M0 outcome: AT-SPI for Wayland, XFixes PRIMARY for X11, coverage reported per application

- **Status:** Accepted (freezes the M1 backend choice)
- **Date:** 2026-11-16
- **Deciders:** Project maintainer
- **Related:** [ADR 0005](0005-automatic-selection.md), [`../design/04-selection.md`](../design/04-selection.md),
  [`../../prototypes/m0-selection-probe/RESULTS.md`](../../prototypes/m0-selection-probe/RESULTS.md)

## Context

ADR 0005 made automatic selection observation the primary workflow but left the backend undecided until a
real desktop could be measured. Seven manual probe rounds and one unattended run produced the following
evidence on GNOME 42 / mutter, Wayland session with XWayland (Ubuntu 22.04):

| Question | Measurement |
| --- | --- |
| Does XFixes `PRIMARY` notification + conversion work? | **Yes**, 5/5 self-test iterations resolved the expected text, no X errors. Every iteration needed the `CurrentTime` retry: the conversion with the XFixes event timestamp stalled for the full 1500 ms deadline. |
| Does AT-SPI deliver events and text? | **Yes** against a controlled GTK provider: 11/11 events, text matched the selected ranges exactly, `GetRangeText` resolve time p50 1.3 ms, `GetRangeExtents` returned usable screen coordinates. |
| Coverage of real applications | VTE terminal: events arrive but ranges are zero-length in 3 of 4 attempts (one attempt resolved correctly, i.e. timing-dependent). Chromium-family and Electron apps (Chrome, VS Code, Codex, QQ): text objects present, **no** text events at all, only focus/name events, with `toolkit-accessibility` either `false` or `true`. |
| Portals | `GlobalShortcuts` **not exported**; `Screenshot` v2 (no area target); `ScreenCast` 4; `Secret` 1. |
| Desktop accessibility status | `org.a11y.Status` not exposed by this GNOME; `toolkit-accessibility` is the authoritative switch and defaults to `false`. |

## Decision

1. **Wayland: AT-SPI** (`object:text-selection-changed`, libatspi, Qt-free measurement tooling) is the M1
   backend. The event→text→anchor path is proven; per-application coverage is an application property and is
   reported, never promised.
2. **X11: XFixes `PRIMARY`** with ownership notifications is the M1 backend on an X11 session, using a
   **two-attempt conversion**: the event timestamp with a short deadline (250 ms), then `CurrentTime`. The
   winning attempt is reported. A single 1.5 s blocking attempt is not acceptable for an interactive tool.
3. **Zero-length ranges are not selections.** They are ignored, and the coordinator's stability delay
   applies. A zero-length range must never produce a request or an error popup.
4. **One event type, one backend at a time.** Wildcard subscriptions are prohibited: a measured `object`
   subscription produced thousands of events per minute from shell and daemons with no user action.
5. **Coverage is reported, not assumed.** The capability report distinguishes interface presence, access and
   observed behaviour; the verification matrix records per-application results. Browsers and Electron apps
   are *unverified/unsupported* until a measurement shows otherwise, and the explicit paste flow is the
   documented alternative.
6. **`widget toolkit accessibility` is a user decision.** If a future target requires enabling desktop
   accessibility to reach browser content, that is an explicit, explained, reversible choice — never a silent
   toggle.

## Consequences

- M1 can be built without further feasibility work. The remaining unknowns are coverage, not architecture.
- The terminal path cannot be claimed as "works": VTE's accessible text exposed zero-length ranges in most
  attempts. It goes into the matrix as partially supported, with the paste flow beside it.
- The X11 conversion strategy becomes a product requirement, not a probe detail: a short first deadline keeps
  the worst case at ~250 ms + retry instead of 1.5 s.
- The probe's GTK publisher becomes the regression test for the read path: it needs no human interaction, so
  CI-like checks and future refactors can use it.
- Data-control backends (`ext-data-control-v1`, `wlr-data-control`) remain deferred to M2; nothing measured
  here justifies their complexity.

## Alternatives considered

- **QtDBus-only AT-SPI client**: rejected earlier (ADR-level note in `04-selection.md` §7.2) — signal slots do
  not expose the D-Bus sender, so events cannot be attributed to an application, and `RegisterEvent`'s wire
  signature varies by at-spi2-core version.
- **Clipboard-first model**: would abandon the product's premise and only help where the user can copy.
- **Waiting for browser coverage before shipping**: would block the vertical slice indefinitely; the paste
  flow covers those applications today and the matrix records the gap honestly.
