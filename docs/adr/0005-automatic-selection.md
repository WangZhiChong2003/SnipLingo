# ADR 0005 — Automatic selection translation with explicit consent

- **Status:** Accepted product direction; backend feasibility is unverified
- **Date:** 2026-11-16
- **Supersedes:** [ADR 0002](0002-selection-first-acquisition.md)
- **Related:** [Top-level design](../DESIGN.md), [Linux integration](../linux-integration.md)

## Context

The product owner wants a translation to appear after selecting text, without a shortcut for each
selection. X11, Wayland compositors, applications and sandboxes expose different selection and accessibility
capabilities. In addition, selection text may be sensitive: a configured remote service could receive
selections the user did not intend to translate.

## Decision

Make user-enabled automatic selection observation the primary workflow. Ask separately for informed
consent before automatically sending selections to a remote provider. Provide a persistent pause/resume
control and an explicit paste workflow. Avoid automatic clipboard reading, input injection and capture.
When automatic selection is unsupported, say so; do not substitute an old clipboard value.

Prototype X11 `PRIMARY`/XFixes and AT-SPI on representative desktops, and investigate Wayland data-control
only where the compositor exposes primary selection to this app. The initial implementation uses a tested
source appropriate to the session; it must not assume that all apps expose events or merge multiple sources
without evidence-backed correlation. Determine actual support through a real desktop and application matrix.

## Consequences and trade-offs

- Automatic behavior is conditional on the selected backend, source app, user choice and provider. It is not
  guaranteed for every X11 app or universally available on Wayland.
- The automatic popup should remain passive; local popup keys require focus, while placement may be controlled
  by the compositor.
- An event-driven stability delay can reduce duplicate work, but cannot prove every drag or selection change
  has been observed. A shared request generation prevents stale results across all flows.
- Native and sandboxed packaging are validated separately. Browser-specific or compositor-specific
  integrations may later improve coverage; none is included or required for the first slice.
- User-facing documentation must distinguish selection observation from ordinary clipboard monitoring and
  disclose the destination before enabling automatic remote transmission.

## Sources

- [AT-SPI selection events](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/method.EventListener.register.html)
  and [text interface](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/iface.Text.html).
- [X11 inter-client communication conventions](https://www.x.org/docs/ICCCM/icccm.pdf).
- [Wayland primary selection](https://wayland.app/protocols/primary-selection-unstable-v1),
  [ext-data-control](https://wayland.app/protocols/ext-data-control-v1), and
  [wlr-data-control](https://wayland.app/protocols/wlr-data-control-unstable-v1).
