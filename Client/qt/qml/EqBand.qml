import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Aural

ColumnLayout {
    id: root
    property string label: ""
    property int value: 0
    property bool active: true
    property int minimum: -10
    property int maximum: 10
    signal moved(int value)
    spacing: Theme.base

    Text {
        Layout.alignment: Qt.AlignHCenter
        text: (root.value > 0 ? "+" : "") + root.value
        color: root.active ? Theme.primary : Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.labelMd
        font.weight: Font.DemiBold
    }

    Slider {
        id: s
        Layout.alignment: Qt.AlignHCenter
        Layout.fillHeight: true
        orientation: Qt.Vertical
        from: root.minimum
        to: root.maximum
        stepSize: 1
        enabled: root.active
        onMoved: root.moved(Math.round(value))

        // A Binding object survives the slider writing `value` internally during drags
        // (a plain `value: root.value` binding would be destroyed by the first drag).
        Binding on value { value: root.value }

        background: Rectangle {
            x: s.leftPadding + s.availableWidth / 2 - width / 2
            y: s.topPadding
            width: 6
            height: s.availableHeight
            radius: 3
            color: Theme.surfaceContainerHighest

            Rectangle {
                width: parent.width
                anchors.bottom: parent.bottom
                height: (1 - s.visualPosition) * parent.height
                radius: 3
                color: s.enabled ? Theme.primary : Theme.outline
            }
        }

        handle: Rectangle {
            x: s.leftPadding + s.availableWidth / 2 - width / 2
            y: s.topPadding + s.visualPosition * (s.availableHeight - height)
            width: 16
            height: 16
            radius: 8
            color: "#ffffff"
            border.color: Theme.outlineVariant
            border.width: 1
        }
    }

    Text {
        Layout.alignment: Qt.AlignHCenter
        text: root.label
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.labelSm
    }
}
