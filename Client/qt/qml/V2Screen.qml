import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Aural

ScrollView {
    id: root
    contentWidth: availableWidth
    clip: true
    ColumnLayout {
        width: root.availableWidth
        spacing: Theme.lg
        Text { Layout.fillWidth: true; text: Hp.deviceModel; color: Theme.textPrimary; font.pixelSize: Theme.display; font.family: Theme.displayFont; wrapMode: Text.WordWrap }
        Text { Layout.fillWidth: true; text: Hp.v2Info.message || "Reading device information..."; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
        Text { Layout.fillWidth: true; text: "Firmware: " + (Hp.diagnostics.firmware || "Not reported"); color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
        AuralButton { text: "Refresh values"; enabled: !Hp.syncing && Hp.v2Info.phase === "ready"; onClicked: Hp.refreshV2Values() }
        Text { visible: !!Hp.v2Info.codec; text: "Codec: " + (Hp.v2Info.codec || ""); color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
        Text { visible: !!Hp.v2Info.effect; text: "DSEE effect: " + (Hp.v2Info.effect || ""); color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
        Repeater {
            model: Hp.v2Info.audio || []
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                Text { text: modelData.title; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd }
                SegmentedControl {
                    Layout.fillWidth: true
                    enabled: modelData.ready
                    options: modelData.selector === 0 ? ["Sound quality", "Stable connection"] : ["Off", "Auto"]
                    currentIndex: modelData.value
                    onActivated: (i) => Hp.setV2Audio(modelData.selector, i)
                }
                StatusChip { status: modelData.status }
            }
        }
        Repeater {
            model: Hp.v2Info.ambient || []
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                radius: Theme.radiusLg; color: Theme.surfaceContainer
                border.color: Theme.outlineVariant; border.width: 1
                implicitHeight: ambientCol.implicitHeight + 2 * Theme.lg
                ColumnLayout {
                    id: ambientCol
                    anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.md
                    Text { text: "Ambient sound"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd }
                    SegmentedControl {
                        Layout.fillWidth: true
                        enabled: modelData.ready
                        options: ["Off", "Noise cancelling", "Ambient"]
                        currentIndex: modelData.mode
                        onActivated: (i) => Hp.setV2Ambient(modelData.selector, i, modelData.level, modelData.voice)
                    }
                    AudioSlider {
                        Layout.fillWidth: true
                        enabled: modelData.ready && modelData.mode === 2
                        from: modelData.minimum; to: modelData.maximum; stepSize: modelData.step
                        Binding on value { value: modelData.level }
                        onMoved: Hp.setV2Ambient(modelData.selector, modelData.mode, Math.round(value), modelData.voice)
                    }
                    RowLayout {
                        visible: modelData.voiceSupported
                        Text { text: "Focus on voice"; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
                        Item { Layout.fillWidth: true }
                        PillSwitch { enabled: modelData.ready && modelData.mode === 2; checked: modelData.voice; onToggled: (on) => Hp.setV2Ambient(modelData.selector, modelData.mode, modelData.level, on) }
                    }
                    StatusChip { status: modelData.status }
                }
            }
        }
        Repeater {
            model: Hp.v2Info.equalizers || []
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                radius: Theme.radiusLg; color: Theme.surfaceContainer
                border.color: Theme.outlineVariant; border.width: 1
                implicitHeight: eqCol.implicitHeight + 2 * Theme.lg
                ColumnLayout {
                    id: eqCol
                    anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.md
                    Text { text: modelData.title; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd }
                    AuralCombo {
                        Layout.fillWidth: true
                        visible: modelData.presetIds.length > 0
                        enabled: modelData.ready && modelData.status !== "requested"
                        model: modelData.names
                        Binding on currentIndex { value: modelData.selected }
                        onActivated: (i) => Hp.setV2EqPreset(modelData.selector, modelData.presetIds[i])
                    }
                    Text { Layout.fillWidth: true; text: modelData.bandsEditable ? "Adjust each band below." : modelData.read ? "Current curve received. Band editing is unavailable until its layout is fully described." : "Waiting for the current equalizer setting."; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd; wrapMode: Text.WordWrap }
                    Repeater {
                        model: modelData.bands
                        delegate: RowLayout {
                            required property var modelData
                            readonly property var equalizer: eqCol.parent.modelData
                            Layout.fillWidth: true
                            Text { Layout.preferredWidth: 85; text: modelData.label; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
                            AudioSlider {
                                Layout.fillWidth: true
                                enabled: equalizer.bandsEditable
                                from: equalizer.minimum; to: equalizer.maximum; stepSize: 1
                                Binding on value { value: modelData.value }
                                onMoved: Hp.setV2EqBand(equalizer.selector, modelData.index, Math.round(value))
                            }
                            Text { Layout.preferredWidth: 28; text: modelData.value; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
                        }
                    }
                    StatusChip { status: modelData.status }
                }
            }
        }
        Text { Layout.fillWidth: true; text: "Additional controls appear as their protocol variants become supported. Device details and log export are available in System."; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd; wrapMode: Text.WordWrap }
        Item { Layout.preferredHeight: Theme.xl }
    }
}
