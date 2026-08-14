import QtQuick
import QtQuick.Controls
import GSave

Button {
    id: root
    property string kind: "secondary"
    property bool compact: false

    implicitHeight: compact ? 34 : 42
    implicitWidth: Math.max(compact ? 92 : 112, label.implicitWidth + 30)
    leftPadding: 15
    rightPadding: 15

    contentItem: Text {
        id: label
        text: root.text
        color: !root.enabled ? Theme.faint
            : root.kind === "primary" ? Theme.canvas
            : root.kind === "danger" ? Theme.red
            : Theme.text
        font.family: Theme.uiFont
        font.pixelSize: root.compact ? 13 : 14
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 8
        border.width: root.kind === "primary" ? 0 : 1
        border.color: root.kind === "danger" ? Theme.red
            : root.hovered ? Theme.amber : Theme.border
        color: !root.enabled ? Theme.panelRaised
            : root.kind === "primary" ? (root.down ? "#d99c45" : Theme.amber)
            : root.kind === "danger" ? (root.down ? Theme.redSoft : "transparent")
            : root.down ? Theme.panelHover
            : root.hovered ? Theme.panelRaised : "transparent"
        Behavior on color { ColorAnimation { duration: 110 } }
    }
}
