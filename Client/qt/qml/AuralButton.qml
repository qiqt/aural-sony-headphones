import QtQuick
import QtQuick.Controls.Basic
import Aural

Button {
    id: control
    property bool primary: false
    property bool danger: false
    property string iconName: ""

    readonly property color fg: control.primary ? Theme.primaryText
                                : (control.danger ? Theme.error : Theme.textPrimary)

    implicitHeight: 40
    leftPadding: Theme.md
    rightPadding: Theme.md
    font.pixelSize: Theme.labelLg
    font.weight: Font.DemiBold

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: Theme.base
            Icon {
                visible: control.iconName !== ""
                name: control.iconName
                size: 18
                color: control.fg
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: control.text
                font: control.font
                color: control.fg
                verticalAlignment: Text.AlignVCenter
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
            }
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.primary ? Theme.primary
                               : (control.down ? Theme.surfaceContainerHighest : Theme.surfaceContainerHigh)
        border.color: control.primary ? "transparent" : Theme.outlineVariant
        border.width: 1
        opacity: control.enabled ? 1.0 : 0.5
    }
}
