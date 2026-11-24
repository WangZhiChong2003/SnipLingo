# Translation engines

[简体中文](engines.zh-CN.md)

Open **Settings → Engines** to add an engine. Once you choose a target language on **Translate**,
**Test with sample text** sends a built-in example to the selected engine to check that it works.

| Category | Setup |
| --- | --- |
| **On device** | Start a local LibreTranslate service, or start Ollama with a downloaded model. For Ollama, enter the local API URL and model name. |
| **Public API** | MyMemory accepts short requests without a key. Check the provider's [usage limits](https://mymemory.translated.net/doc/usagelimits.php) if a request is rejected. |
| **Your API key** | For DeepL, enter your key and the [API address for your account](https://developers.deepl.com/docs/getting-started/auth). For an OpenAI-compatible engine, enter the service's API URL, model and key if required. |
| **Web translation** | Google, Bing, Youdao and Volcengine Web use unofficial endpoints. Availability depends on the service and your network. |

An online engine receives your submitted text. SnipLingo remembers automatic sending permission
for each destination. If a new engine needs permission, choose **Allow and add engine** to save it
or **Cancel** to leave your engines as they are. Web translation engines show an unofficial-endpoint
notice in the catalog and in the permission dialog. If you run an engine locally, review its own
settings for how it handles text.

If a service needs a proxy, open **Settings → Network**. Services on loopback addresses connect
directly. Run `sniplyngo --help` for terminal commands, or
`./build/release/sniplyngo --help` from the source directory after building.
