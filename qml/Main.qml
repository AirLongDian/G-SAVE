import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GSave
import "pages"
import "components"

ApplicationWindow {
    id: window
    width: 1280
    height: 840
    minimumWidth: 1060
    minimumHeight: 700
    visible: true
    title: "G-SAVE · 游戏存档时间线"
    color: Theme.canvas

    // 0 library, 1 game detail, 2 settings
    property int currentPage: 0
    property int selectedGame: -1
    property string toastText: ""
    property bool toastError: false
    property bool closeApproved: false

    Component.onCompleted: Backend.refreshIndex()

    Connections {
        target: Backend
        function onMessage(text, error) {
            window.toastText = text
            window.toastError = error
            toast.open()
        }
    }

    // Core reads its configuration once at startup, so staged edits would be
    // silently lost on exit. Ask instead of dropping them.
    onClosing: function (close) {
        if (window.closeApproved || !Backend.hasPendingChanges) return
        close.accepted = false
        saveOnExitDialog.open()
    }

    function openGame(index) {
        window.selectedGame = index
        window.currentPage = 1
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Title bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: Theme.sidebar

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 14

                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: 9
                    color: Theme.amber
                    Text {
                        anchors.centerIn: parent
                        text: "G"
                        color: Theme.canvas
                        font.family: Theme.displayFont
                        font.pixelSize: 18
                        font.weight: Font.Black
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.currentPage = 0
                    }
                }

                Text {
                    text: "G-SAVE"
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: 18
                    font.weight: Font.Bold
                    font.letterSpacing: 1.1
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 24
                    color: Theme.border
                }

                Repeater {
                    model: [
                        { title: "游戏库", page: 0 },
                        { title: "设置", page: 2 }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.preferredWidth: navLabel.implicitWidth + 26
                        Layout.preferredHeight: 34
                        radius: 8
                        color: window.currentPage === modelData.page
                            || (modelData.page === 0 && window.currentPage === 1)
                            ? Theme.panelRaised
                            : navMouse.containsMouse ? Theme.panel : "transparent"
                        Text {
                            id: navLabel
                            anchors.centerIn: parent
                            text: modelData.title
                            color: window.currentPage === modelData.page
                                || (modelData.page === 0 && window.currentPage === 1)
                                ? Theme.text : Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.currentPage = modelData.page
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    compact: true
                    visible: Backend.hasPendingChanges
                    kind: "primary"
                    text: "保存并重启服务"
                    onClicked: Backend.savePendingChanges()
                }

                // Core state indicator; clicking toggles the service.
                Rectangle {
                    Layout.preferredHeight: 36
                    Layout.preferredWidth: coreRow.implicitWidth + 26
                    radius: 18
                    color: coreMouse.containsMouse && !Backend.coreBusy
                        ? Theme.panelHover : Theme.panel
                    border.width: 1
                    border.color: Backend.coreBusy ? Theme.border
                        : Backend.coreRunning ? Theme.cyan : Theme.border
                    opacity: Backend.coreBusy ? 0.7 : 1.0

                    Behavior on color { ColorAnimation { duration: 120 } }
                    Behavior on border.color { ColorAnimation { duration: 140 } }

                    RowLayout {
                        id: coreRow
                        anchors.centerIn: parent
                        spacing: 9

                        Rectangle {
                            Layout.preferredWidth: 9
                            Layout.preferredHeight: 9
                            radius: 5
                            color: Backend.coreBusy ? Theme.amber
                                : Backend.coreRunning ? Theme.cyan : Theme.faint

                            SequentialAnimation on opacity {
                                running: Backend.coreBusy
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.25; duration: 480 }
                                NumberAnimation { to: 1.0; duration: 480 }
                            }
                        }

                        Text {
                            text: Backend.coreBusy
                                ? "处理中…"
                                : Backend.coreRunning ? "保护运行中" : "保护已停止"
                            color: Backend.coreRunning && !Backend.coreBusy
                                ? Theme.text : Theme.muted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }

                    MouseArea {
                        id: coreMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !Backend.coreBusy
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Backend.toggleCore()
                    }

                    ToolTip.visible: coreMouse.containsMouse && !Backend.coreBusy
                    ToolTip.text: Backend.coreRunning
                        ? "点击停止存档保护" : "点击启动存档保护"
                }
            }
        }

        // Breadcrumb for the detail page
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            visible: window.currentPage === 1
            color: Theme.canvas

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: backLabel.implicitWidth + 26
                    Layout.preferredHeight: 28
                    radius: 8
                    color: backMouse.containsMouse ? Theme.panel : "transparent"
                    Text {
                        id: backLabel
                        anchors.centerIn: parent
                        text: "‹  返回游戏库"
                        color: backMouse.containsMouse ? Theme.text : Theme.muted
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: backMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.currentPage = 0
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: window.currentPage

            LibraryPage {
                onOpenGame: function (gameIndex) { window.openGame(gameIndex) }
            }

            GameDetailPage {
                gameIndex: window.selectedGame
                onBack: window.currentPage = 0
            }

            SettingsPage { }
        }
    }

    Dialog {
        id: saveOnExitDialog
        anchors.centerIn: parent
        width: 440
        modal: true
        title: "保存设置并重启服务？"
        closePolicy: Popup.NoAutoClose

        footer: DialogButtonBox {
            AppButton {
                kind: "primary"
                text: "保存并重启"
                onClicked: {
                    saveOnExitDialog.close()
                    if (Backend.savePendingChanges()) {
                        window.closeApproved = true
                        window.close()
                    }
                }
            }
            AppButton {
                text: "放弃改动并退出"
                onClicked: {
                    saveOnExitDialog.close()
                    Backend.discardPendingChanges()
                    window.closeApproved = true
                    window.close()
                }
            }
            AppButton {
                text: "取消"
                onClicked: saveOnExitDialog.close()
            }
        }

        contentItem: Text {
            text: "有未保存的设置。存档保护在启动时读取配置，所以保存后需要重启服务才会生效。"
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            lineHeight: 1.4
        }
    }

    Popup {
        id: toast
        x: window.width - width - 28
        y: window.height - height - 28
        width: Math.min(500, Math.max(300, toastLabel.implicitWidth + 54))
        height: Math.max(58, toastLabel.implicitHeight + 28)
        padding: 0
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150 }
            NumberAnimation { property: "y"; from: window.height; to: window.height - toast.height - 28; duration: 180; easing.type: Easing.OutCubic }
        }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 140 } }
        onOpened: toastTimer.restart()
        background: Rectangle {
            radius: 12
            color: window.toastError ? Theme.redSoft : Theme.cyanSoft
            border.color: window.toastError ? Theme.red : Theme.cyan
        }
        contentItem: Row {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 11
            Text {
                text: window.toastError ? "!" : "✓"
                color: window.toastError ? Theme.red : Theme.cyan
                font.pixelSize: 19
                font.weight: Font.Bold
            }
            Text {
                id: toastLabel
                width: toast.width - 62
                text: window.toastText
                color: Theme.text
                wrapMode: Text.Wrap
                font.family: Theme.uiFont
                font.pixelSize: 13
            }
        }
        Timer { id: toastTimer; interval: window.toastError ? 8000 : 3800; onTriggered: toast.close() }
    }

    // Dropping a package archive anywhere in the window imports it, which makes
    // sharing community packages easy without hunting for a menu.
    DropArea {
        anchors.fill: parent
        onDropped: function (drop) {
            if (!drop.hasUrls) return
            for (let index = 0; index < drop.urls.length; ++index) {
                const path = String(drop.urls[index])
                if (path.toLowerCase().endsWith(".zip")) {
                    Backend.importPackageFile(
                        Qt.resolvedUrl(drop.urls[index]).toString()
                            .replace(/^file:\/{3}/, ""))
                    return
                }
            }
        }
    }
}
