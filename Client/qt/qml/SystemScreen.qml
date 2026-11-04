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

        Rectangle {
            Layout.fillWidth: true
            visible: Hp.connected && !Hp.isV2
            radius: Theme.radiusLg; color: Theme.surfaceContainer
            border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: audioCol.implicitHeight + 2 * Theme.lg
            ColumnLayout {
                id: audioCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.sm
                Text { text: "Audio connection"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold }
                Text { text: "Current codec: " + (Hp.audioInfo.codec || "Not reported"); color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
                Text { text: "DSEE activity: " + (Hp.audioInfo.dseeActivity || "Not reported"); color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
                Text {
                    Layout.fillWidth: true
                    text: "Activity reports whether DSEE is processing now. The DSEE switch above controls whether it is enabled. Changing connection preference can interrupt audio."
                    wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.labelSm
                }
                AuralCombo {
                    Layout.fillWidth: true
                    visible: Hp.audioInfo.offered === true
                    enabled: Hp.audioInfo.ready === true && Hp.audioInfo.status !== "requested"
                    model: ["Prioritize sound quality", "Prioritize connection stability"]
                    Binding on currentIndex { value: Hp.audioInfo.value === undefined ? -1 : Hp.audioInfo.value }
                    onActivated: (i) => Hp.setConnectionPreference(i)
                }
                StatusChip { visible: Hp.audioInfo.offered === true; active: true; status: Hp.audioInfo.status || "unknown" }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: Hp.buttonAssignments.length > 0
            radius: Theme.radiusLg; color: Theme.surfaceContainer
            border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: assignmentCol.implicitHeight + 2 * Theme.lg
            ColumnLayout {
                id: assignmentCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.md
                Text { text: "Button assignments"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    text: "Choose from the assignments your headphones offer. Voice assistants may also need setup on your phone."
                    wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.labelSm
                }
                Repeater {
                    model: Hp.buttonAssignments
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text { text: modelData.title; color: Theme.textPrimary; font.pixelSize: Theme.bodyMd }
                        AuralCombo {
                            Layout.fillWidth: true
                            model: modelData.labels
                            enabled: modelData.ready && modelData.status !== "requested"
                            Binding on currentIndex { value: modelData.selected }
                            onActivated: (i) => Hp.setButtonAssignment(modelData.keyIndex, modelData.presetIds[i])
                        }
                        Text { Layout.fillWidth: true; text: modelData.summary; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                        StatusChip { active: true; status: modelData.status }
                    }
                }
            }
        }

        // Power management (needs a connected v1 device)
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: powCol.implicitHeight + 2 * Theme.lg
            // The device's capability overrides the model fallback once read.
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

        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusLg; color: Theme.surfaceContainer
            border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: diagnosticsCol.implicitHeight + 2 * Theme.lg
            ColumnLayout {
                id: diagnosticsCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.sm
                Text { text: "Device diagnostics"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold }
                Text { Layout.fillWidth: true; text: "Model: " + (Hp.diagnostics.reportedModel || "Not reported") + " / Firmware: " + (Hp.diagnostics.firmware || "Not reported"); wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
                Text { Layout.fillWidth: true; text: Hp.diagnostics.protocol + "\n" + Hp.diagnostics.protocolSource + "\n" + Hp.diagnostics.validation; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
                Text { Layout.fillWidth: true; text: Hp.diagnostics.capabilitiesKnown ? "Reported functions: " + (Hp.diagnostics.functions || "None") : "Capabilities not reported"; wrapMode: Text.WrapAnywhere; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                Text { Layout.fillWidth: true; text: "Export saves device details and the latest 256 log entries to Documents/Aural Diagnostics. The log may include Bluetooth names and addresses; review it before sharing."; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                AuralButton { text: "Export diagnostics"; onClicked: Hp.exportDiagnostics() }
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
