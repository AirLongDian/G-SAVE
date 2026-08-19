import QtQuick
import QtQuick.Layouts
import GSave
import "../components"

Item {
    id: page

    property int gameIndex: -1
    property int currentTab: 0
    property int saveIndex: 0
    property var detail: ({})
    property var repositoryState: ({})
    property var branchList: []
    property string selectedCommit: ""

    signal back()

    function refresh() {
        if (page.gameIndex < 0) return
        page.detail = Backend.gameDetail(page.gameIndex)
        page.reloadRepository()
    }

    function reloadRepository() {
        if (page.gameIndex < 0) return
        page.repositoryState = Backend.repositoryState(page.gameIndex, page.saveIndex)
        page.branchList = Backend.branches(page.gameIndex, page.saveIndex)
        Backend.refreshHistory(page.gameIndex, page.saveIndex)
        page.selectedCommit = Backend.history.length > 0 ? Backend.history[0].id : ""
    }

    onGameIndexChanged: page.refresh()
    onSaveIndexChanged: page.reloadRepository()

    Connections {
        target: Backend
        function onGamesChanged() {
            if (page.gameIndex >= 0) page.detail = Backend.gameDetail(page.gameIndex)
        }
        function onHistoryChanged() {
            if (Backend.history.length === 0) {
                page.selectedCommit = ""
            } else if (page.selectedCommit.length === 0) {
                page.selectedCommit = Backend.history[0].id
            }
        }
    }

    readonly property var selectedVersion: {
        const history = Backend.history
        for (let index = 0; index < history.length; ++index) {
            if (history[index].id === page.selectedCommit) return history[index]
        }
        return history.length > 0 ? history[0] : undefined
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Banner
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 158
            clip: true

            Rectangle {
                anchors.fill: parent
                color: Theme.sidebar
            }

            Image {
                anchors.fill: parent
                source: page.detail.banner === undefined ? "" : page.detail.banner
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: false
                opacity: 0.5
                visible: status === Image.Ready
            }

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#a60b1118" }
                    GradientStop { position: 1.0; color: "#f20b1118" }
                }
            }

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 30
                anchors.rightMargin: 30
                anchors.bottomMargin: 20
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5

                    Text {
                        Layout.fillWidth: true
                        text: page.detail.name === undefined ? "" : page.detail.name
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: 30
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    RowLayout {
                        spacing: 10
                        Text {
                            text: page.detail.packageName === undefined
                                ? "" : page.detail.packageName
                            color: Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                        }
                        Text {
                            visible: page.detail.packageVersion !== undefined
                                && page.detail.packageVersion.length > 0
                            text: page.detail.packageVersion === undefined
                                ? "" : page.detail.packageVersion
                            color: Theme.faint
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                        }
                    }
                }

                AppButton {
                    compact: true
                    text: page.detail.enabled ? "暂停保护" : "继续保护"
                    onClicked: Backend.toggleGame(page.gameIndex)
                }

                AppButton {
                    compact: true
                    kind: "danger"
                    text: "移除"
                    onClicked: {
                        Backend.removeGame(page.gameIndex)
                        page.back()
                    }
                }
            }
        }

        // Tabs
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 30
            Layout.topMargin: 16
            spacing: 4

            Repeater {
                model: ["支持配置", "存档管理"]
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    implicitWidth: tabLabel.implicitWidth + 30
                    implicitHeight: 38
                    radius: 9
                    color: page.currentTab === index ? Theme.panelRaised
                        : tabMouse.containsMouse ? Theme.panel : "transparent"
                    border.width: page.currentTab === index ? 1 : 0
                    border.color: Theme.border

                    Text {
                        id: tabLabel
                        anchors.centerIn: parent
                        text: modelData
                        color: page.currentTab === index ? Theme.text : Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.currentTab = index
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                visible: Backend.hasPendingChanges
                Layout.preferredHeight: 30
                Layout.preferredWidth: pendingLabel.implicitWidth + 20
                radius: 8
                color: Theme.amberSoft
                border.width: 1
                border.color: Theme.amber
                Text {
                    id: pendingLabel
                    anchors.centerIn: parent
                    text: "有未保存的设置"
                    color: Theme.amber
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }

            AppButton {
                compact: true
                visible: Backend.hasPendingChanges
                text: "放弃改动"
                onClicked: {
                    Backend.discardPendingChanges()
                    page.refresh()
                }
            }

            AppButton {
                compact: true
                kind: "primary"
                visible: Backend.hasPendingChanges
                text: "保存并重启服务"
                onClicked: {
                    if (Backend.savePendingChanges()) page.refresh()
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: page.currentTab

            GameSupportTab {
                gameIndex: page.gameIndex
                detail: page.detail
                repositoryState: page.repositoryState
                saveIndex: page.saveIndex
                onSaveIndexRequested: function (value) { page.saveIndex = value }
                onReloadRequested: page.refresh()
            }

            GameSavesTab {
                gameIndex: page.gameIndex
                detail: page.detail
                repositoryState: page.repositoryState
                branchList: page.branchList
                saveIndex: page.saveIndex
                selectedCommit: page.selectedCommit
                selectedVersion: page.selectedVersion
                onSaveIndexRequested: function (value) { page.saveIndex = value }
                onCommitSelected: function (value) { page.selectedCommit = value }
                onReloadRequested: page.reloadRepository()
            }
        }
    }
}
