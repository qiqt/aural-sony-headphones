import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Aural

// "Sent" means the headphones acknowledged the command.
// "Confirmed" means they reported the setting's value.
Rectangle {
    id: chip

    // One of: unknown, requested, acknowledged, confirmed, failed
    property string status: "unknown"
    // Hide the chip entirely when there is nothing useful to say (e.g. no device).
    property bool active: true

    readonly property var _text: ({
        "unknown":      "Not read from device",
        "requested":    "Sending",
        "acknowledged": "Sent, unconfirmed",
        "confirmed":    "Confirmed",
        "failed":       "Could not apply"
    })
    readonly property var _icon: ({
        "unknown":      "help_outline",
        "requested":    "sync",
        "acknowledged": "north_east",
        "confirmed":    "check_circle",
        "failed":       "error_outline"
    })

    function _tone() {
        if (chip.status === "failed") return Theme.error
        if (chip.status === "confirmed") return Theme.success
        return Theme.textSecondary
    }

    readonly property var explanations: ({
        "unknown": "The headphones have not reported this setting yet.",
        "requested": "Waiting for the headphones to respond.",
        "acknowledged": "The headphones received the command but haven't reported the new setting.",
        "confirmed": "The headphones reported the value shown here.",
        "failed": "Couldn't change this setting. Check the connection and try again."
    })
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered
    ToolTip.text: chip.explanations[chip.status] || ""
    ToolTip.delay: 300
    Accessible.name: chip._text[chip.status] || chip.status
    Accessible.description: chip.explanations[chip.status] || ""

    visible: active
    radius: Theme.radiusFull
    color: Theme.surfaceContainerHigh
    border.width: 1
    border.color: chip.status === "failed" ? Theme.error : Theme.outlineVariant
    implicitWidth: row.implicitWidth + Theme.md
    implicitHeight: 24

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Icon {
            name: chip._icon[chip.status] !== undefined ? chip._icon[chip.status] : "help_outline"
            size: 14
            color: chip._tone()
        }
        Text {
            text: chip._text[chip.status] !== undefined ? chip._text[chip.status] : chip.status
            color: chip._tone()
            font.pixelSize: Theme.labelMd
        }
    }
}
