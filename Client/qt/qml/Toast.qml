import QtQuick
import Aural

Rectangle {
    id: root
    property bool isError: false

    width: row.implicitWidth + 2 * Theme.md
    height: 44
    radius: Theme.radius
    color: Theme.surfaceContainerHighest
    border.color: Theme.outlineVariant
    border.width: 1
    opacity: 0
    visible: opacity > 0

    function show(message, error) {
        label.text = message
        root.isError = error === true
        anim.restart()
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.base

        Rectangle {
            width: 8
            height: 8
            radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: root.isError ? Theme.error : Theme.success
        }

        Text {
            id: label
            color: Theme.textPrimary
            font.pixelSize: Theme.bodyMd
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    SequentialAnimation {
        id: anim
        NumberAnimation { target: root; property: "opacity"; to: 1; duration: 150 }
        PauseAnimation { duration: 2800 }
        NumberAnimation { target: root; property: "opacity"; to: 0; duration: 300 }
    }
}
