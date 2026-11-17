# Security Policy

SnipLingo is designed to handle **screen content** and **text the user selected or the application
recognized**. Both can contain credentials, private messages, or business documents. Security
reports about how that data is captured, stored, logged, or transmitted are taken seriously.

The project is still in the design phase. The mitigations below are requirements, not implemented guarantees.

## Reporting a vulnerability

**Do not open a public issue.**

1. Prefer GitHub's private reporting: **Security → Report a vulnerability** on this repository
   (GitHub Security Advisories).
2. Alternative: email **2962072587@qq.com** with `SnipLingo security` in the subject.

Please include:

- SnipLingo version (`sniplyngo --version`) and install channel (Flatpak / native / from source)
- Distribution, desktop environment, display server (Wayland/X11), and scaling
- Reproduction steps and the observed vs. expected behaviour
- Impact assessment: what data is exposed, to whom, and under what preconditions
- Proof-of-concept if you have one (please redact real screen content)

**Response targets** (best effort, single-maintainer project):

| Stage | Target |
| --- | --- |
| Acknowledgement | 5 days |
| Initial assessment | 10 days |
| Fix or mitigation for high-severity issues | 30 days |
| Public advisory | After a release containing the fix |

Credit is given in the advisory unless you ask otherwise. Coordinated disclosure is expected: please do not
publish before a fixed release exists.

## Supported versions

| Version | Supported |
| --- | --- |
| `main` (pre-alpha, unreleased) | Development branch — reports welcome, no released build to patch |
| `< 1.0` | Not released |
| `1.x` | Supported once released; the latest minor receives fixes |

## Threat model

The threat model focuses on data SnipLingo could observe, store or transmit:

| Threat | Mitigation |
| --- | --- |
| Accidental transmission of selected text | Explicit enablement of selection observation and separate consent for automatic remote sending; show the configured endpoint and provide pause/resume. |
| Sensitive text or screenshot leakage | Do not log selected text or images; do not deliberately persist them by default. Verify each screenshot backend's temporary-file behavior. |
| Credential leakage | Use OS-backed secure storage, including an appropriate sandbox backend; report failure rather than silently writing plaintext. |
| Hostile provider/OCR output | Render untrusted results as plain text and limit input/output sizes. |
| Silent capability bypass | Respect compositor, accessibility, portal and sandbox boundaries; disclose unsupported paths. |
| Command injection | Use structured process arguments rather than interpolating untrusted strings into shell commands. |
| Privilege misuse | Do not require elevated privileges, `/dev/uinput` access or synthetic copy. |

Explicitly **out of scope** (report them upstream, not here):

- A malicious program already granted access to the same screen, selection or clipboard data.
- The security model of third-party translation providers and their handling of submitted text.
- Tesseract/ONNX model bugs, unless SnipLingo's default configuration makes the impact worse.
- X11 clients being able to snoop on each other — that is a property of the X11 protocol, not of this
  application. SnipLingo must not make it *worse*, which is testable.

## Hardening notes for users

- Pause automatic selection before selecting content that should not be translated.
- Verify the configured destination before enabling automatic remote transmission. A local service has its
  own behavior and must be trusted not to forward text elsewhere.
- History and translation cache are deferred; if added, each must have explicit retention controls.

## Secure development

Contributors: `CONTRIBUTING.md` §3 lists the rules reviews enforce (no secrets, no shell interpolation, no
blocking work on the UI thread, no log content). Security-relevant behaviour changes must be called out in
the pull request description and reflected in `docs/DESIGN.md` §8.
