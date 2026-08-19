import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GSave
import "../components"

Item {
    id: page

    property string query: ""

    signal openGame(int gameIndex)

    function matches(entry) {
        if (page.query.length === 0) return true
        const needle = page.query.toLowerCase()
        const name = (entry.name || "").toLowerCase()
        const id = (entry.id || "").toLowerCase()
        const process = (entry.process || "").toLowerCase()
        return name.indexOf(needle) >= 0
            || id.indexOf(needle) >= 0
            || process.indexOf(needle) >= 0
    }

    function filtered(kind) {
        const result = []
        const library = Backend.library
        for (let index = 0; index < library.length; ++index) {
            const entry = library[index]
            if (entry.kind === kind && page.matches(entry)) result.push(entry)
        }
        return result
    }

    readonly property var installedEntries: {
        Backend.library
        page.query
        return page.filtered("installed")
    }
    readonly property var availableEntries: {
        Backend.library
        page.query
        return page.filtered("available")
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: page.width
            spacing: 20

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 34
                Layout.rightMargin: 34
                Layout.topMargin: 26
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    ColumnLayout {
                        spacing: 3
                        Text {
                            text: "游戏库"
                            color: Theme.text
                            font.family: Theme.displayFont
                            font.pixelSize: 30
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: Backend.games.length === 0
                                ? "选择一个游戏开始保护存档"
                                : "正在保护 " + Backend.games.length + " 个游戏"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                        }
                    }

                    Item { Layout.fillWidth: true }

                    AppTextField {
                        id: search
                        Layout.preferredWidth: 260
                        placeholderText: "搜索游戏"
                        text: page.query
                        onTextChanged: page.query = text
                    }

                    AppButton {
                        text: "导入支持包 ZIP"
                        onClicked: Backend.importPackage()
                    }

                    AppButton {
                        text: Backend.indexLoading ? "获取中…" : "刷新在线清单"
                        enabled: !Backend.indexLoading
                        onClicked: Backend.refreshIndex()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: Backend.indexStatus.length > 0
                    text: Backend.indexStatus
                    color: Theme.muted
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 34
                Layout.rightMargin: 34
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "已安装"
                        color: Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: page.installedEntries.length + " 个游戏"
                        color: Theme.faint
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 16

                    Repeater {
                        model: page.installedEntries
                        delegate: GameCard {
                            required property var modelData
                            entry: modelData
                            onActivated: page.openGame(modelData.gameIndex)
                        }
                    }

                    AddGameCard {
                        visible: page.query.length === 0
                        onActivated: Backend.installGenericPackage()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 34
                Layout.rightMargin: 34
                Layout.bottomMargin: 30
                spacing: 12
                visible: page.availableEntries.length > 0

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "可安装"
                        color: Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: page.availableEntries.length + " 个支持包"
                        color: Theme.faint
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 16

                    Repeater {
                        model: page.availableEntries
                        delegate: GameCard {
                            required property var modelData
                            entry: modelData
                            onActivated: {
                                if (modelData.source === "online") {
                                    Backend.installFromIndex(modelData.id)
                                } else {
                                    Backend.installPackage(modelData.packageIndex)
                                }
                            }
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                visible: page.installedEntries.length === 0
                    && page.availableEntries.length === 0

                Text {
                    anchors.centerIn: parent
                    text: page.query.length > 0
                        ? "没有匹配「" + page.query + "」的游戏"
                        : "还没有任何支持包\n导入支持包 ZIP，或刷新在线清单"
                    color: Theme.muted
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 1.5
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                }
            }
        }
    }
}
