import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GSave
import "../components"

Item {
    id: page
    property var form: ({})

    function loadForm() {
        form = Backend.cloudSettings()
        address.text = form.serviceAddress || ""
        token.text = ""
        trigger.currentIndex = form.trigger === undefined ? 1 : form.trigger
        interval.text = String(form.interval || 300)
        indexField.text = Backend.indexUrl
    }

    function currentForm() {
        return {
            serviceAddress: address.text,
            token: token.text,
            trigger: trigger.currentIndex,
            interval: interval.text
        }
    }

    Component.onCompleted: loadForm()

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: Math.min(880, page.width - 68)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 18

            Item { Layout.preferredHeight: 20 }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: "设置"
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: "存档保护、云端账号和支持包来源。云端设置对全部已配置游戏生效。"
                    color: Theme.muted
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }

            // Startup and service
            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: startupColumn.implicitHeight + 40

                ColumnLayout {
                    id: startupColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 14

                    Text {
                        text: "存档保护服务"
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 5
                            color: Backend.coreRunning ? Theme.cyan : Theme.faint
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: Backend.coreRunning ? "正在运行" : "已停止"
                                color: Theme.text
                                font.family: Theme.uiFont
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: Backend.coreRunning
                                    ? "游戏没运行时不会打开存档目录，也不做任何轮询。"
                                    : "启动后才会在游戏运行时保护存档。"
                                color: Theme.faint
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                            }
                        }

                        AppButton {
                            compact: true
                            enabled: !Backend.coreBusy
                            text: Backend.coreRunning ? "停止" : "启动"
                            onClicked: Backend.toggleCore()
                        }
                        AppButton {
                            compact: true
                            enabled: !Backend.coreBusy && Backend.coreRunning
                            text: "重启"
                            onClicked: Backend.restartCore()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.border
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: "登录 Windows 后自动启动"
                                color: Theme.text
                                font.family: Theme.uiFont
                                font.pixelSize: 14
                            }
                            Text {
                                text: "推荐开启。以所需权限启动，无窗口、无托盘、不写常驻日志。"
                                color: Theme.faint
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                        Switch {
                            checked: Backend.autostartEnabled
                            onToggled: Backend.setAutostart(checked)
                        }
                    }
                }
            }

            // Cloud account
            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: cloudColumn.implicitHeight + 40

                ColumnLayout {
                    id: cloudColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 13

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: "云端账号"
                                color: Theme.text
                                font.family: Theme.displayFont
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: "自动识别 GitHub、Gitee、GitLab 和 Gitea，并按游戏创建私有仓库"
                                color: Theme.faint
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: Math.min(150, tokenState.implicitWidth + 26)
                            Layout.minimumWidth: 100
                            Layout.preferredHeight: 30
                            radius: 15
                            color: page.form.credentialStored ? Theme.cyanSoft : Theme.panelRaised
                            border.color: page.form.credentialStored ? Theme.cyan : Theme.border
                            Text {
                                id: tokenState
                                anchors.centerIn: parent
                                text: page.form.credentialStored ? "✓ Token 已保存" : "尚未连接"
                                color: page.form.credentialStored ? Theme.cyan : Theme.muted
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: "服务地址"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 11
                        }
                        AppTextField {
                            id: address
                            Layout.fillWidth: true
                            placeholderText: "例如 github.com、gitee.com 或你的 Git 服务地址"
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        RowLayout {
                            Text {
                                text: "Token"
                                color: Theme.muted
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: page.form.credentialStored
                                    ? "留空可继续使用已保存的 Token"
                                    : "在服务的个人设置中创建访问令牌"
                                color: Theme.faint
                                font.family: Theme.uiFont
                                font.pixelSize: 10
                            }
                        }
                        AppTextField {
                            id: token
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            placeholderText: page.form.credentialStored
                                ? "Token 已保存，无需重复输入"
                                : "粘贴 Token；G-SAVE 会自动读取账号名"
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: "什么时候上传"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 11
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            AppComboBox {
                                id: trigger
                                Layout.fillWidth: true
                                model: ["每次保存后", "退出游戏后（推荐）", "每隔一段时间",
                                        "只在我点击上传时"]
                            }
                            AppTextField {
                                id: interval
                                visible: trigger.currentIndex === 2
                                Layout.preferredWidth: 110
                                validator: IntValidator { bottom: 1 }
                            }
                            Text {
                                visible: trigger.currentIndex === 2
                                text: "秒"
                                color: Theme.muted
                                font.family: Theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AppButton {
                            text: "只测试登录"
                            enabled: address.text.trim().length > 0
                                && (token.text.trim().length > 0
                                    || page.form.credentialStored)
                            onClicked: Backend.testCloudConnection(page.currentForm())
                        }
                        AppButton {
                            Layout.fillWidth: true
                            kind: "primary"
                            text: page.form.allRemoteConfigured
                                ? "检查全部仓库并保存" : "创建全部仓库并开始备份"
                            enabled: page.form.gameCount > 0
                                && address.text.trim().length > 0
                                && (token.text.trim().length > 0
                                    || page.form.credentialStored)
                            onClicked: {
                                if (Backend.saveCloudSettings(page.currentForm())) {
                                    token.text = ""
                                    page.loadForm()
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "测试登录不会创建仓库。保存时才会检测或创建私有仓库、验证上传权限，"
                            + "并将 Token 存入 Windows 凭据管理器；Token 不会写入配置或存档历史。"
                        color: Theme.faint
                        font.family: Theme.uiFont
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    AppButton {
                        visible: page.form.credentialStored
                        compact: true
                        kind: "danger"
                        text: "删除已保存的 Token"
                        onClicked: {
                            if (Backend.deleteCredential()) page.loadForm()
                        }
                    }
                }
            }

            // Package index
            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: indexColumn.implicitHeight + 40

                ColumnLayout {
                    id: indexColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    Text {
                        text: "支持包来源"
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: "在线清单地址"
                        color: Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AppTextField {
                            id: indexField
                            Layout.fillWidth: true
                            text: Backend.indexUrl
                        }
                        AppButton {
                            compact: true
                            text: Backend.indexLoading ? "获取中…" : "刷新"
                            enabled: !Backend.indexLoading
                            onClicked: {
                                Backend.indexUrl = indexField.text
                                Backend.refreshIndex()
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: Backend.indexStatus.length > 0
                        text: Backend.indexStatus
                        color: Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "下载的支持包会先校验大小和 SHA-256，校验不通过不会安装。"
                            + "也可以在游戏库直接导入自制的支持包 ZIP。"
                        color: Theme.faint
                        font.family: Theme.uiFont
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Paths
            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: pathColumn.implicitHeight + 40

                ColumnLayout {
                    id: pathColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 11

                    Text {
                        text: "文件位置"
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: [
                            { label: "配置文件", value: Backend.configPath },
                            { label: "支持包目录", value: Backend.packageRoot },
                            { label: "后台程序", value: Backend.corePath }
                        ]
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: modelData.label
                                color: Theme.muted
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.value
                                color: Theme.text
                                font.family: Theme.monoFont
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 26 }
        }
    }
}
