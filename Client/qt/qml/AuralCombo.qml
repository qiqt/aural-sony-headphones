import QtQuick
import QtQuick.Controls.Basic
import Aural

ComboBox {
    id: control
    implicitHeight: 44
    font.family: Theme.fontFamily
    font.pixelSize: Theme.bodyMd

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surfaceContainerHigh
        border.color: control.activeFocus ? Theme.primary : Theme.outlineVariant
        border.width: 1
    }

    contentItem: Text {
        leftPadding: Theme.md
        rightPadding: 28
        text: control.displayText
        color: Theme.textPrimary
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - Theme.md
        y: (control.height - height) / 2
        text: "▾"
        color: Theme.textSecondary
        font.pixelSize: 12
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        padding: 4
        background: Rectangle {
            radius: Theme.radius
            color: Theme.surfaceContainerHigh
            border.color: Theme.outlineVariant
            border.width: 1
        }
        contentItem: ListView {
            clip: true
            implicitHeight: Math.min(contentHeight, 260)
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }

    delegate: ItemDelegate {
        width: ListView.view ? ListView.view.width : control.width
        height: 36
        contentItem: Text {
            leftPadding: Theme.base
            text: modelData
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyMd
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: highlighted ? Theme.surfaceContainerHighest : "transparent"
        }
        highlighted: control.highlightedIndex === index
    }
}
