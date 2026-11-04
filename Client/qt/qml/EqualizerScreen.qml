import QtQuick
import QtQuick.Layouts
import Aural

Item {
    id: root
    readonly property var presetNames: ["Off","Bright","Excited","Mellow","Relaxed","Vocal","Treble Boost","Bass Boost","Speech","Manual","Custom 1","Custom 2"]
    readonly property var bandLabels: ["400","1k","2.5k","6.3k","16k"]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.xl
        spacing: Theme.lg

        ColumnLayout {
            spacing: 4
            Text { text: "Equalizer"; color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: Theme.display; font.weight: Font.Bold }
            Text { text: "Shape your sound with presets or a manual 5-band curve."; color: Theme.textSecondary; font.pixelSize: Theme.bodyLg }
            StatusChip {
                Layout.topMargin: Theme.xs
                active: Hp.equalizerAvailable
                status: Hp.settingStatus.equalizer
            }
        }

        // Shown until the headphones have reported their equalizer: before that, what is on
        // screen is the app's last choice, not necessarily the device's.
        Rectangle {
            Layout.fillWidth: true
            visible: Hp.settingStatus.equalizer !== "confirmed"
            radius: Theme.radius
            color: Theme.surfaceContainerLow
            border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: noteRow.implicitHeight + Theme.md
            RowLayout {
                id: noteRow
                anchors.fill: parent; anchors.margins: Theme.sm; spacing: Theme.base
                Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: Theme.textSecondary; Layout.alignment: Qt.AlignVCenter }
                Text { text: "Not yet read from the headphones: a change made from another device may not show here."; color: Theme.textSecondary; font.pixelSize: Theme.labelMd }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.lg

            // --- Presets ---
            Rectangle {
                Layout.preferredWidth: 280
                Layout.maximumWidth: 280
                Layout.fillHeight: true
                radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.sm
                    Text { text: "Presets"; color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.base
                        rowSpacing: Theme.base
                        Repeater {
                            model: root.presetNames
                            delegate: Rectangle {
                                enabled: !Hp.eqSavePending && Hp.eqPresetOptions.indexOf(index) >= 0 && (index !== 9 || Hp.eqBandsEditable)
                                opacity: enabled ? 1 : 0.4
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                radius: Theme.radius
                                color: index === Hp.eqPreset ? Theme.primaryContainer : Theme.surfaceContainerHigh
                                border.color: index === Hp.eqPreset ? Theme.primary : Theme.outlineVariant
                                border.width: 1
                                Text { anchors.centerIn: parent; text: modelData; color: index === Hp.eqPreset ? Theme.primaryContainerText : Theme.textSecondary; font.pixelSize: Theme.labelMd; font.weight: index === Hp.eqPreset ? Font.DemiBold : Font.Normal }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Hp.eqPreset = index }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            // --- Curve + manual ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.lg

                // Response curve
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 170
                    radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.base
                        RowLayout {
                            Layout.fillWidth: true
                            // -1: the headphones report a preset this table does not list.
                            Text { text: "Response Curve: " + (Hp.eqPreset >= 0 ? root.presetNames[Hp.eqPreset] : "Other"); color: Theme.textPrimary; font.pixelSize: Theme.labelLg; font.weight: Font.DemiBold }
                            Item { Layout.fillWidth: true }
                            Row { spacing: 6
                                Rectangle { width: 8; height: 8; radius: 4; color: Theme.success; anchors.verticalCenter: parent.verticalCenter }
                                Text { anchors.verticalCenter: parent.verticalCenter; text: "ACTIVE"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm; font.letterSpacing: 1 }
                            }
                        }
                        EqCurve {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                    }
                }

                // Manual adjustment
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
                    readonly property bool editing: Hp.eqManual && Hp.eqBandsEditable && !Hp.eqSavePending
                    opacity: editing ? 1.0 : 0.6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.base
                        Text {
                            text: !Hp.eqBandsEditable ? "Manual Adjustment (these headphones report a different band layout)"
                                : Hp.eqManual ? "Manual Adjustment" : "Manual Adjustment (select the Manual preset)"
                            color: Theme.textPrimary; font.pixelSize: Theme.labelLg; font.weight: Font.DemiBold
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: Theme.md
                            Repeater {
                                model: 5
                                delegate: EqBand {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    label: root.bandLabels[index]
                                    value: Hp.eqBands[index]
                                    active: Hp.eqManual && Hp.eqBandsEditable && !Hp.eqSavePending
                                    minimum: Hp.eqMinimum
                                    maximum: Hp.eqMaximum
                                    onMoved: (v) => Hp.setEqBand(index, v)
                                }
                            }
                            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.outlineVariant }
                            EqBand {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                label: "Clear Bass"
                                value: Hp.eqClearBass
                                active: Hp.eqManual && Hp.eqBandsEditable && !Hp.eqSavePending
                                minimum: Hp.eqMinimum
                                maximum: Hp.eqMaximum
                                onMoved: (v) => Hp.eqClearBass = v
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.sm
                            AuralButton {
                                text: "Save to Custom 1"
                                enabled: Hp.eqCanSave && Hp.eqPresetOptions.indexOf(10) >= 0
                                onClicked: Hp.saveEqToCustom(1)
                            }
                            AuralButton {
                                text: "Save to Custom 2"
                                enabled: Hp.eqCanSave && Hp.eqPresetOptions.indexOf(11) >= 0
                                onClicked: Hp.saveEqToCustom(2)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: Hp.eqSavePending
                            text: "Saving the curve and checking the headphones' reply…"
                            wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.labelSm
                        }
                    }
                }
            }
        }
    }
}
