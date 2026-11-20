# Changelog

User-visible changes are listed here. See [the README](README.md) for the current preview and setup.

## Unreleased

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
- Store provider credentials in the system keyring where available, and configure a proxy for
  remote requests.

### Fixed

- Close the floating result when clicking outside it where the desktop supports that behavior.
- Keep the floating result open when the pointer merely moves away from it on X11 desktops; an
  outside click still closes it.
- Draw the floating result's close button with the same caption-button style as the main window.
- Show clearer connection errors without displaying remote response bodies.
