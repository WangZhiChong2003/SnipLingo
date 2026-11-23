# Linux troubleshooting

[简体中文](linux-integration.zh-CN.md)

## SnipLingo cannot read my selection

- Open **Translate → Test selection** and select a short line in the app where you saw the problem.
  Try the same test in a simple text editor. If only one app fails, it may use a text field that
  does not expose its selection. You can still paste into **Translate**.
- Check **Settings → Selection → Selection source**. **Automatic** prefers an available X11 or
  XWayland primary selection and otherwise tries accessibility events. You can explicitly try
  **Accessibility events** or **X11 / XWayland primary**, then restart and repeat the test.
- If the test succeeds but translations do not appear, check that **Automatic selection** is enabled
  and resumed, a target language is selected, and you have allowed automatic sending if you use an
  online engine. Use **Settings → Engines → Test with sample text** to check the engine separately.

## The translation is in the main window but there is no floating button

On Wayland, you can read the translation on **Translate** in the main window. If you see
**Show the floating result near the selection (XWayland)** under **Settings → Selection**, enable it
and restart SnipLingo. The result can take focus in that mode. On desktops that support the floating
result, **Show translation immediately** opens the card without waiting for you to hover over the button.

With **X11 / XWayland primary**, SnipLingo positions the floating button where the pointer is
after the selection settles. XWayland only tracks the pointer over X11 windows.

On GNOME Shell 42–44, use the optional **SnipLingo Pointer** extension for native Wayland apps.
From a source checkout, run `sh integrations/gnome/install.sh` once and sign back into GNOME if
the installer asks you to. Keep **Show the floating result near the selection (XWayland)** enabled
in SnipLingo. You can then launch Chrome, VS Code and other source apps as usual. The extension
supplies only the cursor position on request over the local session bus. Disable it with GNOME
Extensions at any time.

When there is no reliable pointer position, **X11 / XWayland primary** shows the translation in
the main window. **Accessibility events** can also position the button in apps that provide
screen coordinates for the selected text.

## The tray icon is missing

Some desktops, including a standard GNOME session, may need an extension to show tray icons. You
can reopen the running window by launching SnipLingo again from the terminal or application menu.
[GNOME AppIndicator extension](https://extensions.gnome.org/extension/615/appindicator-support/)

## Reporting a problem

Run `sniplyngo caps --json`, or `./build/release/sniplyngo caps --json` from a source build. Include the
desktop, whether you use Wayland or X11, the app and field you selected text in, and the result of
**Test selection**. Review diagnostic output before sharing it and remove private details.
[Open a bug report](https://github.com/WangZhiChong2003/SnipLingo/issues/new?template=bug_report.yml)
