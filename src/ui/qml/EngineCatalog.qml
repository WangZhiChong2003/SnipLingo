import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: catalog
    required property var colors
    signal presetRequested(string type)
    signal customRequested(string label, string url, string model)

    property int category: 0
    property string customKind: ""
    spacing: 10

    readonly property var choices: [
        [
            { type: "libretranslate", name: qsTr("LibreTranslate"),
              detail: qsTr("Connect to a translation service you run. The preset uses this device.") },
            { type: "ollama", name: qsTr("Ollama"),
              detail: qsTr("Use a model already installed in your local Ollama service.") }
        ],
        [
            { type: "mymemory", name: "MyMemory",
              detail: qsTr("Public translation API for short text. 500 UTF-8 bytes per request and 5,000 characters per day anonymously.") }
        ],
        [
            { type: "deepl", name: qsTr("DeepL API"),
              detail: qsTr("Use your DeepL API key. Available usage depends on your plan.") },
            { type: "custom", name: qsTr("OpenAI-compatible"),
              detail: qsTr("Connect to a service with a chat completions API and choose its model.") }
        ],
        [
            { type: "google-free", name: qsTr("Google Translate"), detail: "" },
            { type: "bing-free", name: qsTr("Bing Translator"), detail: "" },
            { type: "youdao", name: qsTr("Youdao"), detail: "" },
            { type: "huoshanweb", name: qsTr("Volcengine Web"), detail: "" }
        ]
    ]

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: [qsTr("On device"), qsTr("Public API"), qsTr("Your API key"), qsTr("Web translation")]
            AppButton {
                required property int index
                required property string modelData
                objectName: "engineCategory" + index
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                colors: catalog.colors
                text: modelData
                highlighted: index === catalog.category
                onClicked: {
                    catalog.category = index;
                    catalog.customKind = "";
                }
            }
        }
    }
    Text {
        objectName: "webEngineWarning"
        Layout.fillWidth: true
        visible: catalog.category === 3
        text: qsTr("Unofficial, no key required. The service may limit requests or stop working.")
        color: catalog.colors.textPrimary
        font.bold: true
        font.pixelSize: catalog.colors.fontOriginal
        wrapMode: Text.Wrap
    }
    Repeater {
        model: catalog.choices[catalog.category]
        RowLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: modelData.name
                    color: catalog.colors.textPrimary
                    font.pixelSize: catalog.colors.fontBody
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: modelData.detail
                    wrapMode: Text.Wrap
                    color: catalog.colors.textSecondary
                    font.pixelSize: catalog.colors.fontOriginal
                }
            }
            AppButton {
                objectName: "addPreset-" + modelData.type
                colors: catalog.colors
                text: modelData.type === "ollama" || modelData.type === "custom"
                      ? qsTr("Configure") : qsTr("Add")
                Accessible.name: qsTr("Set up %1").arg(modelData.name)
                onClicked: {
                    if (modelData.type === "ollama" || modelData.type === "custom")
                        catalog.customKind = modelData.type;
                    else
                        catalog.presetRequested(modelData.type);
                }
            }
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: catalog.customKind !== ""
        spacing: 8
        AppTextField {
            id: serviceUrl
            objectName: "catalogServiceUrl"
            colors: catalog.colors
            Layout.fillWidth: true
            text: catalog.customKind === "ollama" ? "http://127.0.0.1:11434/v1" : ""
            placeholderText: qsTr("Base URL, including the API path")
            Accessible.name: qsTr("Service base URL")
        }
        RowLayout {
            Layout.fillWidth: true
            AppTextField {
                id: modelName
                colors: catalog.colors
                Layout.fillWidth: true
                placeholderText: qsTr("Model name")
                Accessible.name: qsTr("Model name")
            }
            AppTextField {
                id: serviceName
                colors: catalog.colors
                Layout.fillWidth: true
                text: catalog.customKind === "ollama" ? qsTr("Ollama") : ""
                placeholderText: qsTr("Display name")
                Accessible.name: qsTr("Engine display name")
            }
            AppButton {
                colors: catalog.colors
                text: qsTr("Add engine")
                enabled: serviceUrl.text.trim().length > 0
                         && (catalog.customKind !== "ollama" || modelName.text.trim().length > 0)
                onClicked: catalog.customRequested(serviceName.text.trim(), serviceUrl.text.trim(),
                                                    modelName.text.trim())
            }
        }
    }
}
