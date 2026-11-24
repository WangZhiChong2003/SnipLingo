# Changelog

User-visible changes are listed here. See [the README](README.md) for installation and setup.

## Unreleased

### Changed

- Grow both the original and translated text panes with their content. Each pane scrolls when
  it reaches its height limit.
- Widen the floating result for longer lines. Limit its width and height to half of the current
  display's work area, including when interface zoom is enabled.
- Call the engine catalog category **Web translation** and show provider names for configured web
  engines. The catalog explains that these services use unofficial endpoints.

### Fixed

- Use the same font size for the translation and original text. Adjust both under
  **Settings → Interface → Result text size**.
- Keep the XWayland translation card open when expanding the original moves the card away from
  the pointer.
- Keep the chosen language visible in **Settings → Interface** after switching languages.
- Keep language changes from reopening automatic translation consent dialogs.
- Match the corner radius of consent dialogs and floating results to the main window.
- Remember automatic sending permission for each destination. Save new engines that require
  permission only after approval, and show the unofficial endpoint notice in their consent dialogs.
- Restore automatic selection on startup without opening a consent dialog for an unapproved engine.

## 1.0.0

### Changed

- Support builds on Ubuntu 22.04 using its system Qt 6.2.4 packages.

### Added

- Translate selected text in supported Linux apps, or type and paste text into the main window.
- Choose a local translation service or an online engine, including DeepL and OpenAI-compatible
  services. Set a target language and test the selected engine with sample text.
- Enable automatic selection separately from authorization to send selections to an online engine.
  Pause observation and resume at the next selection.
- Read results in the main window or use floating results on desktops that support them, including
  an optional XWayland mode in Wayland sessions.
- Keep SnipLingo running in the background, with tray actions to reopen or quit on desktops that
  show tray icons.
- Pin the floating translation to keep it on screen while you select more text; new selections
  update the pinned card in place, and unpinning restores click-to-close.
- The floating translation card grows with the translated text and switches to scrolling once it
  would cover half the screen.
- Zoom the whole interface and set the translation and original text size under
  **Settings → Interface**; both apply immediately.
- Switch the interface between English and Simplified Chinese under
  **Settings → Interface**, or follow the desktop locale. The language changes immediately,
  including the tray menu.
- Store provider credentials in the system keyring where available, and configure a proxy for
  remote requests.

### Fixed

- Close the floating result when clicking outside it where the desktop supports that behavior.
- Keep the floating result open when the pointer merely moves away from it on X11 desktops; an
  outside click still closes it.
- Draw the floating result's close button with the same caption-button style as the main window.
- Position the floating selection button using the pointer after the selection settles. On
  XWayland, use the main window when the pointer cannot be matched to the source application.
- Add an optional GNOME Shell 42–44 extension to place the floating selection button near the
  pointer in native Wayland apps.
- Show clearer connection errors without displaying remote response bodies.
