import QtQuick
import QtQuick.Layouts
import Aural

Item {
    id: root
    readonly property var powerOptions: ["Don't power off","After 5 min","After 30 min","After 1 hour","After 3 hours","When taken off"]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.xl
        spacing: Theme.lg

        ColumnLayout {
            spacing: 4
            Text { text: "System"; color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: Theme.display; font.weight: Font.Bold }
            Text { text: "Device behaviour and application preferences."; color: Theme.textSecondary; font.pixelSize: Theme.bodyLg }
        }

        // Device features (need a connected v1 device)
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: featCol.implicitHeight + 2 * Theme.lg
            enabled: Hp.connected && !Hp.isV2
            opacity: enabled ? 1.0 : 0.5
            ColumnLayout {
                id: featCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.md
                Text { text: "Device Features"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold; font.family: Theme.displayFont }
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout { spacing: 0
                        Text { text: "DSEE Extreme"; color: Theme.textPrimary; font.pixelSize: Theme.bodyMd; font.weight: Font.DemiBold }
                        Text { text: "Upscale compressed audio in real time."; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                    }
                    Item { Layout.fillWidth: true }
                    PillSwitch { checked: Hp.dsee; onToggled: (on) => Hp.dsee = on }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout { spacing: 0
                        Text { text: "Voice Guidance"; color: Theme.textPrimary; font.pixelSize: Theme.bodyMd; font.weight: Font.DemiBold }
                        Text { text: "Hear spoken status alerts from the headphones."; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                    }
                    Item { Layout.fillWidth: true }
                    PillSwitch { checked: Hp.voiceGuidance; onToggled: (on) => Hp.voiceGuidance = on }
                }
            }
        }

        // Power management (needs a connected v1 device)
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: powCol.implicitHeight + 2 * Theme.lg
            enabled: Hp.connected && !Hp.isV2
            opacity: enabled ? 1.0 : 0.5
            ColumnLayout {
                id: powCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.sm
                Text { text: "Power Management"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold; font.family: Theme.displayFont }
                Text { text: "Auto power-off"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                AuralCombo {
                    Layout.fillWidth: true
                    model: root.powerOptions
                    onActivated: (i) => Hp.autoPowerOff = i
                    Binding on currentIndex { value: Hp.autoPowerOff }
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
            }
        }

        Item { Layout.fillHeight: true }
    }
}
