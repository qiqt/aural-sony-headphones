import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Aural

Item {
    id: root
    // Indexed by AUTO_POWER_OFF value. Only the values in Hp.autoPowerOffOptions are offered.
    readonly property var powerNames: ["Don't power off","After 5 min","After 30 min","After 1 hour","After 3 hours","When taken off"]
    readonly property var powerOptions: Hp.autoPowerOffOptions
    readonly property var powerLabels: powerOptions.map(v => powerNames[v])

    // This page is the tallest one and grows with each preference, so it scrolls rather
    // than clipping the bottom card at the minimum window height.
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

    ColumnLayout {
        x: Theme.xl
        y: Theme.xl
        width: root.width - 2 * Theme.xl
        spacing: Theme.lg

        ColumnLayout {
            spacing: 4
            Text { text: "System"; color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: Theme.display; font.weight: Font.Bold }
            Text { text: "Device behaviour and application preferences."; color: Theme.textSecondary; font.pixelSize: Theme.bodyLg }
        }

        // Device features (need a connected v1 device). Each row follows its own gate: the
        // device's advertised functions once it has answered, otherwise the recognised model.
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: featCol.implicitHeight + 2 * Theme.lg
            enabled: Hp.dseeAvailable || Hp.voiceGuidanceAvailable
            opacity: enabled ? 1.0 : 0.5
            ColumnLayout {
                id: featCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.md
                Text { text: "Device Features"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold; font.family: Theme.displayFont }
                Text {
                    Layout.fillWidth: true
                    visible: Hp.connected && !Hp.isV2 && !Hp.dseeAvailable && !Hp.voiceGuidanceAvailable
                    text: Hp.capabilitiesKnown
                          ? "These headphones did not report DSEE or voice guidance."
                          : "Not offered for an unrecognised device until it reports supporting them."
                    color: Theme.textSecondary
                    font.pixelSize: Theme.labelSm
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Hp.dseeAvailable || !Hp.connected
                    ColumnLayout { spacing: 0
                        Text { text: "DSEE Extreme"; color: Theme.textPrimary; font.pixelSize: Theme.bodyMd; font.weight: Font.DemiBold }
                        // The setting, not whether upscaling is running right now: that is a
                        // separate indicator the headphones report on their own.
                        Text { text: "Allow the headphones to upscale compressed audio."; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                    }
                    Item { Layout.fillWidth: true }
                    StatusChip {
                        Layout.alignment: Qt.AlignVCenter
                        active: Hp.dseeAvailable
                        status: Hp.settingStatus.dsee
                    }
                    PillSwitch { checked: Hp.dsee; enabled: Hp.dseeAvailable; onToggled: (on) => Hp.dsee = on }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Hp.voiceGuidanceAvailable || !Hp.connected
                    ColumnLayout { spacing: 0
                        Text { text: "Voice Guidance"; color: Theme.textPrimary; font.pixelSize: Theme.bodyMd; font.weight: Font.DemiBold }
                        Text { text: "Hear spoken status alerts from the headphones."; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                    }
                    Item { Layout.fillWidth: true }
                    StatusChip {
                        Layout.alignment: Qt.AlignVCenter
                        active: Hp.voiceGuidanceAvailable
                        status: Hp.settingStatus.voiceGuidance
                    }
                    PillSwitch { checked: Hp.voiceGuidance; enabled: Hp.voiceGuidanceAvailable; onToggled: (on) => Hp.voiceGuidance = on }
                }
            }
        }

        // Controls the headphones describe themselves. The card appears once the device has
        // described one of them; until then there is nothing honest to show. Bound to the data,
        // not to the rows' visible: a hidden card makes its rows read as hidden too, so it could
        // never come back.
        Rectangle {
            Layout.fillWidth: true
            visible: Hp.touchPanel.phase !== "absent" || Hp.wearingControl.phase !== "absent"
                     || Hp.multipointMode.phase !== "absent"
            radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: ctlCol.implicitHeight + 2 * Theme.lg
            ColumnLayout {
                id: ctlCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.md
                Text { text: "Headphone Controls"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold; font.family: Theme.displayFont }
                DeviceToggleRow {
                    title: "Touch controls"
                    // Enables or disables the panel; what each gesture does is unchanged.
                    summary: "Turn the touch panel on the ear cup on or off. Gestures keep their current actions."
                    control: Hp.touchPanel
                    onToggled: (on) => Hp.setTouchPanelEnabled(on)
                }
                DeviceToggleRow {
                    title: "Control by wearing detection"
                    // The setting, not a live "worn / not worn" reading.
                    summary: "Let the headphones respond when you put them on or take them off."
                    control: Hp.wearingControl
                    onToggled: (on) => Hp.setWearingControlEnabled(on)
                }
                DeviceToggleRow {
                    title: "Multipoint"
                    // The headphones may ask for confirmation (MultipointConfirmDialog in Main)
                    // and disconnect to apply the change; Aural then reconnects and reads it.
                    summary: "Stay connected to two devices at once. Changing this can disconnect the headphones for a moment."
                    control: Hp.multipointMode
                    onToggled: (on) => Hp.setMultipointEnabled(on)
                }
            }
        }

        // Power management (needs a connected v1 device)
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: powCol.implicitHeight + 2 * Theme.lg
            // The choices are the model's known ones plus whatever the device reports holding.
            enabled: Hp.autoPowerOffAvailable
            opacity: enabled ? 1.0 : 0.5
            ColumnLayout {
                id: powCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.sm
                Text { text: "Power Management"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold; font.family: Theme.displayFont }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Auto power-off"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                    Item { Layout.fillWidth: true }
                    StatusChip {
                        active: Hp.autoPowerOffAvailable
                        status: Hp.settingStatus.autoPowerOff
                    }
                }
                AuralCombo {
                    Layout.fillWidth: true
                    model: root.powerLabels
                    onActivated: (i) => Hp.autoPowerOff = root.powerOptions[i]
                    Binding on currentIndex { value: Math.max(0, root.powerOptions.indexOf(Hp.autoPowerOff)) }
                }
            }
        }

        // App preferences
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: appCol.implicitHeight + 2 * Theme.lg
            ColumnLayout {
                id: appCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.sm
                Text { text: "App Preferences"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold; font.family: Theme.displayFont }
                Text { text: "Theme"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                SegmentedControl {
                    Layout.fillWidth: true
                    options: ["Light","Dark"]
                    currentIndex: Theme.dark ? 1 : 0
                    onActivated: (i) => Theme.dark = (i === 1)
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.base
                    ColumnLayout { spacing: 0
                        Text { text: "Keep running in the system tray"; color: Theme.textPrimary; font.pixelSize: Theme.bodyMd; font.weight: Font.DemiBold }
                        Text { text: "Closing the window hides Aural instead of quitting it."; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                    }
                    Item { Layout.fillWidth: true }
                    PillSwitch { checked: App.minimiseToTray; onToggled: (on) => App.minimiseToTray = on }
                }
            }
        }

        // Bottom breathing room; ScrollView content has no anchor margins.
        Item { Layout.preferredHeight: Theme.xl }
    }
    }
}
