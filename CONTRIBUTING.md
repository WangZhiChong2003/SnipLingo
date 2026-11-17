# Contributing to SnipLingo

Thanks for helping. This project is a small, deliberately narrow desktop utility: a translation window that
appears after an eligible text selection. Changes are judged by whether they make it **more correct, faster, or
easier to maintain** — not by how much of the Linux desktop they automate.

Please also read [`docs/DESIGN.md`](docs/DESIGN.md) before proposing architectural changes; if a design
decision needs to change, it changes through a new ADR in [`docs/adr/`](docs/adr/), not through a patch.

---

## 1. Ways to contribute

| Kind | Notes |
| --- | --- |
| Real-hardware feasibility reports | The highest-value contribution right now. Use the matrix in [`docs/linux-integration.md`](docs/linux-integration.md) §9 and state the exact DE, compositor, source application and install channel. |
| Bug reports | Use the issue template. A non-sensitive `sniplyngo doctor --json` report is planned but not implemented. |
| Platform fixes | Wayland/X11 behaviour, portal version quirks, HiDPI math. |
| OCR fixtures | Screenshots with mixed Chinese/English UI text, 100 %/125 %/150 %/200 % scaling, light and dark themes. Redact anything personal. |
| Translation providers | Read [ADR 0003](docs/adr/0003-provider-agnostic-translation.md); additions need real API and behavior tests. |
| Documentation | Especially honest statements of limitations. |

Not accepted: telemetry, analytics SDKs, auto-update channels, bundled fonts, new runtime dependencies
without justification, and features that require the application to run with elevated privileges.

---

## 2. Development environment

Ubuntu 24.04 is the proposed reference environment. No CMake project or presets exist yet; the commands
below describe the intended interface once the application skeleton is added, not a currently working build.

```bash
sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev qt6-declarative-dev \
                 libatspi2.0-dev libxcb-xfixes0-dev libsecret-1-dev \
                 xdg-desktop-portal xdg-desktop-portal-gnome clang-format

cmake --preset dev
BUILD_JOBS=$(( $(nproc) - 2 ))
[ "$BUILD_JOBS" -lt 1 ] && BUILD_JOBS=1
cmake --build --preset dev --parallel "$BUILD_JOBS"
ctest --preset dev --output-on-failure
```

The project targets **Qt 6.4+** and **CMake 3.21+** with **C++20**. Building against a newer Qt locally is
fine; code must not use APIs newer than 6.4 without a version guard.

### Build efficiency

Use the core count minus two for parallelism, with a minimum of one job. Redirect long build output to a log file
instead of streaming it, and read only high-signal diagnostics on failure. Do not poll a running build.

---

## 3. Code style

- Formatting is enforced: `clang-format` via [`.clang-format`](.clang-format). Run
  `clang-format -i` on changed files, or use `cmake --build --preset dev --target format`.
- Naming: `PascalCase` types, `camelCase` functions/variables, `k`-prefixed constants where they are
  compile-time, `m_` prefix for private members. QML: `PascalCase.qml` files, `camelCase` properties.
- Prefer `enum class`, `[[nodiscard]]`, `const`, and explicit ownership (RAII, no manual `delete`).
- No raw `new`/`delete` in application code; use values, `std::unique_ptr`, or Qt parent ownership.
- Errors are values (`Result<T>`) at module boundaries; exceptions are not used for control flow.
- One module, one reason to change. Do not add helpers, wrappers, or abstractions without a concrete
  problem they solve.
- Comments explain *why* (protocol quirks, DPI math, ordering constraints), never *what*.

### Rules that reviews enforce

1. No blocking work on the UI thread (OCR, network in sync form, D-Bus sync calls, image transforms > 1 MP).
2. No hardcoded user-visible strings — every string goes through `tr()`/`qsTr()`.
3. No secrets in code, logs, or config; credentials only through `SecretStore`.
4. No shell string interpolation; `QProcess` with argument lists only.
5. No desktop-environment branching on the product logic path — branch on probed capabilities.
6. No behaviour change outside the stated scope of the change.
7. New public interface ⇒ update `docs/DESIGN.md`; changed decision ⇒ new ADR.
8. New dependency ⇒ justify in the PR (maintenance, license, footprint, packaging impact).

---

## 4. Tests

| Change | Required tests |
| --- | --- |
| Text normalization, geometry/DPI math, stream parsing, prompt building | Unit test |
| Selection observer, capture, OCR, provider or shortcut backend | Contract tests and applicable failure/cancellation cases |
| QML behaviour (popup states, overlay) | `qmltestrunner` case |
| Window/desktop behaviour | Manual matrix row documented in the PR |

- `ctest --preset dev` must pass before review.
- Tests assert observable behaviour, not internal call order.
- Network tests run against a local stub server; never hit a live provider.
- Do not modify tests to make a failing implementation pass. If a test was wrong, say so and explain why in
  the PR.

### Reporting untested configurations

If you changed platform-specific code, state explicitly which display server, desktop environment, and
scaling you tested. "Tested on X11 only" is a complete answer; silence is not.

---

## 5. Commits and pull requests

**Commit messages**: [Conventional Commits](https://www.conventionalcommits.org/) —
`type(scope): imperative summary`, e.g. `fix(capture): measure pixel ratio from the image, not DPR`.
Types: `feat`, `fix`, `docs`, `refactor`, `test`, `build`, `ci`, `chore`, `perf`.

**Sign-off (DCO)**: every commit must be signed off, certifying the
[Developer Certificate of Origin](https://developercertificate.org/):

```bash
git commit -s -m "feat(translate): stream OpenAI-compatible responses"
```

**Pull request checklist**

- [ ] Scope is one topic; no unrelated formatting or refactoring.
- [ ] `cmake --build` and `ctest` pass locally.
- [ ] `clang-format` clean on touched files.
- [ ] Tests added/updated where behaviour changed.
- [ ] Documentation updated (`docs/`, README, `CHANGELOG.md` under `Unreleased`).
- [ ] Manual test platforms listed (display server, DE, scaling).
- [ ] No new dependency, or a justification paragraph.
- [ ] No secrets, tokens, personal paths, or captured user content in the diff.

Keeping PRs small and single-purpose is the fastest route to review. A larger change is welcome when it is
split into reviewable commits that each stand on their own.

---

## 6. Reporting security issues

Do **not** open a public issue for a vulnerability. Follow [`SECURITY.md`](SECURITY.md).

## 7. Community

All participation is governed by [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md). Be technical, be specific, and
assume good faith — especially about "works on my machine" differences between Linux desktops.
