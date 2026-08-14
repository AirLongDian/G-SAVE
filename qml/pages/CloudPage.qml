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
            width: Math.min(900, parent.width - 68)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 20

            Item { Layout.preferredHeight: 18 }

            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text {
                        text: "云端备份"
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: 30
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "不用配置 Git。登录后，G-SAVE 会为游戏准备独立的私有仓库。"
                        color: Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                }
                Rectangle {
                    Layout.preferredWidth: Math.min(156, tokenState.implicitWidth + 28)
                    Layout.minimumWidth: 104
                    Layout.preferredHeight: 32
                    radius: 16
                    color: form.credentialStored ? Theme.cyanSoft : Theme.panelRaised
                    border.color: form.credentialStored ? Theme.cyan : Theme.border
                    Text {
                        id: tokenState
                        anchors.centerIn: parent
                        width: parent.width - 20
                        text: form.credentialStored ? "✓ Token 已安全保存" : "尚未连接"
                        color: form.credentialStored ? Theme.cyan : Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: 274

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    RowLayout {
                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            radius: 10
                            color: Theme.amberSoft
                            Text {
                                anchors.centerIn: parent
                                text: "☁"
                                color: Theme.amber
                                font.pixelSize: 18
                            }
                        }
                        ColumnLayout {
                            spacing: 2
                            Text {
                                text: "连接你的 Git 服务"
                                color: Theme.text
                                font.family: Theme.displayFont
                                font.pixelSize: 19
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: "自动识别 GitHub、Gitee、GitLab 和 Gitea"
                                color: Theme.faint
                                font.family: Theme.uiFont
                                font.pixelSize: 11
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
                                text: form.credentialStored
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
                            placeholderText: form.credentialStored
                                ? "Token 已保存，无需重复输入"
                                : "粘贴 Token；G-SAVE 会自动读取账号名"
                        }
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: 100 + Math.max(1, repositoryRepeater.count) * 42

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    RowLayout {
                        ColumnLayout {
                            spacing: 3
                            Text {
                                text: "将自动备份的游戏"
                                color: Theme.text
                                font.family: Theme.displayFont
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: form.gameCount > 0
                                    ? "已配置的 " + form.gameCount + " 个游戏全部包含在内，无需再次选择。"
                                    : "还没有配置游戏，请先到游戏支持页面添加。"
                                color: Theme.faint
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            Layout.preferredWidth: Math.min(132, repositoryStatus.implicitWidth + 24)
                            Layout.minimumWidth: 96
                            Layout.preferredHeight: 28
                            radius: 14
                            color: form.allRemoteConfigured ? Theme.cyanSoft : Theme.amberSoft
                            Text {
                                id: repositoryStatus
                                anchors.centerIn: parent
                                width: parent.width - 16
                                text: form.allRemoteConfigured ? "全部已连接" : "连接时自动创建"
                                color: form.allRemoteConfigured ? Theme.cyan : Theme.amber
                                font.family: Theme.uiFont
                                font.pixelSize: 10
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                maximumLineCount: 1
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        Repeater {
                            id: repositoryRepeater
                            model: form.repositories || []
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 9
                                Rectangle {
                                    Layout.preferredWidth: 7
                                    Layout.preferredHeight: 7
                                    radius: 4
                                    color: modelData.configured ? Theme.cyan : Theme.amber
                                }
                                Text {
                                    Layout.preferredWidth: 220
                                    text: modelData.game
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.repository
                                    color: Theme.muted
                                    font.family: Theme.monoFont
                                    font.pixelSize: 11
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    text: !modelData.enabled ? "保护已暂停"
                                        : modelData.configured ? "已连接" : "待创建"
                                    color: !modelData.enabled ? Theme.faint
                                        : modelData.configured ? Theme.cyan : Theme.amber
                                    font.family: Theme.uiFont
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: trigger.currentIndex === 2 ? 158 : 112
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 11
                    Text {
                        text: "什么时候上传"
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppComboBox {
                            id: trigger
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            model: ["每次保存后", "退出游戏后（推荐）", "每隔一段时间", "只在我点击上传时"]
                        }
                        AppTextField {
                            id: interval
                            visible: trigger.currentIndex === 2
                            Layout.preferredWidth: 112
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
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                AppButton {
                    text: "只测试登录"
                    enabled: address.text.trim().length > 0
                        && (token.text.trim().length > 0 || form.credentialStored)
                    onClicked: Backend.testCloudConnection(page.currentForm())
                }
                AppButton {
                    Layout.fillWidth: true
                    text: form.allRemoteConfigured ? "检查全部仓库并保存" : "创建全部仓库并开始备份"
                    kind: "primary"
                    enabled: form.gameCount > 0
                        && address.text.trim().length > 0
                        && (token.text.trim().length > 0 || form.credentialStored)
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
                text: "测试登录不会创建仓库。保存时才会检测或创建私有仓库、验证上传权限，并将 Token 存入 Windows 凭据管理器；Token 不会写入配置或存档历史。"
                wrapMode: Text.WordWrap
                color: Theme.faint
                font.family: Theme.uiFont
                font.pixelSize: 10
            }

            AppButton {
                visible: form.credentialStored
                text: "删除已保存的 Token"
                compact: true
                kind: "danger"
                onClicked: {
                    if (Backend.deleteCredential())
                        page.loadForm()
                }
            }

            Item { Layout.preferredHeight: 26 }
        }
    }
}
