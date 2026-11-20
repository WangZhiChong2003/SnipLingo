import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: about
    required property var colors
    readonly property string email: "wangzhichong47@gmail.com"
    readonly property string repositoryAddress: "github.com/WangZhiChong2003/SnipLingo"

    spacing: colors.spacing

    component DetailLabel: Text {
        Layout.fillWidth: true
        color: about.colors.textSecondary
        font.pixelSize: about.colors.fontOriginal
        wrapMode: Text.Wrap
    }

    component DetailValue: Text {
        Layout.fillWidth: true
        color: about.colors.textPrimary
        font.pixelSize: about.colors.fontBody
        font.weight: Font.DemiBold
        wrapMode: Text.Wrap
    }

    component ExternalLink: AppButton {
        id: link
        required property string label
        required property string detail

        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: linkContent.implicitHeight + topPadding + bottomPadding
        colors: about.colors
        flat: true
        padding: 10
        verticalPadding: 8
        Accessible.name: label
        Accessible.description: detail

        contentItem: RowLayout {
            id: linkContent
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                DetailValue { text: link.label }
                DetailLabel { text: link.detail }
            }

            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: about.colors.radiusControl
                color: about.colors.accentSoft
                Text {
                    anchors.centerIn: parent
                    text: "↗"
                    color: about.colors.accent
                    font.pixelSize: 17
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: intro.implicitHeight + 32
        radius: about.colors.radiusWindow
        border.color: about.colors.border
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: about.colors.accentSoft }
            GradientStop { position: 1; color: about.colors.surface }
        }

        RowLayout {
            id: intro
            anchors.fill: parent
            anchors.margins: 16
            spacing: 18

            Image {
                Layout.preferredWidth: 84
                Layout.preferredHeight: 84
                // Keep the default mipmap mode for the texture shared with the title bar.
                source: "qrc:/branding/sniplingo-icon.png"
                fillMode: Image.PreserveAspectFit
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: "SnipLingo"
                    color: about.colors.textPrimary
                    font.pixelSize: 32
                    font.bold: true
                }
                DetailLabel {
                    text: qsTr("An open-source Linux desktop app for translating selected or pasted text.")
                    font.pixelSize: about.colors.fontBody
                }
                Rectangle {
                    implicitWidth: version.implicitWidth + 20
                    implicitHeight: version.implicitHeight + 8
                    radius: height / 2
                    color: about.colors.surface
                    border.color: about.colors.border

                    Text {
                        id: version
                        anchors.centerIn: parent
                        objectName: "aboutVersion"
                        text: qsTr("Version %1").arg(Qt.application.version)
                        color: about.colors.textPrimary
                        font.pixelSize: about.colors.fontMeta
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: about.colors.spacing

        Rectangle {
            Layout.preferredWidth: (about.width - about.colors.spacing) / 3
            Layout.fillHeight: true
            implicitHeight: project.implicitHeight + 2 * about.colors.padding
            radius: about.colors.radiusWindow
            color: about.colors.surface
            border.color: about.colors.border

            ColumnLayout {
                id: project
                anchors.margins: about.colors.padding
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    DetailLabel { text: qsTr("Author") }
                    DetailValue {
                        objectName: "aboutAuthor"
                        text: "WangZhiChong"
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    DetailLabel { text: qsTr("License") }
                    DetailValue { text: "GPL-3.0-or-later" }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            implicitHeight: links.implicitHeight + 16
            radius: about.colors.radiusWindow
            color: about.colors.surface
            border.color: about.colors.border

            ColumnLayout {
                id: links
                anchors.margins: 8
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3

                ExternalLink {
                    objectName: "aboutRepository"
                    label: qsTr("GitHub repository")
                    detail: about.repositoryAddress
                    text: "https://" + about.repositoryAddress
                    onClicked: Qt.openUrlExternally(text)
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    implicitHeight: 1
                    color: about.colors.border
                }

                ExternalLink {
                    objectName: "aboutEmail"
                    label: qsTr("Email the author")
                    detail: about.email
                    text: about.email
                    onClicked: Qt.openUrlExternally("mailto:" + text)
                }
            }
        }
    }
}
