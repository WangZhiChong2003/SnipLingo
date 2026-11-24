# Contributing to SnipLingo

Bug reports, desktop compatibility reports, fixes and documentation are welcome. For larger changes,
open an issue describing the workflow or problem first. See [the architecture overview](docs/architecture.md)
for the application structure and [the code of conduct](CODE_OF_CONDUCT.md) for community expectations.

## Build and test

Install the [build and runtime dependencies](README.md#get-sniplingo). For development on Ubuntu,
also install `clang-format-18`, `xvfb`, `xauth`, `xclip`, `dbus-daemon` and `libxcb-xtest0-dev`. Clang-format
18 can also be installed with `pip install clang-format==18.1.8` on Ubuntu 22.04. The presets build
with tests enabled and require the Qt Test module from `qt6-base-dev`.

```sh
cmake --preset dev
cmake --build --preset dev --parallel "$(nproc --ignore=2)"
xvfb-run -a sh -c 'SNIPLYNGO_X11_TEST_DISPLAY="$DISPLAY" ctest --preset dev --output-on-failure'
cmake --build --preset dev --target format-check
```

Use clang-format 18 when formatting C and C++ files. The `format` target applies the repository's
[formatting rules](.clang-format). Tests use local stubs for network requests. Some tests use a private
D-Bus daemon or loopback sockets. X11 integration tests need an isolated X server.

For example, set `SNIPLYNGO_X11_TEST_DISPLAY=:99` while running the outside-click test with an
isolated X server on display `:99`.

```sh
SNIPLYNGO_X11_TEST_DISPLAY=:99 ctest --preset dev -R '^x11_outside_click_live_test$'
```

## Report a bug or test a desktop change

For selection problems, use the [Linux troubleshooting guide](docs/linux-integration.md). In a
bug report or pull request, give the distribution, desktop or compositor, display server, scaling
and steps you tried. For selection, include which app and text field you tested. Report X11 and
Wayland results separately. Remove private text, keys and paths before sharing diagnostics.

Use [the security policy](SECURITY.md) to report a vulnerability privately.

## Submit a change

- Keep pull requests focused and describe the user-facing problem, the change and the checks you ran.
- Add tests for behavior changes, including applicable cancellation and failure cases. For platform
  changes, state which environments you tested.
- Keep UI strings translatable with `tr()` or `qsTr()`. Interface translations live in
  `translations/`; after adding a string, run
  `cmake --build --preset dev --target update_translations` and translate the new entries. Keep blocking work off the GUI thread.
- Keep credentials in the secret store and selection or translation text out of logs.
- Update the affected guides and add user-visible changes under **Unreleased** in [the changelog](CHANGELOG.md).
- Use [Conventional Commits](https://www.conventionalcommits.org/) and sign off commits with
  `git commit -s` under the [Developer Certificate of Origin](https://developercertificate.org/).
  Explain any new dependency in the pull request.

For a release, update [VERSION](VERSION). It supplies the version shown by the application and CLI.
