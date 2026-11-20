<h1 align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/sniplingo-logo-dark.svg">
    <img src="assets/branding/sniplingo-logo.svg" width="360" alt="SnipLingo">
  </picture>
</h1>

<p align="center">
  <a href="#获取-sniplingo">获取 SnipLingo</a> ·
  <a href="#完成第一次翻译">快速开始</a> ·
  <a href="README.md">English</a>
</p>

SnipLingo 是一款处于 pre-alpha 阶段的 Linux 桌面翻译工具。你可以把文字粘贴进主窗口，也可以在支持划词的
应用里选中文字查看译文。翻译引擎由你选择，可以使用本机服务或在线服务。

## 获取 SnipLingo

当前预览版以源码形式提供。克隆仓库，或下载并解压源码，然后在源码目录打开终端。

在 Ubuntu 24.04 上，先安装构建工具、Qt 运行时和划词支持所需的依赖。

```sh
sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev qt6-declarative-dev \
  qt6-wayland qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-templates qml6-module-qtquick-window qml6-module-qtqml-workerscript \
  libatspi2.0-dev libglib2.0-dev libdbus-1-dev libxcb1-dev libxcb-xfixes0-dev libxcb-xinput-dev
```

在源码目录构建并运行。

```sh
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --parallel "$(nproc --ignore=2)"
./build/release/sniplyngo
```

其他发行版需要 C++20 编译器、CMake 3.21+、带 Widgets 的 Qt 6.4+ 和上面列出的 QML 模块。
AT-SPI 和 X11 的开发包分别用于启用对应的划词支持。

## 完成第一次翻译

1. 打开 **Settings → Engines**，添加一个引擎。如果只想试译一小段文字，可以选择 **Public API → MyMemory**。
   它无需 API 密钥，会把文字发送给在线服务。使用本机引擎的方法见[引擎配置指南](docs/engines.zh-CN.md)。
2. 打开 **Translate**，选择目标语言。
3. 输入或粘贴文字，点击 **Translate** 或按 `Ctrl+Enter`。译文会显示在同一窗口。

## 开启划词翻译

1. 在 **Translate** 页的 **Automatic selection** 区域点击 **Test selection**，切到常用应用选几个词。
   此操作在本机检查能否读取选区。
2. 测试成功后，在同一区域点击 **Enable**。如果使用在线引擎，点击 **Allow automatic sending**，
   确认发送目的地再授权。授权后，SnipLingo 读到新选区时就可能向该目的地发送文字。
3. 如果桌面支持浮窗，悬停或点击选区旁的 **Translate** 按钮即可查看。也可以在主窗口看译文。
   **Pause** 暂停监听，**Resume** 等待下一次选区。

划词是否可用取决于桌面和选中文字的应用。如果看不到 **Test selection** 或测试读不到文字，仍可手动粘贴翻译。
在 Wayland 上，你也可以留在主窗口查看译文，或在可用时开启 XWayland 浮窗。 [Linux 排障指南](docs/linux-integration.zh-CN.md)

## 后台运行与退出

点击 **Run in background** 会隐藏窗口并继续运行。如果桌面显示托盘图标，可用图标菜单中的 **Open SnipLingo**
唤回窗口，或用 **Quit** 退出。也可以再次运行 `./build/release/sniplyngo` 唤回窗口。已安装到应用菜单时，
可从菜单再次打开 **SnipLingo**。点击主窗口右上角的 **×** 即可退出。

## 隐私与当前功能

划词监听需要主动开启。临时选区测试只在本机读取文字，随后丢弃。手动翻译时，点击翻译才会将文字交给所选引擎。
凭据优先保存在系统密钥环中。如果无法持久保存凭据，退出后可能需要重新输入密钥。

当前预览版提供手动翻译和划词翻译。区域截图、OCR、全局快捷键和界面语言包尚未提供。

[贡献指南](CONTRIBUTING.md) · [安全政策](SECURITY.md) · [GPL-3.0-or-later](LICENSE)
