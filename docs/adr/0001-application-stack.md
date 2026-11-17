# ADR 0001 — C++20 + Qt 6 / QML as the application stack

- **Status:** Accepted
- **Date:** 2025-11-16
- **Deciders:** Project maintainer
- **Related:** `docs/DESIGN.md` §3

## Context

SnipLingo is a Linux-first desktop translation tool with goals including low idle footprint, fast
startup, responsive interaction, correct mixed CJK/Latin text shaping, Wayland and X11 support,
portal/D-Bus integration, and a floating window that must look polished. Candidate stacks were
C++/Qt 6, Rust/GTK4, Python/GTK4, and Electron/Tauri.

## Decision

Use **C++20 with Qt 6 (Quick/QML)**, minimum Qt 6.4, developed against Qt 6.11, built with CMake + Ninja,
tested with Qt Test/CTest.

## Rationale

- Qt 6 provides mature Wayland client support (`xdg-shell`/`xdg-popup`, per-screen DPR,
  `wp_fractional_scale_v1` from 6.5), `QNetworkAccessManager` for streaming responses, `QDBusConnection`
  for portals, and `QSystemTrayIcon`/SNI — all first-party, no glue crates.
- Qt Quick supports the proposed popup and multilingual text layout. Its footprint and desktop behavior
  still need measurements; no comparative performance conclusion is established yet.
- Qt handles multilingual text shaping and font fallback, which is the highest-risk rendering area for a
  translation popup.
- Qt 6 is available on every relevant distribution and is a supported Flatpak runtime base.

## Consequences

- Contributors need a C++ toolchain; CMake presets and reproducible setup are planned but not present yet.
- Qt 6.4+ excludes some older distributions from native builds; sandbox packaging requires its own tests.
- QML's binding engine requires discipline in hot paths; measure actual startup, idle and interaction
  behavior as described in `DESIGN.md` §9.

## Alternatives considered

- **Rust + GTK4, Python + GTK4, Electron or Tauri:** viable alternatives with different contributor,
  packaging and runtime trade-offs. No repository-specific benchmark establishes their relative costs.
