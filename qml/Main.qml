import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GSave
import "pages"
import "components"

ApplicationWindow {
    id: window
    width: 1240
    height: 800
    minimumWidth: 1020
    minimumHeight: 680
    visible: true
    title: "G-SAVE · 游戏存档时间线"
    color: Theme.canvas

    property int currentPage: 0
    property string toastText: ""
    property bool toastError: false

    Connections {
        target: Backend
        function onMessage(text, error) {
            window.toastText = text
            window.toastError = error
            toast.open()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 230
            Layout.fillHeight: true
            color: Theme.sidebar
            border.color: Theme.border
            border.width: 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 8

                Item { Layout.preferredHeight: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 11
                    Rectangle {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        radius: 10
                        color: Theme.amber
                        Text {
                            anchors.centerIn: parent
                            text: "G"
                            color: Theme.canvas
                            font.family: Theme.displayFont
                            font.pixelSize: 19
                            font.weight: Font.Black
                        }
                    }
                    ColumnLayout {
                        spacing: -2
                        Text {
                            text: "G-SAVE"
                            color: Theme.text
                            font.family: Theme.displayFont
                            font.pixelSize: 19
                            font.weight: Font.Bold
                            font.letterSpacing: 1.1
                        }
                        Text {
                            text: "SAVE TIMELINE"
                            color: Theme.faint
                            font.family: Theme.monoFont
                            font.pixelSize: 9
                            font.letterSpacing: 1.5
                        }
                    }
                }

                Item { Layout.preferredHeight: 42 }

                Repeater {
                    model: [
                        { icon: "◇", title: "游戏支持", hint: "添加与管理游戏" },
                        { icon: "◉", title: "存档时间线", hint: "浏览、恢复与同步" },
                        { icon: "↑", title: "云端备份", hint: "上传规则与登录" },
                        { icon: "⚙", title: "运行设置", hint: "服务与自动启动" }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: 58
                        radius: 9
                        color: window.currentPage === index ? Theme.panelRaised
                            : navMouse.containsMouse ? Theme.panel : "transparent"
                        border.width: window.currentPage === index ? 1 : 0
                        border.color: Theme.border

                        Rectangle {
                            visible: window.currentPage === index
                            width: 3
                            height: 30
                            radius: 2
                            color: Theme.amber
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            spacing: 12
                            Text {
                                width: 24
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.icon
                                color: window.currentPage === index ? Theme.amber : Theme.muted
                                font.pixelSize: 20
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text {
                                    text: modelData.title
                                    color: Theme.text
                                    font.family: Theme.uiFont
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: modelData.hint
                                    color: Theme.faint
                                    font.family: Theme.uiFont
                                    font.pixelSize: 11
                                }
                            }
                        }
                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.currentPage = index
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 62
                    radius: 10
                    color: Theme.panel
                    border.color: Theme.border
                    Row {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: Backend.coreRunning ? Theme.cyan : Theme.faint
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                text: Backend.coreRunning ? "存档保护运行中" : "存档保护已停止"
                                color: Theme.text
                                font.family: Theme.uiFont
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: Backend.coreRunning ? "等待游戏保存事件" : "可在运行设置中启动"
                                color: Theme.faint
                                font.family: Theme.uiFont
                                font.pixelSize: 10
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            StackLayout {
                anchors.fill: parent
                currentIndex: window.currentPage
                GamesPage { }
                TimelinePage { }
                CloudPage { }
                ServicePage { }
            }
        }
    }

    Popup {
        id: toast
        x: window.width - width - 28
        y: window.height - height - 28
        width: Math.min(460, Math.max(300, toastLabel.implicitWidth + 54))
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
        Timer { id: toastTimer; interval: window.toastError ? 7000 : 3600; onTriggered: toast.close() }
    }
}
