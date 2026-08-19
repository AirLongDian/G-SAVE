import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GSave
import "../components"

// "存档管理" tab. Opening an older version always creates and switches to a named
// branch: a bare checkout would leave the repository on a detached HEAD where
// push, fetch and divergence handling all silently stop working.
Item {
    id: tab

    property int gameIndex: -1
    property int saveIndex: 0
    property var detail: ({})
    property var repositoryState: ({})
    property var branchList: []
    property string selectedCommit: ""
    property var selectedVersion

    signal saveIndexRequested(int value)
    signal commitSelected(string commitId)
    signal reloadRequested()

    readonly property var saves: detail.saves === undefined ? [] : detail.saves

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        anchors.topMargin: 18
        anchors.bottomMargin: 26
        spacing: 16

        // History list
        ColumnLayout {
            Layout.preferredWidth: 400
            Layout.fillHeight: true
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "存档时间线"
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                AppComboBox {
                    visible: tab.saves.length > 1
                    Layout.preferredWidth: 170
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
            }

            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: branchRow.implicitHeight + 30

                RowLayout {
                    id: branchRow
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: "当前存档线"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 11
                        }
                        Text {
                            Layout.fillWidth: true
                            text: tab.repositoryState.branch === undefined
                                ? "" : tab.repositoryState.branch
                            color: Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: 13
                            elide: Text.ElideMiddle
                        }
                    }

                    AppButton {
                        compact: true
                        text: "切换存档线"
                        enabled: tab.branchList.length > 1
                        onClicked: branchDialog.open()
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: historyList
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    spacing: 6
                    model: Backend.history

                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: historyList.width
                        height: 68
                        radius: 10
                        color: modelData.id === tab.selectedCommit ? Theme.panelRaised
                            : rowMouse.containsMouse ? Theme.panelHover : "transparent"
                        border.width: modelData.id === tab.selectedCommit ? 1 : 0
                        border.color: Theme.border

                        Rectangle {
                            visible: index === 0
                            width: 3
                            height: 26
                            radius: 2
                            color: Theme.amber
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 15
                            anchors.rightMargin: 13
                            anchors.topMargin: 11
                            anchors.bottomMargin: 11
                            spacing: 3

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    color: Theme.text
                                    font.family: Theme.uiFont
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    visible: index === 0
                                    text: "最新"
                                    color: Theme.amber
                                    font.family: Theme.uiFont
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text {
                                    text: modelData.time
                                    color: Theme.muted
                                    font.family: Theme.monoFont
                                    font.pixelSize: 11
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.shortId
                                    color: Theme.faint
                                    font.family: Theme.monoFont
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: tab.commitSelected(modelData.id)
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: Backend.history.length === 0
                        text: "这个存档还没有历史版本"
                        color: Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                    }
                }
            }
        }

        // Selected version detail
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: versionColumn.implicitHeight + 34

                ColumnLayout {
                    id: versionColumn
                    anchors.fill: parent
                    anchors.margins: 17
                    spacing: 11

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: tab.selectedVersion === undefined
                                    ? "没有选中版本" : tab.selectedVersion.title
                                color: Theme.text
                                font.family: Theme.uiFont
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }
                            Text {
                                visible: tab.selectedVersion !== undefined
                                text: tab.selectedVersion === undefined
                                    ? ""
                                    : tab.selectedVersion.time + "  ·  "
                                      + tab.selectedVersion.shortId
                                color: Theme.muted
                                font.family: Theme.monoFont
                                font.pixelSize: 11
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: tab.selectedVersion !== undefined
                        text: tab.selectedVersion === undefined
                            ? "" : tab.selectedVersion.slotSummary
                        color: Theme.cyan
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: tab.selectedVersion === undefined
                            ? [] : tab.selectedVersion.slots
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                Layout.preferredWidth: 70
                                text: modelData.label !== undefined
                                    ? modelData.label : ""
                                color: Theme.muted
                                font.family: Theme.uiFont
                                font.pixelSize: 12
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.detail !== undefined
                                    ? modelData.detail
                                    : (modelData.occupied ? "有存档" : "空")
                                color: modelData.occupied ? Theme.text : Theme.faint
                                font.family: Theme.uiFont
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: actionColumn.implicitHeight + 34

                ColumnLayout {
                    id: actionColumn
                    anchors.fill: parent
                    anchors.margins: 17
                    spacing: 12

                    Text {
                        text: "对选中版本的操作"
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        AppButton {
                            kind: "primary"
                            text: "从这个版本开新存档线"
                            enabled: tab.selectedCommit.length > 0
                            onClicked: {
                                branchNameField.text = Backend.suggestedBranchName(
                                    tab.gameIndex, tab.saveIndex, tab.selectedCommit)
                                newBranchDialog.open()
                            }
                        }

                        AppButton {
                            text: "恢复到当前存档线"
                            enabled: tab.selectedCommit.length > 0
                            onClicked: {
                                Backend.restoreVersion(
                                    tab.gameIndex, tab.saveIndex, tab.selectedCommit)
                                tab.reloadRequested()
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "「开新存档线」保留当前进度，两条线都能继续玩和上传；"
                            + "「恢复」把旧内容作为新版本推进当前存档线，历史保持一条直线。"
                        color: Theme.faint
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        lineHeight: 1.35
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: syncColumn.implicitHeight + 34

                ColumnLayout {
                    id: syncColumn
                    anchors.fill: parent
                    anchors.margins: 17
                    spacing: 12

                    Text {
                        text: "云端"
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: Backend.historyStatus.length > 0
                        text: Backend.historyStatus
                        color: Backend.historyDiverged ? Theme.amber : Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        visible: !Backend.historyDiverged

                        AppButton {
                            text: "上传到云端"
                            onClicked: Backend.pushSave(tab.gameIndex, tab.saveIndex)
                        }
                        AppButton {
                            text: "从云端拉取"
                            onClicked: {
                                Backend.integrateSave(tab.gameIndex, tab.saveIndex)
                                tab.reloadRequested()
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: Backend.historyDiverged
                        spacing: 10

                        Text {
                            Layout.fillWidth: true
                            text: "本地和云端各自都有新版本。存档是二进制文件，无法把两边合成一份，"
                                + "只能选一条作为主时间线；另一条会完整保留为侧存档线。"
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            lineHeight: 1.35
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            AppButton {
                                kind: "primary"
                                text: "用本机存档"
                                onClicked: {
                                    Backend.resolveTimeline(
                                        tab.gameIndex, tab.saveIndex, true)
                                    tab.reloadRequested()
                                }
                            }
                            AppButton {
                                text: "用云端存档"
                                onClicked: {
                                    Backend.resolveTimeline(
                                        tab.gameIndex, tab.saveIndex, false)
                                    tab.reloadRequested()
                                }
                            }
                            AppButton {
                                text: "稍后再说"
                                onClicked: Backend.deferTimelineDecision()
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    Dialog {
        id: newBranchDialog
        anchors.centerIn: parent
        width: 460
        modal: true
        title: "从这个版本开一条新存档线"
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: {
            Backend.openCommitAsBranch(
                tab.gameIndex, tab.saveIndex, tab.selectedCommit, branchNameField.text)
            tab.reloadRequested()
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: "新存档线会从选中的版本开始，当前存档线不受影响。切换过去后存档文件"
                    + "会变成这个版本的内容，之后的游玩都记录在新存档线上。"
                color: Theme.muted
                font.family: Theme.uiFont
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                lineHeight: 1.35
            }

            Text {
                text: "存档线名称"
                color: Theme.muted
                font.family: Theme.uiFont
                font.pixelSize: 12
            }

            AppTextField {
                id: branchNameField
                Layout.fillWidth: true
                placeholderText: "save/20260818-1a2b3c4d"
            }
        }
    }

    Dialog {
        id: branchDialog
        anchors.centerIn: parent
        width: 460
        modal: true
        title: "切换存档线"
        standardButtons: Dialog.Close

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: "每条存档线都是一套完整、独立的存档。切换会整体替换存档文件，"
                    + "其他存档线的历史不会丢失。"
                color: Theme.muted
                font.family: Theme.uiFont
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                lineHeight: 1.35
            }

            Repeater {
                model: tab.branchList
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    radius: 10
                    color: modelData.current ? Theme.panelRaised : "transparent"
                    border.width: 1
                    border.color: modelData.current ? Theme.amber : Theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 12
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: Theme.text
                                font.family: Theme.monoFont
                                font.pixelSize: 13
                                elide: Text.ElideMiddle
                            }
                            Text {
                                text: modelData.time + "  ·  " + modelData.shortTip
                                color: Theme.faint
                                font.family: Theme.monoFont
                                font.pixelSize: 11
                            }
                        }

                        Text {
                            visible: modelData.current
                            text: "当前"
                            color: Theme.amber
                            font.family: Theme.uiFont
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }

                        AppButton {
                            compact: true
                            visible: !modelData.current
                            text: "切换"
                            onClicked: {
                                branchDialog.close()
                                Backend.switchToBranch(
                                    tab.gameIndex, tab.saveIndex, modelData.name)
                                tab.reloadRequested()
                            }
                        }
                    }
                }
            }
        }
    }
}
