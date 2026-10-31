import QtQuick
import QtQuick.Layouts
import Aural

// One control the headphones describe themselves. `control` is the controller's map for it
// (phase, valueKnown, checked, pending, status, and for multipoint busy and note - see
// HeadphonesController.h).
//
// The row only shows what is known: no switch position until the device has reported the
// value, and no way to change it while the device says it cannot be changed or while an
// earlier change is still in progress.
RowLayout {
    id: row

    property string title
    property string summary
    property var control: ({})
    signal toggled(bool on)

    readonly property string phase: control.phase !== undefined ? control.phase : "absent"
    readonly property string note: control.note !== undefined ? control.note : ""

    Layout.fillWidth: true
    visible: phase !== "absent"
    spacing: Theme.md

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        Text { text: row.title; color: Theme.textPrimary; font.pixelSize: Theme.bodyMd; font.weight: Font.DemiBold }
        Text {
            Layout.fillWidth: true
            text: row.summary
            color: Theme.textSecondary
            font.pixelSize: Theme.labelSm
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: text !== ""
            // A change in progress says what it waits for; otherwise the phase explains a
            // missing or disabled switch.
            text: row.note !== "" ? row.note
                  : row.phase === "loading" ? "Reading the current setting from the headphones..."
                  : row.phase === "unavailable" ? "The headphones report that this can't be changed right now."
                  : ""
            color: Theme.textSecondary
            font.pixelSize: Theme.labelSm
            font.italic: true
            wrapMode: Text.WordWrap
        }
    }
    StatusChip {
        Layout.alignment: Qt.AlignVCenter
        active: row.control.valueKnown === true
        status: row.control.status !== undefined ? row.control.status : "unknown"
    }
    PillSwitch {
        Layout.alignment: Qt.AlignVCenter
        visible: row.control.valueKnown === true
        checked: row.control.checked === true
        enabled: row.phase === "ready" && row.control.busy !== true
        onToggled: (on) => row.toggled(on)
    }
}
