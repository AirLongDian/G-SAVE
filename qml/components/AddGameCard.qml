import QtQuick
import QtQuick.Layouts
import GSave

// Trailing tile of the installed row. Generic support is the only flow that asks
// the player to pick paths, so it gets its own explicit entry point instead of
// being mixed into the dedicated package cards.
Rectangle {
    id: root

    signal activated()

    implicitWidth: 208
    implicitHeight: 312
    radius: 14
    color: mouse.containsMouse ? Theme.panelRaised : "transparent"
    border.width: 1
    border.color: mouse.containsMouse ? Theme.amber : Theme.border

    Behavior on color { ColorAnimation { duration: 130 } }
    Behavior on border.color { ColorAnimation { duration: 130 } }

    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 40
        spacing: 12

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "+"
            color: mouse.containsMouse ? Theme.amber : Theme.border
            font.family: Theme.displayFont
            font.pixelSize: 58
            font.weight: Font.Light
            Behavior on color { ColorAnimation { duration: 130 } }
        }

        Text {
            Layout.fillWidth: true
            text: "添加无支持包游戏"
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: 14
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: "手动选择游戏程序和存档文件夹。只提供时间戳和通用文件信息。"
            color: Theme.muted
            font.family: Theme.uiFont
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            lineHeight: 1.3
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated()
    }
}
