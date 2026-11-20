<h1 align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/sniplingo-logo-dark.svg">
    <img src="assets/branding/sniplingo-logo.svg" width="360" alt="SnipLingo">
  </picture>
</h1>

<p align="center">
  <a href="#get-sniplingo">Get SnipLingo</a> ·
  <a href="#translate-your-first-text">Quick start</a> ·
  <a href="README.zh-CN.md">简体中文</a>
</p>

SnipLingo is a pre-alpha Linux desktop translator. Paste text into its main window or select text in a
supported app to see a translation. You choose the translation engine, which can run on your computer
or use an online service.

## Get SnipLingo

The current preview is available as source code. Clone this repository or download and extract the
source, then open a terminal in the source directory.

On Ubuntu 24.04, install the build tools, Qt runtime and selection backend dependencies.

```sh
sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev qt6-declarative-dev \
  qt6-wayland qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-templates qml6-module-qtquick-window qml6-module-qtqml-workerscript \
  libqt6svg6 libatspi2.0-dev libglib2.0-dev libdbus-1-dev libxcb1-dev libxcb-xfixes0-dev libxcb-xinput-dev
```

Build and run from the source directory.

```sh
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --parallel "$(nproc --ignore=2)"
./build/release/sniplyngo
```

Other distributions need a C++20 compiler, CMake 3.21+ and Qt 6.4+ with Widgets and the QML modules
listed above, plus the Qt SVG library whose image-format plugin renders the result-card icons. The
AT-SPI and X11 development packages enable automatic selection support for those backends.

## Translate your first text

1. Open **Settings → Engines** and add an engine. For short text, **Public API → MyMemory** works
   without an API key and sends the text to an online service. To use a local engine, see the [engine guide](docs/engines.md).
2. Open **Translate** and pick a target language.
3. Type or paste some text and click **Translate** or press `Ctrl+Enter`. The translation appears in
   the same window.

## Translate selected text

1. In the **Automatic selection** section of **Translate**, click **Test selection**. Switch to
   the app you want to use and select a few words. This checks selection access locally.
2. Click **Enable** in the same section. For an online engine, click **Allow automatic sending**
   and review the destination before granting permission. SnipLingo can then send text to that
   destination as soon as it reads a new selection.
3. Where floating results are supported, hover over or click the **Translate** button near the
   selection. You can also read the result in the main window. **Pause** stops observation, and
   **Resume** waits for the next selection.

Selection support depends on your desktop and the app you select text in. If **Test selection** is
missing or finds no text, you can still translate by pasting. On Wayland, you can also use the main
window or enable the optional XWayland floating result where available. [Linux troubleshooting](docs/linux-integration.md)

## Running in the background

**Run in background** hides the window and leaves SnipLingo running. Use the tray menu's
**Open SnipLingo** or **Quit** actions on desktops that show the tray icon. You can also run
`./build/release/sniplyngo` again to reopen the existing window, or launch **SnipLingo** from your
app menu if it is installed. Click **×** on the main window to quit.

## Privacy and current limits

Automatic selection starts when you enable it. A temporary selection test reads locally and
discards the text. Manual translation sends text to the configured engine when you submit it.
Credentials use the system keyring where available. Without persistent credential storage, you
may need to enter your key again after quitting.

This preview supports manual translation and automatic selection. Region capture, OCR, global
shortcuts and translated interface catalogs are outside its current feature set.

[Contributing](CONTRIBUTING.md) · [Security policy](SECURITY.md) · [GPL-3.0-or-later](LICENSE)
