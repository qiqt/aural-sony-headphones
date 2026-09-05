import QtQuick
import QtQuick.Controls.Basic
import Aural

Slider {
    id: control
    from: 0
    to: 19
    stepSize: 1
    implicitHeight: 28
    opacity: enabled ? 1.0 : 0.45

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 6
        radius: 3
        color: Theme.surfaceContainerHighest

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 3
            color: Theme.primary
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 18
        height: 18
        radius: 9
        color: "#ffffff"
        border.color: Theme.outlineVariant
        border.width: 1
    }
}
