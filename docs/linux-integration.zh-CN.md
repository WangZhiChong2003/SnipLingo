# Linux 排障指南

[English](linux-integration.md)

## SnipLingo 读不到我选中的文字

- 打开 **Translate → Test selection**，到出现问题的应用中选一行短文字，再用普通文本编辑器做同样的测试。
  如果只在某个应用里失败，该应用的文本控件可能没有提供选区内容。你仍然可以复制文字，粘贴到 **Translate**。
- 检查 **Settings → Selection → Selection source**。**Automatic** 优先使用可用的 X11 或 XWayland 主选区，
  然后尝试无障碍事件。可以改选 **Accessibility events** 或 **X11 / XWayland primary**，重启后再测试。
- 如果选区测试成功但没有译文，检查 **Automatic selection** 是否已开启并恢复监听、是否选了目标语言，以及在线引擎
  是否获得自动发送授权。也可以在 **Settings → Engines → Test with sample text** 单独测试引擎。

## 主窗口有译文，但选区旁看不到浮窗按钮

在 Wayland 上，可以到主窗口的 **Translate** 页看译文。如果 **Settings → Selection** 中有
**Show the floating result near the selection (XWayland)**，可以开启它并重启 SnipLingo。该模式下浮窗可能会
取得焦点。支持浮窗的桌面还可以开启 **Show translation immediately**，选中文字后直接展示译文卡片。

## 找不到托盘图标

一些桌面（包括默认 GNOME 会话）可能需要扩展才能显示托盘图标。可以从终端或应用菜单再次启动 SnipLingo 来唤回窗口。
[GNOME AppIndicator 扩展](https://extensions.gnome.org/extension/615/appindicator-support/)

## 报告问题

运行 `sniplyngo caps --json`。如果是从源码构建，在源码目录运行 `./build/release/sniplyngo caps --json`。
报告里可附上桌面环境、使用的是 Wayland 还是 X11、选中文字的应用和控件，以及 **Test selection** 的结果。
请先检查诊断输出并去掉私人信息。 [提交问题](https://github.com/WangZhiChong2003/SnipLingo/issues/new?template=bug_report.yml)
