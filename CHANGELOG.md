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
- Store provider credentials in the system keyring where available, and configure a proxy for
  remote requests.

### Fixed

- Close the floating result when clicking outside it where the desktop supports that behavior.
- Show clearer connection errors without displaying remote response bodies.
