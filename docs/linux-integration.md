# SnipLingo — Linux Integration Notes

This document describes candidate designs, not tested compatibility. No SnipLingo application or desktop
backend exists yet. A protocol being documented, advertised or accessible does not prove that a given
source application reports usable selections. Record real results using §9 before making support claims.

## 1. Capability, session and privacy boundaries

Determine the active toolkit/display connection and probe individual interfaces. Desktop and session names
are useful diagnostics, but do not prove functionality. Distinguish the host desktop, SnipLingo's display
connection and whether the source application runs through XWayland. Native and Flatpak builds are separate
test configurations.

Automatic observation starts only after the user enables it; the app also needs explicit user confirmation
before automatically transmitting selections to a remote provider. Do not monitor keyboard events, mouse
motion, screenshots or the ordinary clipboard. Pause stops selection observation and cancels or invalidates
work. Do not read private content merely to populate `doctor`.

## 2. Automatic selection: candidate backends

| Candidate | Where it can help | What must still be established |
| --- | --- | --- |
| X11 `PRIMARY` + XFixes | X11 clients and Wayland clients whose selections the compositor shares with XWayland | Selection changes may not always result in new ownership notifications; the source must serve the selected text. |
| AT-SPI | Accessible applications on X11 or Wayland, potentially including GNOME and KDE | Accessibility bus access, event emission, text ranges, protection states and sandbox behavior vary by application and session. |
| Wayland `ext-data-control-v1` | Compositors exposing this privileged protocol to the app, with primary selection enabled | Protocol exposure, authorization and usable primary selections must be tested for the compositor and installation. |
| Wayland `wlr-data-control` | Compositors exposing the protocol and primary-selection events to the app | Version 2 is needed for the primary-selection event; the source app must actually publish it. |

The first working implementation should select one tested source at a time and report it. Do not start
multiple listeners and assume their events correspond one-to-one. Consider additional sources only when a
real application coverage gap justifies their correlation and duplicate-handling complexity. Never treat an
ordinary clipboard update as evidence of a new selection.

The current `auto` policy prefers a reachable XFixes interface, including XWayland, and otherwise chooses
AT-SPI. That preference follows observed coverage on a GNOME session; it is not proof that another compositor
shares native selections. Users may select AT-SPI explicitly when it provides better coverage.

An absent event alone cannot distinguish an unsupported source application from a user who has not made a
selection. A user-initiated selection test can help diagnose coverage; `doctor` must not claim certainty
about a particular application's behavior from interface presence alone.

### 2.1 X11 PRIMARY and XFixes

Register for XFixes `PRIMARY` ownership-related notifications, then asynchronously request the text from
the current selection owner after a short stability delay. Apply size limits, timeouts and request
cancellation; an owner may disappear or decline a transfer. Ignore the previous selection when enabling or
resuming, and verify that a newer event invalidates pending work. The delay does not prove a drag has ended;
intermediate selections can still lead to multiple translations and remote requests.

This is not an event for every possible text mutation. The X11 inter-client conventions say a client should
reacquire ownership when the selection completely changes, but need not do so for a modification regarded
as the same selection. An application may not expose selection text at all. Repeated selection and dragging
inside one source app therefore need real tests, as does behavior across the XWayland bridge. Do not poll
the clipboard or globally capture pointer events to manufacture a stronger guarantee.

Source: [X11 inter-client communication conventions](https://www.x.org/docs/ICCCM/icccm.pdf).

### 2.2 AT-SPI on Wayland and X11

AT-SPI's event listener supports `object:text-selection-changed`; an accessible text object can expose its
selected ranges and text. Prototype an event subscription and resolve only the triggering object's selected
range, with bounded asynchronous work. Do not enumerate unrelated app contents or subscribe to text-change,
keystroke or global pointer events. Ignore this app's own events and protected/password roles when exposed.

This is not a universal permission or a bypass of Wayland: it works only when the session grants access and
the source application exposes the appropriate accessibility object, event and selected text. Some browsers
or applications may require accessibility support to be activated; some widgets, terminals, canvas content
or sandboxed apps may not expose the needed data. Multi-element browser selections require particular care.
Test what a real user can select rather than assuming an event yields the entire selection.

Sources: [AT-SPI event registration](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/method.EventListener.register.html),
[AT-SPI text interface](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/iface.Text.html),
[Chromium accessibility testing prerequisites](https://new.chromium.org/developers/accessibility/testing/automated-testing/ax-inspect/).

### 2.3 Wayland data-control and ordinary primary selection

`ext-data-control-v1` and `wlr-data-control` can advertise primary selections to authorized clients where
the compositor implements and exposes them. They are different protocols; do not infer support from the
desktop name, installed helper commands, or the presence of a Wayland socket. The first offer on binding may
represent an old selection and must not trigger automatic translation on enable or resume.

Ordinary `primary-selection` delivery goes to the focused client and is not equivalent to privileged
data-control. `wl-paste`, `QClipboard`, and the Wayland socket do not guarantee unfocused access; they may
require focus or compositor-specific behavior. The Clipboard Portal extends compatible Remote Desktop or
Input Capture sessions and is not a general-purpose replacement for automatic background selection access.
Respect the compositor and sandbox access decisions; never grant broad device access to force a backend.

Sources: [ext-data-control](https://wayland.app/protocols/ext-data-control-v1),
[wlr-data-control](https://wayland.app/protocols/wlr-data-control-unstable-v1),
[primary selection](https://wayland.app/protocols/primary-selection-unstable-v1),
[Clipboard Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Clipboard.html).

## 3. Explicit alternatives when automatic selection is unavailable

The universal product alternative is to open SnipLingo and paste or type text into its own input field. A
user may explicitly choose a separate clipboard translation action where reading the clipboard is supported;
that action translates the clipboard as such, not a claimed current selection. An optional desktop shortcut
can open the input field or request region capture, but availability and registration require testing.

Where a tested screenshot backend and OCR are available, the user may explicitly choose region translation
for text that is not selectable. Both actions require extra interaction and are not automatic-selection
support. Browser-specific extensions or compositor integrations could improve coverage later, but are
separate projects/decisions, not a guarantee for unrelated applications.

Synthetic `Ctrl+C` is not planned. It can overwrite the clipboard, act on the wrong focus or interrupt a
terminal, and `/dev/uinput` access is broader than this application needs.

## 4. Global shortcuts

Global shortcuts are optional conveniences for pause, manual input or capture. Probe the Portal
`GlobalShortcuts` interface and desktop backend, or use a tested X11/desktop registration mechanism. A
portal may show a binding dialog and may grant only a subset of requested bindings. Register failures and
conflicts must be reported; normal application-window controls remain usable without a tray or global
shortcut. Never imply that registering a shortcut grants selection access.

Measured on one GNOME Wayland session (probe Run 1): `org.freedesktop.portal.ScreenCast` = 4,
`org.freedesktop.portal.Screenshot` = 2, `org.freedesktop.portal.Secret` = 1, and
`org.freedesktop.portal.GlobalShortcuts` **not exported at all**. That session therefore has no portal
shortcut path; the supported route is a desktop binding that runs the CLI trigger. Do not treat the portal as
the default until a probe shows it on the target desktop.

Source: [Global Shortcuts Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html).

## 5. User-initiated screen capture

Capture is a later, independent feature. Prefer a true region selection when supported. If only a full
screen or stream is available, disclose the broader capture scope before the user chooses it; do not claim
that only the selected rectangle was captured. A system chooser may replace our own overlay and may appear
more than once. If capture is unavailable or denied, report it and retain explicit paste.

The Screenshot Portal returns a URI; the interface does not promise an in-memory result or define the
ownership and cleanup of every backend's file. Verify how each backend stores and exposes the image; do not
unconditionally delete an arbitrary URI. If a strict no-temporary-file guarantee is required, disable any
path that cannot establish it. The app does not deliberately persist images.

Measured on one GNOME Wayland session (probe Run 1): the Screenshot portal reports version **2**, so neither
`target` nor `AvailableTargets` exists there and an area request is impossible; a portal capture means a
full-screen image plus our own crop. Confirm the version on each target before designing a capture flow.

`target` and `AvailableTargets` were added in Screenshot interface version 3; `target=4` is only usable
when the interface and actual backend advertise area support. It cannot be a fallback when the Screenshot
Portal itself is unavailable. Portal and backend versions are distinct. A ScreenCast session is another
possible capture mechanism, but needs separate consent, lifecycle and persistence validation; no warm stream
is enabled by default.

Sources: [Screenshot Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html),
[ScreenCast Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html),
[portal release notes](https://github.com/flatpak/xdg-desktop-portal/blob/main/NEWS.md).

## 6. Window placement, focus and accessibility

On X11 the app may be able to use cursor or selection geometry. On Wayland, a normal application cannot
assume access to the global pointer position, selection coordinates, active monitor or arbitrary toplevel
placement. `xdg_popup` is relative to a valid mapped parent; owning an overlay or knowing a coordinate does
not make another application's window its parent. Treat positioning as best effort and let the compositor
place a normal window when a valid anchor is unavailable.

The automatically shown window should be passive and avoid forcing activation. Keys such as `Esc` and
`Ctrl+C` are delivered to the focused window, so popup-local shortcuts become available when the user
focuses it; a global shortcut is optional and may be unavailable. Do not promise both uninterrupted source
focus and delivery of arbitrary popup-local shortcuts. Provide clickable actions and a regular settings
window; GNOME installations without a status-item extension must not depend on a tray.

Source: [XDG shell](https://wayland.app/protocols/xdg-shell),
[Qt window positioning](https://doc.qt.io/qt-6/qwindow.html#setPosition).

## 7. Fractional scaling and multiple displays

Keep logical surface coordinates, source image pixels and per-output transforms distinct. Establish the
captured output, origin, scaling, rotation and crop mapping from the actual backend metadata; one global DPR
or one image-width ratio is not sufficient for every combined or mixed-DPI screenshot. If mapping cannot be
determined, report a limitation rather than silently cropping the wrong content. Test 100%, 125%, 150% and
200% scaling and mixed-DPI monitors on real desktops; reject unsupported cross-monitor selections clearly.

## 8. Packaging and credentials

Test native and Flatpak separately. An exposed Wayland socket does not imply permission to use a privileged
data-control global or unfocused clipboard access. An accessibility bus or Secret Portal also depends on
the actual sandbox and backend. The [Secret Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.impl.portal.Secret.html)
provides a per-application master secret; libsecret may use a portal-backed encrypted file rather than
storing each credential directly as a host keyring entry. Verify storage, access and failure behavior in each
shipped configuration. Do not silently fall back to an unencrypted 0600 file.

## 9. Manual verification matrix

No rows are currently tested. Record the exact compositor, source-app and portal versions, install channel,
result and reproduction details; do not put selected text or credentials in reports.

| Environment / channel | Qt/GTK text | Browser page and editable field | Terminal | Repeat / drag in same app | Pause, resume and focus | Status |
| --- | --- | --- | --- | --- | --- | --- |
| GNOME Wayland, native | | | | | | Untested |
| KDE Wayland, native | | | | | | Untested |
| X11, native | | | | | | Untested |
| XWayland source on Wayland | | | | | | Untested |
| GNOME and KDE Wayland, Flatpak | | | | | | Untested |
| Other tested compositors | | | | | | Untested |

For each automatic success, test selection replacement, empty/unselectable text, protected input, normal
clipboard preservation, no request before consent, no duplicate results, and late-response cancellation.
Separately test manual paste, optional shortcuts, capture permissions, mixed DPI and no-tray operation. A
candidate source that cannot satisfy a row is recorded as unsupported; do not infer success from a different
row.

## 10. Troubleshooting principles

- Automatic mode paused or not configured: explain how to enable it and which provider receives text.
- Source not observable: open the explicit paste flow. Do not read and translate an old clipboard value.
- Accessibility unavailable: identify the session/application limitation; do not turn on a screen reader or
  browser-specific option without the user's knowledge.
- Capture or shortcut unavailable: offer the normal app window and explain the exact tested alternative.
