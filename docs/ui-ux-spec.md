# SnipLingo — UI / UX Specification

This is a proposed interaction design; no UI is implemented yet. Automatic translation is the primary
experience where a selection backend has been validated. Desktop restrictions and source-application
coverage must be visible to the user rather than hidden behind an unrelated clipboard read.

## 1. Interaction Model

### 1.1 First run and automatic mode

1. Explain that enabling automatic mode observes selection changes and can translate selected text without
   a shortcut. The user explicitly enables it; the app starts paused until that choice is made.
2. Let the user configure a local or remote provider and target language. A preset does not constitute
   consent to send data. Before enabling automatic transmission to a remote provider, name the destination,
   explain that selections may be sent automatically and may incur charges, and obtain explicit confirmation.
   Changing the remote destination requires renewed confirmation before automatic transmission resumes.
3. Report the chosen selection backend and its limitations without claiming that a detected interface works
   in every application. Offer a user-initiated test and an explicit paste field.
4. When enabled and configured, a new eligible selection produces a result automatically after a short
   stability delay. A previously selected item must not be translated merely because the app starts or the
   user resumes.

Pause/resume is available from the normal application window and, where a tray exists, from its menu. Pause
stops observation and invalidates pending work; resuming waits for a new selection. Settings show whether
mode is active, paused or unavailable and where text will be sent. A global shortcut is optional.

### 1.2 Other inputs

- **Paste or type:** the user opens SnipLingo and supplies text explicitly. This is the reliable product
  alternative when no automatic source works. A separate clipboard action, if supported, translates only
  the clipboard the user explicitly requested.
- **Region OCR:** planned for a later milestone. The user requests capture, approves any system dialog and
  selects a region. The capture backend may be unavailable, use a system chooser or capture a broader area;
  disclose its actual behavior before use.

Never translate an old clipboard value as if it were a new selection. Do not synthesize `Ctrl+C`.

### 1.3 Result window

The window presents the translation as plain text, an optional original-text disclosure, the configured
provider or endpoint, a close action and an explicit copy action. Loading, result and error states must be
readable for streaming and non-streaming providers. A new selection replaces the previous request; late
results from any input flow cannot overwrite it. Empty or inaccessible automatic selections should not
produce a stream of error popups.

The automatic window is passive by default and must not intentionally interrupt typing in the source app.
Position near a reliable selection anchor when the platform permits it; otherwise allow compositor placement.
There is no universal guarantee of cursor location, screen center or exact coordinates on Wayland.

### 1.4 Focus and keyboard behavior

A passive popup cannot receive keyboard events while another application holds focus. When the user focuses
the result window, `Esc` dismisses and cancels the current request, `Ctrl+C` copies selected result text and
standard Tab navigation reaches controls. A dedicated copy button copies the full result. A global shortcut
for pause or dismissal may be offered only where it can be registered; it is not a prerequisite for the app.
Clicking the close button is always available when the window is visible. Do not force focus to make a
keyboard promise appear to work.

Settings, the paste field, consent prompts and result controls must support standard keyboard navigation.
The desktop decides focus restoration and may deny programmatic window activation. Do not move the pointer,
auto-paste into the source app or alter the clipboard without a user's copy action.

### 1.5 Popup states

| State | Behavior |
| --- | --- |
| Idle / paused | No automatic selection subscription or automatic request. |
| Waiting for selection stability | A short, cancellable delay; no remote request yet. |
| Loading / streaming | Show progress and, if available, incremental plain text. |
| Done | Display translation, source disclosure, destination, copy and close. |
| Error | Describe the provider or acquisition failure and give a relevant action. |

No permanent queue or history is implied. An explicit paste or OCR operation replaces an automatic request
in the same result window. Optional history and cache are deferred and would need separate consent.

## 2. Visual System

The UI should be small, readable, localized and consistent with desktop light, dark and high-contrast
settings. Use semantic color tokens, system fonts, clear contrast, a visible focus indicator, and reduced
motion settings. Avoid effects or animations that delay results. Qt Quick is the current stack proposal;
verify startup, memory and accessibility before committing to elaborate visual effects.

The result area has visual priority over metadata and controls. Font sizes, dimensions, shadows and animation
timings are design defaults to validate with real mixed CJK/Latin text, scaling and assistive technology;
they are not performance or platform compatibility guarantees.

Proposed starting tokens, adjustable after testing:

| Role | Light | Dark |
| --- | --- | --- |
| Surface | `#FFFFFF` | `#16181D` |
| Primary text | `#1B1D21` | `#E8EAEE` |
| Secondary text | `#5A6068` | `#A2A9B4` |
| Accent | `#2F6FEB` | `#7AA2F7` |
| Warning | `#B26A00` | `#E0A24A` |

Start with a roughly 460-logical-pixel-wide popup, a resizable result area and system text around 15 px;
adapt to available space and user font scaling. Do not force these dimensions where the compositor or
accessibility settings require another size. Keep the original text visually subordinate but accessible.

## 3. Settings and Onboarding

The normal application window provides:

- Automatic selection enabled/paused, selected backend, and a plain explanation of its actual coverage.
- Provider and destination, automatic remote-send consent, source/target language, and credential setup.
- Explicit paste input and optional clipboard action.
- Optional shortcuts and a clear registration status; region capture/OCR when implemented.
- Privacy information and a non-sensitive diagnostic report.

A screen-reader or source-application accessibility setting must not be changed without the user's
knowledge. Backend failure must offer a useful manual path without opening repeated dialogs. On desktops
without a tray, all essential controls remain accessible in the app window.

## 4. Accessibility and Localization

All user-visible strings are localizable. Respect system font scaling and theme, use plain text for
untrusted provider output, provide meaningful accessible names and roles, and support reduced motion.
Avoid announcing every partial streaming update to a screen reader. Test SnipLingo itself with a screen reader
and ensure observing other applications does not depend on running a screen reader unless that specific
setup has been tested and explained.

## 5. Verification

Verify on real X11 and Wayland sessions with and without a tray: first-run consent, pause/resume, repeated
selection, protected or unsupported sources, focus, global-shortcut failure, explicit paste, destination
disclosure, cancellation, and mixed-language text. Use the source-app and installation-channel matrix in
[Linux integration](linux-integration.md#9-manual-verification-matrix). Record untested combinations.
