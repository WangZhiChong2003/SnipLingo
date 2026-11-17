# ADR 0004 — Evaluate Flatpak as a distribution channel

- **Status:** Proposed; the earlier primary-channel decision requires validation
- **Date:** 2025-11-16; reassessed 2026-11-16
- **Related:** `docs/DESIGN.md` §12, `docs/linux-integration.md` §8

## Context

The original design chose Flatpak as the primary channel before an application or a selection backend was
implemented. Automatic selection is now the primary workflow, and its feasibility depends on source-app
accessibility, compositor protocols and the application sandbox. A portal or Wayland socket being present
is not proof that this workflow works.

## Proposed direction

Use native builds for initial development and evaluate Flatpak in parallel on representative GNOME and KDE
Wayland sessions. Decide the primary distribution channel after testing automatic selection, manual paste,
credential storage, permissions and normal application controls in both environments. Publish the actual
feature differences if both channels are shipped.

Flatpak may simplify dependency distribution and provide useful sandboxing, but does not automatically
provide unfocused clipboard access, a supported data-control global, accessibility coverage or a working
portal backend. Secure storage may use a portal-provided master key and an encrypted application file, not
individual host keyring entries. Each path needs real testing and clear failure handling.

## Consequences

- No package format is promised as implemented or feature-complete until the prototype and verification
  matrix exist.
- Test the actual permission requirements and privacy consequences; do not weaken isolation merely to make
  a feature appear universal.
- Supply a normal application-window entry for paste, settings and pause even when the desktop has no tray.
- AppStream, desktop files, packaging manifests and distribution CI are release deliverables, not current
  repository contents.
