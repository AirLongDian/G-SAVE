import QtQuick
import QtQuick.Controls
import GSave

TextField {
    id: root
    implicitHeight: 44
    color: Theme.text
    placeholderTextColor: Theme.faint
    selectionColor: Theme.amber
    selectedTextColor: Theme.canvas
    font.family: Theme.uiFont
    font.pixelSize: 14
    leftPadding: 14
    rightPadding: 14

    background: Rectangle {
        radius: 8
        color: Theme.input
        border.width: 1
        border.color: root.activeFocus ? Theme.amber : Theme.border
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }
}
