import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GSave
import "../components"

// "支持配置" tab. Paths and ignore rules are read-only: a dedicated package
// detects them itself, and a wrong manual value would silently version the wrong
// directory. Commit settings are staged, not written, because Core reads the
// configuration once at startup.
Item {
    id: tab

    property int gameIndex: -1
    property int saveIndex: 0
    property var detail: ({})
    property var repositoryState: ({})

    signal saveIndexRequested(int value)
    signal reloadRequested()

    readonly property var commit: detail.commit === undefined ? ({}) : detail.commit
    readonly property var sync: detail.sync === undefined ? ({}) : detail.sync
    readonly property var saves: detail.saves === undefined ? [] : detail.saves

    function stage(field, value) {
        const payload = {}
        payload[field] = value
        Backend.stageCommitPolicy(tab.gameIndex, payload)
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: tab.width
            spacing: 16

            // Game
            Panel {
                Layout.fillWidth: true
                Layout.leftMargin: 30
                Layout.rightMargin: 30
                Layout.topMargin: 18
                Layout.preferredHeight: gameColumn.implicitHeight + 36

                ColumnLayout {
                    id: gameColumn
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "游戏"
                            color: Theme.text
                            font.family: Theme.uiFont
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        AppButton {
                            compact: true
                            visible: tab.detail.generic === false
                                && tab.detail.packageIndex !== undefined
                                && tab.detail.packageIndex >= 0
                            text: "重新检测路径"
                            onClicked: {
                                Backend.installPackage(tab.detail.packageIndex)
                                tab.reloadRequested()
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 20
                        rowSpacing: 9

                        Text {
                            text: "程序路径"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            text: tab.detail.processPath === undefined
                                ? "" : tab.detail.processPath
                            color: Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                        }

                        Text {
                            text: "进程名"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            text: tab.detail.process === undefined ? "" : tab.detail.process
                            color: Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }

                        Text {
                            text: "解析脚本"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            text: tab.detail.parser === undefined ? "" : tab.detail.parser
                            color: Theme.faint
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: tab.detail.generic === false
                        text: "专用支持包自动检测这些位置，因此不提供手动选择。游戏换盘或重装后点击「重新检测路径」。"
                        color: Theme.faint
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Saves
            Panel {
                Layout.fillWidth: true
                Layout.leftMargin: 30
                Layout.rightMargin: 30
                Layout.preferredHeight: saveColumn.implicitHeight + 36

                ColumnLayout {
                    id: saveColumn
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    Text {
                        text: "存档位置"
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: tab.saves
                        delegate: ColumnLayout {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                Layout.fillWidth: true
                                text: modelData.path
                                color: Theme.text
                                font.family: Theme.monoFont
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: modelData.includeGlobs.length > 0
                                text: "监听：" + modelData.includeGlobs.join("  ")
                                color: Theme.muted
                                font.family: Theme.monoFont
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: modelData.excludeGlobs.length > 0
                                text: "忽略：" + modelData.excludeGlobs.join("  ")
                                color: Theme.faint
                                font.family: Theme.monoFont
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            // Commit policy
            Panel {
                Layout.fillWidth: true
                Layout.leftMargin: 30
                Layout.rightMargin: 30
                Layout.preferredHeight: commitColumn.implicitHeight + 36

                ColumnLayout {
                    id: commitColumn
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "什么时候保存存档"
                            color: Theme.text
                            font.family: Theme.uiFont
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: tab.commit.pending === true
                            text: "未保存"
                            color: Theme.amber
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 20
                        rowSpacing: 10

                        Text {
                            text: "提交方式"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        AppComboBox {
                            Layout.preferredWidth: 300
                            model: [
                                "安静期：游戏写完后等一会儿再保存",
                                "周期：持续写入时按最长间隔保存",
                                "退出时：只在关闭游戏时保存",
                                "组合：安静期 + 最长间隔 + 退出时"
                            ]
                            currentIndex: tab.commit.strategy === undefined
                                ? 3 : tab.commit.strategy
                            onActivated: function (index) { tab.stage("strategy", index) }
                        }

                        Text {
                            text: "安静时间（秒）"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        SpinBox {
                            Layout.preferredWidth: 140
                            from: 1
                            to: 3600
                            editable: true
                            value: tab.commit.quietSeconds === undefined
                                || tab.commit.quietSeconds <= 0
                                ? 5 : tab.commit.quietSeconds
                            onValueModified: tab.stage("quietSeconds", value)
                        }

                        Text {
                            text: "最长间隔（秒）"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        SpinBox {
                            Layout.preferredWidth: 140
                            from: 1
                            to: 86400
                            editable: true
                            value: tab.commit.maxIntervalSeconds === undefined
                                || tab.commit.maxIntervalSeconds <= 0
                                ? 300 : tab.commit.maxIntervalSeconds
                            onValueModified: tab.stage("maxIntervalSeconds", value)
                        }

                        Text {
                            text: "退出游戏时保存"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        Switch {
                            checked: tab.commit.commitOnExit === true
                            onToggled: tab.stage("commitOnExit", checked)
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "存档保护读取配置的时机是启动，因此改动需要保存并重启服务后才生效。"
                        color: Theme.faint
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Repository state
            Panel {
                Layout.fillWidth: true
                Layout.leftMargin: 30
                Layout.rightMargin: 30
                Layout.bottomMargin: 26
                Layout.preferredHeight: stateColumn.implicitHeight + 36

                ColumnLayout {
                    id: stateColumn
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "当前状态"
                            color: Theme.text
                            font.family: Theme.uiFont
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        AppComboBox {
                            visible: tab.saves.length > 1
                            Layout.preferredWidth: 200
                            model: {
                                const names = []
                                for (let i = 0; i < tab.saves.length; ++i) {
                                    names.push("存档位置 " + (i + 1))
                                }
                                return names
                            }
                            currentIndex: tab.saveIndex
                            onActivated: function (index) { tab.saveIndexRequested(index) }
                        }
                        AppButton {
                            compact: true
                            text: "刷新"
                            onClicked: tab.reloadRequested()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: tab.repositoryState.error !== undefined
                        text: tab.repositoryState.error === undefined
                            ? "" : tab.repositoryState.error
                        color: Theme.red
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        visible: tab.repositoryState.error === undefined
                        columns: 2
                        columnSpacing: 20
                        rowSpacing: 9

                        Text {
                            text: "当前存档线"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            text: tab.repositoryState.branch === undefined
                                ? "" : tab.repositoryState.branch
                            color: Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                        }

                        Text {
                            text: "未保存的变化"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            text: tab.repositoryState.dirty === true ? "有" : "没有"
                            color: tab.repositoryState.dirty === true
                                ? Theme.amber : Theme.cyan
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }

                        Text {
                            text: "与云端的差异"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                if (tab.repositoryState.remoteUrl === undefined
                                    || tab.repositoryState.remoteUrl.length === 0) {
                                    return "未配置云端"
                                }
                                const ahead = tab.repositoryState.ahead || 0
                                const behind = tab.repositoryState.behind || 0
                                if (ahead === 0 && behind === 0) return "已同步"
                                let parts = []
                                if (ahead > 0) parts.push("本地多 " + ahead + " 个版本")
                                if (behind > 0) parts.push("云端多 " + behind + " 个版本")
                                return parts.join("，")
                            }
                            color: Theme.text
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: "最后一次保存"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            text: tab.repositoryState.lastCommitAt === undefined
                                ? "还没有版本"
                                : tab.repositoryState.lastCommitAt + "  ·  "
                                  + (tab.repositoryState.lastReason || "")
                            color: Theme.text
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "云端账号在设置页配置，对全部游戏生效。"
                        color: Theme.faint
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
