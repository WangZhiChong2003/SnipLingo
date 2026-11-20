# 翻译引擎配置

[English](engines.md)

打开 **Settings → Engines** 添加引擎。在 **Translate** 选择目标语言后，可以点击 **Test with sample text**，
用一条内置示例测试所选引擎。

| 分类 | 如何配置 |
| --- | --- |
| **On device** | 先在本机启动 LibreTranslate，或让 Ollama 运行已下载的模型。配置 Ollama 时填写本机 API 地址和模型名。 |
| **Public API** | MyMemory 无需密钥，可翻译短文本。如果服务拒绝请求，可以检查[用量限制](https://mymemory.translated.net/doc/usagelimits.php)。 |
| **Your API key** | 使用 DeepL 时，填入密钥及[账号对应的 API 地址](https://developers.deepl.com/docs/getting-started/auth)。使用 OpenAI 兼容服务时，填入 API 地址、模型和服务所需的密钥。 |
| **Web demos** | Google、Bing、有道和火山网页翻译使用非官方接口，可用性取决于服务和网络。 |

在线引擎会收到你提交的文字。为划词翻译授权自动发送时，请确认发送目的地。若使用本机引擎，可在该服务的设置里确认文字如何处理。

如需代理，可打开 **Settings → Network**。本机回环地址会直接连接。终端命令见 `sniplyngo --help`；
从源码构建后，可在源码目录运行 `./build/release/sniplyngo --help`。
