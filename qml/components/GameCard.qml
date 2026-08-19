import QtQuick
import QtQuick.Layouts
import GSave

// Library tile. The poster is fetched straight from the Steam CDN on demand and
// is never cached to disk; a missing application ID or a failed request falls
// back to a gradient tile so the card is always readable.
Rectangle {
    id: root

    property var entry
    property bool installed: entry !== undefined && entry.kind === "installed"
    property string title: entry === undefined ? "" : (entry.name || "")
    property string posterSource: entry === undefined ? "" : (entry.poster || "")
    property bool hovered: mouse.containsMouse

    signal activated()

    implicitWidth: 208
    implicitHeight: 312
    radius: 14
    color: Theme.panel
    border.width: 1
    border.color: hovered ? Theme.amber : Theme.border
    clip: true

    Behavior on border.color { ColorAnimation { duration: 130 } }

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.panelRaised }
            GradientStop { position: 1.0; color: Theme.panel }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: poster.status !== Image.Ready
        text: root.title.length > 0 ? root.title.charAt(0).toUpperCase() : "G"
        color: Theme.border
        font.family: Theme.displayFont
        font.pixelSize: 96
        font.weight: Font.Black
    }

    Image {
        id: poster
        anchors.fill: parent
        source: root.posterSource
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
        visible: status === Image.Ready
        opacity: root.installed || root.hovered ? 1.0 : 0.72
        Behavior on opacity { NumberAnimation { duration: 130 } }
    }

    // Keeps the name legible over bright cover art.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 132
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.45; color: "#c00b1118" }
            GradientStop { position: 1.0; color: "#f2080d13" }
        }
    }

    RowLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 10
        spacing: 6

        Rectangle {
            visible: root.installed
            Layout.preferredHeight: 22
            Layout.preferredWidth: stateLabel.implicitWidth + 16
            radius: 11
            color: root.entry !== undefined && root.entry.enabled
                ? Theme.cyanSoft : "#cc16232c"
            border.width: 1
            border.color: root.entry !== undefined && root.entry.enabled
                ? Theme.cyan : Theme.border
            Text {
                id: stateLabel
                anchors.centerIn: parent
                text: root.entry !== undefined && root.entry.enabled ? "保护中" : "已暂停"
                color: root.entry !== undefined && root.entry.enabled
                    ? Theme.cyan : Theme.muted
                font.family: Theme.uiFont
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            visible: root.entry !== undefined
                && root.entry.updateVersion !== undefined
                && root.entry.updateVersion.length > 0
            Layout.preferredHeight: 22
            Layout.preferredWidth: updateLabel.implicitWidth + 16
            radius: 11
            color: Theme.amberSoft
            border.width: 1
            border.color: Theme.amber
            Text {
                id: updateLabel
                anchors.centerIn: parent
                text: "可更新"
                color: Theme.amber
                font.family: Theme.uiFont
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            visible: !root.installed && root.entry !== undefined
                && root.entry.source === "local"
            Layout.preferredHeight: 22
            Layout.preferredWidth: localLabel.implicitWidth + 16
            radius: 11
            color: "#cc16232c"
            border.width: 1
            border.color: Theme.border
            Text {
                id: localLabel
                anchors.centerIn: parent
                text: "本地"
                color: Theme.muted
                font.family: Theme.uiFont
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 13
        spacing: 3

        Text {
            Layout.fillWidth: true
            text: root.title
            color: Theme.text
            font.family: Theme.displayFont
            font.pixelSize: 15
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            text: {
                if (root.entry === undefined) return ""
                if (root.installed) {
                    return root.entry.saveCount === 1
                        ? "1 个存档位置"
                        : root.entry.saveCount + " 个存档位置"
                }
                if (root.entry.summary !== undefined && root.entry.summary.length > 0) {
                    return root.entry.summary
                }
                return root.entry.source === "online" ? "点击下载并配置" : "点击自动检测并配置"
            }
            color: Theme.muted
            font.family: Theme.uiFont
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            visible: root.entry !== undefined && root.entry.version !== undefined
                && root.entry.version.length > 0
            text: {
                if (root.entry === undefined) return ""
                if (root.entry.updateVersion !== undefined
                    && root.entry.updateVersion.length > 0) {
                    return root.entry.version + " → " + root.entry.updateVersion
                }
                return "支持包 " + root.entry.version
            }
            color: Theme.faint
            font.family: Theme.monoFont
            font.pixelSize: 10
            elide: Text.ElideRight
            maximumLineCount: 1
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
