# ADR 0002 — Selection-first text acquisition with an explicit capability ladder

- **Status:** Superseded by [ADR 0005](0005-automatic-selection.md)
- **Date:** 2025-11-16
- **Deciders:** Project maintainer
- **Related:** `docs/DESIGN.md` §5, `docs/linux-integration.md` §2

> Historical decision: the shortcut-first workflow and clipboard fallback below no longer define the
> product. They are retained to explain the change in direction; use ADR 0005 for the current design.

## Context

The product's primary workflow is "select text → one shortcut → translation appears". On X11 this is
trivial (`PRIMARY` selection). On Wayland it is deliberately impossible for an unfocused client to read
another application's selection: GNOME exposes no `wlr-data-control`, and no portal provides selection
reading. Options were:

1. Require the user to press Ctrl+C before triggering (clipboard-only model).
2. Send a synthetic Ctrl+C with `/dev/uinput` (privileged, opt-in, not available in Flatpak).
3. Make region OCR the primary workflow instead.
4. Ship an X11-only product and declare Wayland unsupported.

## Decision

Keep **selection translation as the primary workflow**, and implement acquisition as a probed capability
ladder: X11 `PRIMARY` → clipboard read → opt-in synthetic copy (uinput/dotool, native installs only) →
in-app paste field. Region OCR is the always-available fallback, not the default. Wayland never pretends
to read a selection it cannot read; the UI states the required action once and stops nagging.

## Rationale

- Choosing clipboard-only as the *product model* would misdescribe the app: the extra keypress is an
  environment limitation, not the intended interaction.
- Making OCR primary would add 200–400 ms and recognition error to the most common case.
- The ladder keeps one code shape (an `Acquirer` interface) while letting each desktop get the best
  experience it permits, and it keeps `doctor` able to explain precisely why the copy step was needed.

## Consequences

- Every acquisition path returns the same `Result<QString>`, so the pipeline above it is
  environment-agnostic.
- Synthetic copy is off by default, requires documented `udev` permission, and is disabled in the Flatpak
  build (UI must reflect the probe, not the build).
- On GNOME Wayland without uinput access, Flow A has one extra keypress. This is documented in the README,
  the wizard, and the troubleshooting table rather than hidden.
- The de-duplication rule (ignore identical clipboard content within 5 s) becomes necessary to keep the
  copy-based flow from double-firing.

## Alternatives considered

- **Clipboard-only as the design:** simpler, but permanently worse on X11 and wlroots compositors, and it
  bakes an environment limitation into the product model.
- **uinput-only:** would give a uniform one-step experience but requires elevated device permissions for a
  translation tool — unacceptable as a default and impossible for the primary distribution channel.
