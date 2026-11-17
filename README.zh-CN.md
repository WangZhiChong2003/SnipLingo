# SnipLingo

SnipLingo 是一款实验性的 Linux 桌面翻译应用。可以在窗口中输入或粘贴文本进行翻译；在受支持的
桌面和应用中，也可以由用户明确启用自动选区翻译。项目仍处于 pre-alpha 阶段，桌面集成和公共翻译
服务均有使用限制。

[English](README.md) · [设计文档](docs/DESIGN.md) · [Linux 集成](docs/linux-integration.md) · [贡献指南](CONTRIBUTING.md)

## 构建与运行

需要 C++20 编译器、CMake 3.21 及以上、Ninja、Qt 6（Core、Network、D-Bus、GUI、QML、Quick，
运行测试还需要 Test）和 Qt Quick Controls 运行时。安装 AT-SPI 和 X11 开发包可启用相应的可选选区后端。
界面字符串已预留本地化支持，但目前尚未提供中文等界面翻译包。

```sh
cmake --preset dev
cmake --build --preset dev --parallel $(( $(nproc) - 2 ))
./build/dev/sniplyngo
```

在窗口中选择翻译引擎和目标语言，输入或粘贴文本，然后点击 **Translate**。如需自动翻译选区，
启用 **Translate selected text**；自动发送到远程服务还需要单独授权。可在窗口中检查监听状态，
随时暂停或关闭。默认在选区附近显示小图标，鼠标移上后展开译文；开启 **Show translation
immediately** 可直接弹出译文。选区读取完成后即开始翻译，远程发送仍受授权控制。点击 **Hide**
可让应用在后台继续运行，再次启动 SnipLingo 可重新打开窗口；关闭窗口则会退出应用。展开的译文框会
请求焦点，切换到其他窗口时关闭；焦点行为可能受桌面合成器限制。

Google 预设使用非官方接口，可能失效或被限流。MyMemory 的匿名额度有限，每次最多接受 500 个
UTF-8 字节。LibreTranslate 需要用户自行运行服务；预设地址为 `http://127.0.0.1:5000`，可在窗口中
修改地址并按需填写密钥。窗口也支持配置 OpenAI 兼容服务，包括本地服务。Bing 的旧非官方认证接口
在 2026 年 11 月返回 HTTP 404，因此不再出现在窗口的新建预设中。任何公共服务都不保证永久免费或可用。

排查代理时：

```sh
./build/dev/sniplyngo proxy manual http://127.0.0.1:7897
./build/dev/sniplyngo engine target zh-Hans
./build/dev/sniplyngo engine test google-free
```

`engine test` 会向指定服务发送简短的测试文本（目标语言为英语时使用 `你好`，否则使用 `hello`）；`--all` 会测试内置候选项和已配置的引擎。远程连接使用
TLS 1.2，代理连接同样适用；仅支持 TLS 1.3 的服务在此设置下不可用。本机回环地址直接连接，不经过
代理。MyMemory 的自动源语言只会猜测中英文，其他语言请手动指定。

运行 `ctest --preset dev --output-on-failure` 执行测试。部分测试需要本机回环端口和临时 D-Bus 服务。

## 平台与隐私

自动选区优先使用可用的 X11 或 XWayland XFixes PRIMARY，否则使用可用的 AT-SPI。XWayland 能否
获取选区取决于合成器和源应用是否共享选区；接口可用不代表所有应用都受支持。可在 **Selection
source** 中手动选择来源，例如 **Accessibility events**；修改后需重启应用。

在 GNOME Wayland 下，XWayland 有时能收到原生应用的选区，却无法取得实时的全局鼠标坐标，因此
小图标可能离选区较远。一个可选的临时办法是完全退出源应用后，让它通过 XWayland 启动，例如
`google-chrome --ozone-platform=x11` 或 `code --ozone-platform=x11`。这会改变该应用使用桌面的
方式，位置仍取决于合成器；参见 [Chromium Ozone 文档](https://chromium.googlesource.com/chromium/src/+/main/docs/ozone_overview.md)。

在一次 GNOME Wayland 实测中，即使结果窗口请求不获得焦点，合成器仍会将其激活。使用
`QT_QPA_PLATFORM=xcb ./build/dev/sniplyngo` 让窗口通过 XWayland 运行，在该次测试中避免了
这个问题；该设置不会更改选区来源，也不保证适用于其他桌面。使用 AT-SPI 时，Chromium 系浏览器可能需要启用辅助
功能才能暴露选区，参见 [Chromium 辅助功能说明](https://www.chromium.org/developers/design-documents/accessibility/)。
例如，完全退出 Chrome 后可使用 `google-chrome --force-renderer-accessibility` 启动。
启用辅助功能可能使其他辅助功能客户端访问应用内容，请根据自己的环境决定是否启用。无法获取选区时，
请使用文本输入功能。截图、OCR、全局快捷键和系统托盘尚未实现。

只有用户启用后才观察选区；不会持续监听普通剪贴板、键盘或屏幕。自动发送到远程服务需要明确授权；
点击 Translate 则表示主动请求向所选服务发送输入文本。SnipLingo 不会主动保存译文或截图；远程服务
适用其自身的隐私政策。凭据优先使用系统 Secret Service，不可用时仅在当前会话内保存。

更多信息见[模块设计](docs/design/README.md)、[界面规范](docs/ui-ux-spec.md)和[安全政策](SECURITY.md)。

## 许可

[GPL-3.0-or-later](LICENSE)。第三方服务和模型适用各自的条款，也可能产生费用。
