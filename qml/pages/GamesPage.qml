import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GSave
import "../components"

Item {
    id: page
    property int selectedGame: -1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 34
        spacing: 22

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: 620
                spacing: 5
                Text {
                    text: "游戏支持"
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: "导入支持包 ZIP 只把支持包加入列表；随后点击「配置」检测游戏和存档位置，才开始保护存档。"
                    color: Theme.muted
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                text: "导入支持包 ZIP"
                onClicked: Backend.importPackage()
            }
            AppButton {
                text: "添加其他游戏"
                kind: "primary"
                onClicked: Backend.installGenericPackage()
            }
        }

        Text {
            visible: Backend.packages.length > 0
            text: "添加游戏支持"
            color: Theme.muted
            font.family: Theme.uiFont
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.letterSpacing: 1
        }

        Flow {
            id: packageFlow
            visible: Backend.packages.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            spacing: 14
            Repeater {
                model: Backend.packages
                delegate: Panel {
                    required property var modelData
                    width: Math.min(340, Math.max(220,
                        (packageFlow.width - packageFlow.spacing * 2) / 3))
                    height: 118
                    color: packageMouse.containsMouse ? Theme.panelHover : Theme.panel
                    Behavior on color { ColorAnimation { duration: 120 } }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 17
                        spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: Theme.text
                                font.family: Theme.displayFont
                                font.pixelSize: 17
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                maximumLineCount: 1
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.configured
                                ? "已配置。点击卡片修改游戏或存档位置"
                                : "已导入。点击卡片配置游戏、账号与存档目录"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "支持包 " + modelData.version
                            color: Theme.faint
                            font.family: Theme.monoFont
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                    }
                    MouseArea {
                        id: packageMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Backend.installPackage(modelData.index)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "正在管理"
                color: Theme.muted
                font.family: Theme.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }
            Item { Layout.fillWidth: true }
            Text {
                text: Backend.games.length + " 个游戏"
                color: Theme.faint
                font.family: Theme.monoFont
                font.pixelSize: 11
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: gameList
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                spacing: 7
                model: Backend.games
                currentIndex: page.selectedGame

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: gameList.width
                    height: 82
                    radius: 10
                    color: page.selectedGame === index ? Theme.panelRaised
                        : rowMouse.containsMouse ? Theme.panelHover : "transparent"
                    border.width: page.selectedGame === index ? 1 : 0
                    border.color: Theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 12
                        spacing: 14
                        Rectangle {
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 44
                            radius: 12
                            color: modelData.enabled ? Theme.cyanSoft : Theme.panelRaised
                            Text {
                                anchors.centerIn: parent
                                text: modelData.name.length > 0 ? modelData.name[0].toUpperCase() : "G"
                                color: modelData.enabled ? Theme.cyan : Theme.muted
                                font.family: Theme.displayFont
                                font.pixelSize: 20
                                font.weight: Font.Bold
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            RowLayout {
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    color: Theme.text
                                    font.family: Theme.uiFont
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                                Rectangle {
                                    Layout.preferredWidth: enabledText.implicitWidth + 14
                                    Layout.preferredHeight: 21
                                    radius: 10
                                    color: modelData.enabled ? Theme.cyanSoft : Theme.panelRaised
                                    Text {
                                        id: enabledText
                                        anchors.centerIn: parent
                                        text: modelData.enabled ? "保护中" : "已暂停"
                                        color: modelData.enabled ? Theme.cyan : Theme.muted
                                        font.family: Theme.uiFont
                                        font.pixelSize: 10
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.saveCount === 1
                                    ? modelData.savePath
                                    : modelData.saveCount + " 个存档位置 · " + modelData.savePath
                                color: Theme.muted
                                elide: Text.ElideMiddle
                                font.family: Theme.monoFont
                                font.pixelSize: 11
                            }
                        }
                        Text {
                            Layout.preferredWidth: 180
                            Layout.maximumWidth: 240
                            text: modelData.process
                            color: Theme.faint
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                            maximumLineCount: 1
                        }
                        AppButton {
                            compact: true
                            text: modelData.enabled ? "暂停保护" : "继续保护"
                            onClicked: Backend.toggleGame(index)
                        }
                        AppButton {
                            compact: true
                            kind: "danger"
                            text: "移除"
                            onClicked: Backend.removeGame(index)
                        }
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        z: -1
                        onClicked: page.selectedGame = index
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: Backend.games.length === 0
                    text: "还没有管理任何游戏\n先导入游戏支持包 ZIP，或添加其他游戏"
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
