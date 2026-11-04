import QtQuick
import Aural

Item {
    id: root
    property var options: []
    property int currentIndex: 0
    signal activated(int index)

    implicitHeight: 40
    implicitWidth: 320

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: Theme.radiusFull
        color: Theme.surfaceContainerHighest

        Rectangle {
            id: highlight
            visible: root.currentIndex >= 0 && root.currentIndex < root.options.length
            width: root.options.length > 0 ? (bg.width - 8) / root.options.length : 0
            height: bg.height - 8
            y: 4
            x: 4 + root.currentIndex * width
            radius: Theme.radiusFull
            color: Theme.primaryContainer
            Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }

        Row {
            anchors.fill: parent
            anchors.margins: 4
            Repeater {
                model: root.options
                delegate: Item {
                    width: (bg.width - 8) / Math.max(1, root.options.length)
                    height: parent.height
                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: index === root.currentIndex ? Theme.primaryContainerText : Theme.textSecondary
                        font.pixelSize: Theme.labelLg
                        font.weight: index === root.currentIndex ? Font.DemiBold : Font.Normal
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        // Only emit — writing currentIndex here would break the caller's binding.
                        onClicked: root.activated(index)
                    }
                }
            }
        }
    }
}
