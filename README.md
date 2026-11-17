# SnipLingo

SnipLingo is an experimental Linux desktop translation application. It can translate text entered in its
window and, on supported desktops and applications, observe selections after the user explicitly enables
that feature. This is pre-alpha software; desktop integration and public translation services have limits.

[简体中文](README.zh-CN.md) · [Design](docs/DESIGN.md) · [Linux integration](docs/linux-integration.md) · [Contributing](CONTRIBUTING.md)

## Build and run

A C++20 compiler, CMake 3.21+, Ninja, Qt 6 (Core, Network, D-Bus, GUI, QML and Quick, plus Test for tests),
and the Qt Quick Controls runtime are required. AT-SPI and X11 development packages enable the optional
selection backends.
The interface text is prepared for localization; translated interface catalogs are not yet shipped.

```sh
cmake --preset dev
cmake --build --preset dev --parallel $(( $(nproc) - 2 ))
./build/dev/sniplyngo
```

Choose an engine and a target language, type or paste text into the window, and select **Translate**.
For an automatic selection, enable **Translate selected text**; remote automatic sending requires separate
permission for the selected destination. Check the listener status in the window, and pause or disable it at
any time. A small icon appears near the selection; hover over it to open the translation. Enable **Show
translation immediately** to open the result directly. Translation starts as soon as a selection is
resolved, subject to the remote-sending permission. The expanded result requests focus and closes when another
window is activated; the compositor can restrict focus handling. Use **Hide** to keep the app running in the background;
launching SnipLingo again reopens the window. Closing the window quits the app.

The Google preset is an unofficial service and can be unavailable or rate-limited. MyMemory has a limited
anonymous quota and accepts at most 500 UTF-8 bytes per request. LibreTranslate requires an independently
running instance; the preset points to `http://127.0.0.1:5000`, and its URL and optional key can be changed
in the window. An OpenAI-compatible service, including a local server, can also be configured there. The old
unofficial Bing authentication endpoint returned HTTP 404 in November 2026, so it is no longer offered as a
new preset in the window. No provider is guaranteed to be free, reachable, or available indefinitely.

For proxy troubleshooting:

```sh
./build/dev/sniplyngo proxy manual http://127.0.0.1:7897
./build/dev/sniplyngo engine target zh-Hans
./build/dev/sniplyngo engine test google-free
```

`engine test` sends a short sample (`hello`, or `你好` when translating into English) to the specified service; `--all` tests the built-in candidates and
configured engines. Remote connections use TLS 1.2, including when a proxy is configured. Servers requiring
TLS 1.3 only are not supported under this setting. Loopback services connect directly. The automatic source
mode for MyMemory guesses English or Chinese; choose a source explicitly for other languages.

Run `ctest --preset dev --output-on-failure` for the tests. Some tests require a loopback socket and a private
D-Bus daemon.

## Platform and privacy

Automatic selection prefers XFixes PRIMARY when an X11 or XWayland server is usable, otherwise it uses
AT-SPI when available. XWayland PRIMARY depends on the compositor and source application sharing selections;
an available XFixes interface does not guarantee coverage. The **Selection source** control allows an
explicit backend, such as **Accessibility events**; restart the app after changing it.

On GNOME Wayland, XWayland can receive a native application's selection without receiving an up-to-date
global mouse position. In that case the icon can appear far from the selection. As an optional workaround,
fully exit the source application and start it through XWayland, for example
`google-chrome --ozone-platform=x11` or `code --ozone-platform=x11`. This changes how that application uses
the desktop; positioning is still subject to the compositor. See the [Chromium Ozone documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/ozone_overview.md).

On a tested GNOME Wayland session, the compositor activated the result window despite the non-focus hint.
Starting SnipLingo with `QT_QPA_PLATFORM=xcb ./build/dev/sniplyngo` used XWayland for its windows and avoided
this in that test. This does not change the selection backend and is not a guarantee for other desktops.
When using AT-SPI, Chromium-based browsers may need accessibility enabled before they expose selected text; see
[Chromium's accessibility documentation](https://www.chromium.org/developers/design-documents/accessibility/).
For example, after fully exiting Chrome, it can be started with `google-chrome --force-renderer-accessibility`.
Enabling accessibility exposes application content to accessibility clients, so decide whether this is
appropriate for your environment. When selection is unavailable, use the text-entry workflow. Screen
capture, OCR, global shortcuts and a tray interface are not implemented.

Selection observation is disabled until enabled by the user; ordinary clipboard changes, keystrokes and the
screen are not continuously monitored. Remote automatic translation needs explicit consent; clicking Translate
is an explicit request to send the entered text to the chosen service. SnipLingo does not deliberately store
translation text or screenshots. Remote services have their own privacy policies. Credentials use the system
Secret Service when available, otherwise they are held for the current session only.

See [module designs](docs/design/README.md), [UI specification](docs/ui-ux-spec.md), and
[security policy](SECURITY.md) for more context.

## License

[GPL-3.0-or-later](LICENSE). Third-party services and models have their own terms and may have costs.
