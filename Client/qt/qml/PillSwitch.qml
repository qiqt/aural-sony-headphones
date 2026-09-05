import QtQuick
import Aural

Item {
    id: root
    property bool checked: false
    signal toggled(bool checked)

    implicitWidth: 48
    implicitHeight: 28
    opacity: enabled ? 1.0 : 0.45

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? Theme.primary : Theme.surfaceContainerHighest
        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            id: knob
            width: parent.height - 6
            height: parent.height - 6
            radius: height / 2
            y: 3
            x: root.checked ? parent.width - width - 3 : 3
            color: root.checked ? "#ffffff" : Theme.outline
            Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        // Only emit — never write `checked` here, or the caller's `checked: Hp.x`
        // binding is destroyed and the switch stops tracking state changes.
        onClicked: root.toggled(!root.checked)
    }
}
