import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GSave
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 34
        spacing: 22

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text {
                    text: "运行设置"
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: "管理后台存档保护。界面关闭后，只有极简 Core 在等待游戏事件。"
                    color: Theme.muted
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }
            }
            AppButton { text: "刷新状态"; onClicked: Backend.refreshService() }
        }

        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 164
            color: Backend.coreRunning ? Theme.cyanSoft : Theme.panel
            border.color: Backend.coreRunning ? Theme.cyan : Theme.border
            RowLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 20
                Rectangle {
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 72
                    radius: 22
                    color: Backend.coreRunning ? "#235249" : Theme.panelRaised
                    Text {
                        anchors.centerIn: parent
                        text: Backend.coreRunning ? "✓" : "Ⅱ"
                        color: Backend.coreRunning ? Theme.cyan : Theme.muted
                        font.pixelSize: 30
                        font.weight: Font.Bold
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Text {
                        text: Backend.coreRunning ? "存档保护正在运行" : "存档保护已停止"
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: 23
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: Backend.coreRunning
                            ? "没有游戏时只等待系统事件；游戏保存后才临时执行 Git 任务。"
                            : "启动后会自动监听已启用游戏，不需要保持这个窗口打开。"
                        color: Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
                AppButton {
                    visible: !Backend.coreRunning
                    text: "启动保护"
                    kind: "primary"
                    onClicked: Backend.startCore()
                }
                AppButton {
                    visible: Backend.coreRunning
                    text: "重新启动"
                    onClicked: Backend.restartCore()
                }
                AppButton {
                    visible: Backend.coreRunning
                    text: "停止保护"
                    kind: "danger"
                    onClicked: Backend.stopCore()
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 128
            RowLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "登录 Windows 后自动保护存档"
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "推荐开启。后台 Core 会以所需权限启动，游戏没运行时不会打开存档目录。"
                        color: Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
                Switch {
                    id: autostartSwitch
                    Binding {
                        target: autostartSwitch
                        property: "checked"
                        value: Backend.autostartEnabled
                    }
                    onToggled: Backend.setAutostart(checked)
                    indicator: Rectangle {
                        implicitWidth: 52
                        implicitHeight: 28
                        radius: 14
                        color: autostartSwitch.checked ? Theme.cyan : Theme.panelRaised
                        border.width: 1
                        border.color: autostartSwitch.checked ? Theme.cyan : Theme.border

                        Rectangle {
                            x: autostartSwitch.checked ? parent.width - width - 2 : 2
                            y: 2
                            width: parent.height - 4
                            height: parent.height - 4
                            radius: width / 2
                            color: Theme.text
                            Behavior on x { NumberAnimation { duration: 120 } }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: 146
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 9
                    Text { text: "Core 程序"; color: Theme.muted; font.family: Theme.uiFont; font.pixelSize: 11 }
                    Text {
                        Layout.fillWidth: true
                        text: Backend.corePath
                        color: Theme.text
                        elide: Text.ElideMiddle
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                    }
                    Item { Layout.fillHeight: true }
                    Text {
                        text: "一个无窗口、事件驱动的进程"
                        color: Theme.faint
                        font.family: Theme.uiFont
                        font.pixelSize: 10
                    }
                }
            }
            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: 146
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 9
                    Text { text: "配置文件"; color: Theme.muted; font.family: Theme.uiFont; font.pixelSize: 11 }
                    Text {
                        Layout.fillWidth: true
                        text: Backend.configPath
                        color: Theme.text
                        elide: Text.ElideMiddle
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                    }
                    Item { Layout.fillHeight: true }
                    Text {
                        text: "界面保存设置后会安全替换 TOML"
                        color: Theme.faint
                        font.family: Theme.uiFont
                        font.pixelSize: 10
                    }
                }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
