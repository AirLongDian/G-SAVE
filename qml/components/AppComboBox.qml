import QtQuick
import QtQuick.Controls
import GSave

ComboBox {
    id: root
    implicitHeight: 44
    leftPadding: 14
    rightPadding: 42
    font.family: Theme.uiFont
    font.pixelSize: 14

    contentItem: Text {
        leftPadding: 0
        rightPadding: 0
        text: root.displayText
        color: root.enabled ? Theme.text : Theme.faint
        font: root.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: root.width - width - 14
        anchors.verticalCenter: parent.verticalCenter
        text: "⌄"
        color: root.enabled ? Theme.muted : Theme.faint
        font.family: Theme.uiFont
        font.pixelSize: 18
    }
    background: Rectangle {
        radius: 8
        color: root.enabled ? Theme.input : Theme.panelRaised
        border.width: 1
        border.color: root.activeFocus ? Theme.amber : Theme.border
    }
    delegate: ItemDelegate {
        required property var modelData
        required property int index
        width: root.width
        height: 40
        highlighted: root.highlightedIndex === index
        contentItem: Text {
            text: root.textRole && modelData && modelData[root.textRole] !== undefined
                ? modelData[root.textRole] : String(modelData)
            color: Theme.text
            font: root.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: highlighted ? Theme.panelHover : Theme.panel
        }
    }
    popup: Popup {
        y: root.height + 4
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
        }
        background: Rectangle {
            radius: 8
            color: Theme.panel
            border.color: Theme.border
        }
    }
}
