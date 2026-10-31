import QtQuick
import QtQuick.Layouts
import Aural

// One control the headphones describe themselves. `control` is the controller's map for it
// (phase, valueKnown, checked, pending, status - see HeadphonesController.h).
//
// The row only shows what is known: no switch position until the device has reported the
// value, and no way to change it while the device says it cannot be changed.
RowLayout {
    id: row

    property string title
    property string summary
    property var control: ({})
    signal toggled(bool on)

    readonly property string phase: control.phase !== undefined ? control.phase : "absent"

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
            visible: row.phase === "loading" || row.phase === "unavailable"
            text: row.phase === "loading"
                  ? "Reading the current setting from the headphones…"
                  : "The headphones report that this can't be changed right now."
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
        enabled: row.phase === "ready"
        onToggled: (on) => row.toggled(on)
    }
}
