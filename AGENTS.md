# Repository guidance

SnipLingo is a Linux-first Qt 6 desktop translator. Read the relevant code and
[contribution guide](CONTRIBUTING.md) before editing. Use the
[architecture overview](docs/architecture.md) for module boundaries.

- Keep changes focused and reuse existing code paths. Preserve public interfaces and behavior
  outside the requested change.
- Keep blocking I/O and expensive work off the GUI thread. Respect desktop capability differences
  and test X11 and Wayland separately when changing platform behavior.
- Observe selections only after explicit opt-in or during a local test. Require separate consent
  for automatic remote sending. Keep captured text and credentials out of logs.
- Use existing translation functions for UI strings and English for developer documentation.
- Add or update tests for behavior changes and run the relevant build and test commands in the
  contribution guide. Report any desktop configuration or check you could not verify.
