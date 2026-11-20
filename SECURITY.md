# Security policy

## Report a vulnerability

Use **Security → Report a vulnerability** on the project repository if private vulnerability
reporting is available. You can also email **2962072587@qq.com** with `SnipLingo security` in
the subject. Please keep the report private while we investigate.

Include the SnipLingo version or commit, your Linux distribution and desktop, the steps to
reproduce the problem, and what data or functionality is affected. Use sample text and redact
secrets and personal content from screenshots, logs and diagnostic output.

## Supported versions

SnipLingo is in pre-alpha. Security reports for the current development branch are welcome.
Fixes are made on the development branch.

## Text and credentials

- SnipLingo reads selections when you enable automatic selection or run a temporary local selection
  test.
- With an online engine, submitting a manual translation sends the text to the configured
  destination. Automatic translation requires separate permission to send selections to that
  destination.
- SnipLingo keeps the current translation in memory. It uses the system keyring for credentials
  when available and supports session-only storage when persistent storage is unavailable.
- You can pause automatic selection before selecting text that you do not want to send to the
  configured engine.

For setup and troubleshooting, see [the engine guide](docs/engines.md) and
[the Linux guide](docs/linux-integration.md).
