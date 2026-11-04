import QtQuick
import QtQuick.Layouts
import Aural

Item {
    id: root
    readonly property var modeNames: ["Noise Cancelling", "Ambient Sound", "Off"]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.xl
        spacing: Theme.lg

        ColumnLayout {
            spacing: 4
            Text {
                text: "Ambient Sound Control"
                color: Theme.textPrimary
                font.family: Theme.displayFont
                font.pixelSize: Theme.display
                font.weight: Font.Bold
            }
            Text {
                text: "Fine-tune how much of the outside world you want to hear."
                color: Theme.textSecondary
                font.pixelSize: Theme.bodyLg
            }
            StatusChip {
                Layout.topMargin: Theme.xs
                active: Hp.ambientAvailable
                status: Hp.settingStatus.ambient
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.lg

            // ---- Hero card: mode + level ----
            Rectangle {
                Layout.fillWidth: true
                Layout.minimumWidth: 340
                Layout.alignment: Qt.AlignTop
                radius: Theme.radiusLg
                color: Theme.surfaceContainer
                border.color: Theme.outlineVariant
                border.width: 1
                implicitHeight: heroCol.implicitHeight + 2 * Theme.lg

                ColumnLayout {
                    id: heroCol
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.lg
                    spacing: Theme.md

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            spacing: 0
                            Text {
                                text: root.modeNames[Hp.ambientMode]
                                color: Theme.textPrimary
                                font.pixelSize: Theme.headlineMd
                                font.weight: Font.DemiBold
                                font.family: Theme.displayFont
                            }
                            Text {
                                text: Hp.ambientMode === 0 ? "Blocking outside sound"
                                    : Hp.ambientMode === 1 ? ("Letting sound in - level " + Hp.asmLevel)
                                    : "ANC and ambient sound are disabled"
                                color: Theme.textSecondary
                                font.pixelSize: Theme.labelMd
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Icon { name: "hearing"; size: 22; color: Theme.primary; Layout.alignment: Qt.AlignVCenter }
                    }

                    SegmentedControl {
                        Layout.fillWidth: true
                        options: ["Noise Cancelling", "Ambient Sound", "Off"]
                        currentIndex: Hp.ambientMode
                        onActivated: (i) => Hp.ambientMode = i
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.base
                        opacity: Hp.ambientMode === 1 ? 1.0 : 0.45

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "Quieter"
                                color: Theme.textSecondary
                                font.pixelSize: Theme.labelSm
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "More Ambient Sound"
                                color: Theme.textSecondary
                                font.pixelSize: Theme.labelSm
                            }
                        }

                        AudioSlider {
                            id: asmSlider
                            Layout.fillWidth: true
                            from: 1
                            to: Math.max(1, Hp.asmMaximum)
                            stepSize: 1
                            enabled: Hp.ambientMode === 1 && Hp.asmMaximum > 1
                            onMoved: Hp.asmLevel = Math.round(value)
                            // Survives interactive drags; a plain `value:` binding would not.
                            Binding on value { value: Hp.asmLevel }
                        }
                    }
                }
            }

            // ---- Right column ----
            ColumnLayout {
                Layout.preferredWidth: 280
                Layout.maximumWidth: 280
                Layout.alignment: Qt.AlignTop
                spacing: Theme.lg

                Rectangle {
                    Layout.fillWidth: true
                    radius: Theme.radiusLg
                    color: Theme.surfaceContainer
                    border.color: Theme.outlineVariant
                    border.width: 1
                    implicitHeight: focusCol.implicitHeight + 2 * Theme.lg

                    ColumnLayout {
                        id: focusCol
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: Theme.lg
                        spacing: Theme.base

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.base
                            Icon { name: "record_voice_over"; size: 20; color: Theme.primary; Layout.alignment: Qt.AlignVCenter }
                            Text {
                                text: "Focus on Voice"
                                color: Theme.textPrimary
                                font.pixelSize: Theme.labelLg
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            PillSwitch {
                                checked: Hp.focusOnVoice
                                enabled: Hp.focusOnVoiceAvailable
                                onToggled: (on) => Hp.focusOnVoice = on
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Hp.focusOnVoiceAvailable
                                  ? "Prioritise human voices while suppressing background noise."
                                  : "Unavailable for the current ambient mode or device."
                            color: Theme.textSecondary
                            font.pixelSize: Theme.bodyMd
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    visible: Hp.supportsOptimizer
                    radius: Theme.radiusLg
                    color: Theme.surfaceContainer
                    border.color: Theme.outlineVariant
                    border.width: 1
                    implicitHeight: optCol.implicitHeight + 2 * Theme.lg

                    ColumnLayout {
                        id: optCol
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: Theme.lg
                        spacing: Theme.base

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.base
                            Icon { name: "tune"; size: 20; color: Theme.primary; Layout.alignment: Qt.AlignVCenter }
                            Text {
                                Layout.fillWidth: true
                                text: "Noise Cancelling Optimizer"
                                color: Theme.textPrimary
                                font.pixelSize: Theme.labelLg
                                font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Analyses wearing conditions and atmospheric pressure to optimise noise cancelling."
                            color: Theme.textSecondary
                            font.pixelSize: Theme.bodyMd
                            wrapMode: Text.WordWrap
                        }
                        AuralButton {
                            Layout.fillWidth: true
                            text: Hp.optimizing ? "Optimising... (cancel)" : "Start Optimisation"
                            onClicked: Hp.optimizing ? Hp.cancelOptimize() : Hp.startOptimize()
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
