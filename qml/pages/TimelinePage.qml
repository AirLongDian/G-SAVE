import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GSave
import "../components"

Item {
    id: page
    property int selectedGame: Backend.games.length > 0 ? 0 : -1
    property int selectedSave: repositories.length > 0 ? 0 : -1
    property var repositories: selectedGame >= 0 ? Backend.repositoriesForGame(selectedGame) : []
    property int selectedCommit: historyList.currentIndex

    function updateHistory() {
        repositories = selectedGame >= 0 ? Backend.repositoriesForGame(selectedGame) : []
        selectedSave = repositories.length > 0 ? Math.min(Math.max(selectedSave, 0), repositories.length - 1) : -1
        if (selectedGame >= 0 && selectedSave >= 0)
            Backend.refreshHistory(selectedGame, selectedSave)
    }

    onSelectedGameChanged: updateHistory()
    onSelectedSaveChanged: {
        if (selectedGame >= 0 && selectedSave >= 0)
            Backend.refreshHistory(selectedGame, selectedSave)
    }
    Component.onCompleted: updateHistory()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 34
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text {
                    text: "存档时间线"
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: "先选择游戏，再查看它自己的版本。每个时间点都是一份完整、可恢复的存档。"
                    color: Theme.muted
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }
            }
            AppButton {
                text: "从云端检查并拉取"
                enabled: page.selectedGame >= 0 && page.selectedSave >= 0
                onClicked: Backend.integrateSave(page.selectedGame, page.selectedSave)
            }
            AppButton {
                text: "把本地上传到云端"
                kind: "primary"
                enabled: page.selectedGame >= 0 && page.selectedSave >= 0
                onClicked: Backend.pushSave(page.selectedGame, page.selectedSave)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 7
                Text { text: "游戏"; color: Theme.muted; font.family: Theme.uiFont; font.pixelSize: 12 }
                AppComboBox {
                    id: gameBox
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    model: Backend.games
                    textRole: "name"
                    currentIndex: page.selectedGame
                    onActivated: page.selectedGame = currentIndex
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 7
                Text { text: "存档位置"; color: Theme.muted; font.family: Theme.uiFont; font.pixelSize: 12 }
                AppComboBox {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    model: page.repositories
                    textRole: "name"
                    currentIndex: page.selectedSave
                    enabled: page.repositories.length > 1
                    onActivated: page.selectedSave = currentIndex
                    ToolTip.visible: hovered && page.repositories.length > 0
                    ToolTip.text: page.repositories.length > 0
                        ? page.repositories[Math.max(0, currentIndex)].path : ""
                }
            }
            AppButton {
                Layout.alignment: Qt.AlignBottom
                text: "刷新"
                compact: true
                onClicked: page.updateHistory()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 64 : 0
            visible: Backend.historyStatus.length > 0
            radius: 10
            color: Backend.historyDiverged ? Theme.redSoft : Theme.cyanSoft
            border.color: Backend.historyDiverged ? Theme.red : Theme.cyan
            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                Text {
                    text: Backend.historyDiverged ? "!" : "✓"
                    color: Backend.historyDiverged ? Theme.red : Theme.cyan
                    font.pixelSize: 18
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: Backend.historyStatus
                    color: Theme.text
                    wrapMode: Text.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                }
                AppButton {
                    visible: Backend.historyDiverged
                    compact: true
                    text: "保留本地为主线"
                    onClicked: Backend.resolveTimeline(page.selectedGame, page.selectedSave, true)
                }
                AppButton {
                    visible: Backend.historyDiverged
                    compact: true
                    text: "保留云端为主线"
                    onClicked: Backend.resolveTimeline(page.selectedGame, page.selectedSave, false)
                }
                AppButton {
                    visible: Backend.historyDiverged
                    compact: true
                    text: "稍后处理"
                    onClicked: Backend.deferTimelineDecision()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            Panel {
                Layout.preferredWidth: 510
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: historyList
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    spacing: 3
                    model: Backend.history
                    currentIndex: Backend.history.length > 0 ? 0 : -1

                    delegate: Item {
                        required property var modelData
                        required property int index
                        width: historyList.width
                        height: 84

                        Rectangle {
                            x: 24
                            y: index === 0 ? 42 : 0
                            width: 2
                            height: index === 0 ? parent.height - 42 : parent.height
                            color: Theme.border
                        }
                        Rectangle {
                            x: 18; y: 34
                            width: 14; height: 14; radius: 7
                            color: index === 0 ? Theme.amber : Theme.panel
                            border.width: 2
                            border.color: Theme.amber
                        }
                        Rectangle {
                            x: 46; y: 4
                            width: parent.width - 52; height: 76
                            radius: 10
                            color: historyList.currentIndex === index ? Theme.panelRaised
                                : historyMouse.containsMouse ? Theme.panelHover : "transparent"
                            border.width: historyList.currentIndex === index ? 1 : 0
                            border.color: Theme.border
                            Column {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 14
                                anchors.rightMargin: 12
                                spacing: 5
                                Row {
                                    spacing: 9
                                    Text {
                                        text: modelData.title
                                        color: Theme.text
                                        font.family: Theme.uiFont
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        text: index === 0 ? "当前" : ""
                                        color: Theme.amber
                                        font.family: Theme.uiFont
                                        font.pixelSize: 11
                                    }
                                }
                                Row {
                                    spacing: 12
                                    Text { text: modelData.time; color: Theme.muted; font.family: Theme.uiFont; font.pixelSize: 11 }
                                    Text { text: modelData.shortId; color: Theme.faint; font.family: Theme.monoFont; font.pixelSize: 10 }
                                }
                                Text {
                                    width: parent.width
                                    text: modelData.slotSummary
                                    color: Theme.faint
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: 10
                                }
                            }
                            MouseArea {
                                id: historyMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: historyList.currentIndex = index
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: Backend.history.length === 0
                        text: Backend.games.length === 0
                            ? "先在“游戏支持”中添加一个游戏"
                            : "这个存档还没有历史时间点"
                        color: Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 14
                    }
                }
            }

            Panel {
                id: detailPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                property var commit: historyList.currentIndex >= 0
                    && historyList.currentIndex < Backend.history.length
                    ? Backend.history[historyList.currentIndex] : null

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14
                    Text {
                        text: detailPanel.commit ? detailPanel.commit.title : "选择一个时间点"
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: 21
                        font.weight: Font.DemiBold
                    }
                    RowLayout {
                        visible: detailPanel.commit !== null
                        Text {
                            text: detailPanel.commit ? detailPanel.commit.time : ""
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            text: detailPanel.commit ? detailPanel.commit.shortId : ""
                            color: Theme.faint
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.border
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "存档槽位"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: detailPanel.commit
                                ? detailPanel.commit.slots.length + " 个槽位" : ""
                            color: Theme.faint
                            font.family: Theme.monoFont
                            font.pixelSize: 10
                        }
                    }
                    GridView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        cellWidth: Math.max(160, width / 2)
                        cellHeight: 72
                        model: detailPanel.commit ? detailPanel.commit.slots : []
                        delegate: Rectangle {
                            required property var modelData
                            width: GridView.view.cellWidth - 8
                            height: 62
                            radius: 9
                            color: modelData.occupied ? Theme.panelRaised : Theme.input
                            border.color: modelData.occupied ? Theme.border : "transparent"
                            Column {
                                anchors.fill: parent
                                anchors.margins: 11
                                spacing: 3
                                Text {
                                    width: parent.width
                                    text: (modelData.account
                                        ? modelData.account + " · " : "")
                                        + "槽位 " + modelData.index
                                    color: modelData.occupied ? Theme.amber : Theme.faint
                                    font.family: Theme.monoFont
                                    font.pixelSize: 10
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    width: parent.width
                                    text: modelData.occupied
                                        ? (modelData.name || modelData.label || "已使用") : "空槽位"
                                    color: modelData.occupied ? Theme.text : Theme.faint
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: 13
                                    font.weight: modelData.occupied ? Font.DemiBold : Font.Normal
                                }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !detailPanel.commit
                                || detailPanel.commit.slots.length === 0
                            text: "此支持包没有存档槽位信息"
                            color: Theme.faint
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                        }
                    }
                    AppButton {
                        Layout.fillWidth: true
                        text: "恢复到这个时间点"
                        kind: "primary"
                        enabled: detailPanel.commit !== null && historyList.currentIndex > 0
                        onClicked: Backend.restoreVersion(
                            page.selectedGame, page.selectedSave, detailPanel.commit.id)
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: "恢复前会自动保存当前状态；游戏运行时无法恢复。"
                        color: Theme.faint
                        font.family: Theme.uiFont
                        font.pixelSize: 10
                    }
                }
            }
        }
    }
}
