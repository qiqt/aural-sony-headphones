import QtQuick
import QtQuick.Layouts
import Aural

// Says what the app actually knows about a setting, in the app's own words.
//
// The distinction that matters: "Sent" means the headphones acknowledged the frame.
// "Confirmed" means they reported the value back. Only ambient sound has a read-back today,
// so most controls honestly stop at "Sent".
Rectangle {
    id: chip

    // One of: unknown, requested, acknowledged, confirmed, failed
    property string status: "unknown"
    // Hide the chip entirely when there is nothing useful to say (e.g. no device).
    property bool active: true

    readonly property var _text: ({
        "unknown":      "Not read from device",
        "requested":    "Sending",
        "acknowledged": "Sent",
        "confirmed":    "Confirmed",
        "failed":       "Failed"
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
