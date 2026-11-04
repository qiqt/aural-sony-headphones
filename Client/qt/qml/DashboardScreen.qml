import QtQuick
import QtQuick.Layouts
import Aural

Item {
    id: root

    // Navigation is Main's job: the dashboard asks to go somewhere instead of reaching into
    // another component's context and assigning its screen index directly.
    signal navigate(int screen)

    readonly property var presetNames: ["Off","Bright","Excited","Mellow","Relaxed","Vocal","Treble Boost","Bass Boost","Speech","Manual","Custom 1","Custom 2"]
    readonly property var modeNames: ["Noise Cancelling", "Ambient Sound", "Off"]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.xl
        spacing: Theme.lg

        ColumnLayout {
            spacing: 4
            Text { text: "Dashboard"; color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: Theme.display; font.weight: Font.Bold }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.lg
            rowSpacing: Theme.lg

            // --- Current Sound Mode ---
            // Every card mirrors the controller's availability gates (SON-11): the dashboard
            // is a summary of the pages, so it must not offer what they are hiding.
            Rectangle {
                visible: Hp.ambientAvailable
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
                implicitHeight: modeCol.implicitHeight + 2 * Theme.lg
                ColumnLayout {
                    id: modeCol
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.lg
                    spacing: Theme.sm
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "CURRENT SOUND MODE"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm; font.weight: Font.Bold; font.letterSpacing: 1 }
                        Item { Layout.fillWidth: true }
                        Icon { name: "hearing"; size: 20; color: Theme.primary }
                    }
                    Text { text: root.modeNames[Hp.ambientMode]; color: Theme.primary; font.family: Theme.displayFont; font.pixelSize: Theme.headlineLg; font.weight: Font.Bold }
                    Text {
                        text: Hp.ambientMode === 0 ? "Noise cancelling on"
                            : Hp.ambientMode === 1 ? ("Level " + Hp.asmLevel)
                            : "ANC and ambient sound disabled"
                        color: Theme.textSecondary; font.pixelSize: Theme.bodyMd
                    }
                    Item { Layout.preferredHeight: Theme.xs }
                    SegmentedControl {
                        Layout.fillWidth: true
                        options: ["NC", "Ambient", "Off"]
                        currentIndex: Hp.ambientMode
                        onActivated: (i) => Hp.ambientMode = i
                    }
                }
            }

            // --- Equalizer summary ---
            Rectangle {
                visible: Hp.equalizerAvailable
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
                implicitHeight: eqCol.implicitHeight + 2 * Theme.lg
                ColumnLayout {
                    id: eqCol
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.lg
                    spacing: Theme.sm
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "EQUALIZER"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm; font.weight: Font.Bold; font.letterSpacing: 1 }
                        Item { Layout.fillWidth: true }
                        Rectangle { radius: Theme.radiusFull; color: Theme.primaryContainer; implicitWidth: pn.implicitWidth + Theme.md; implicitHeight: 22
                            Text { id: pn; anchors.centerIn: parent; text: Hp.eqPreset >= 0 ? root.presetNames[Hp.eqPreset] : "Other"; color: Theme.primaryContainerText; font.pixelSize: Theme.labelMd; font.weight: Font.DemiBold } }
                    }
                    EqCurve {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                    }
                    AuralButton { Layout.fillWidth: true; text: "Open Equalizer"; iconName: "tune"; onClicked: root.navigate(2) }
                }
            }

            // --- Quick features ---
            Rectangle {
                visible: Hp.systemSettingsAvailable || Hp.supportsSpeakToChat
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
                implicitHeight: qfCol.implicitHeight + 2 * Theme.lg
                ColumnLayout {
                    id: qfCol
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.lg
                    spacing: Theme.sm
                    Text { text: "QUICK FEATURES"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm; font.weight: Font.Bold; font.letterSpacing: 1 }

                    Rectangle {
                        visible: Hp.dseeAvailable
                        Layout.fillWidth: true
                        radius: Theme.radiusMd; color: Theme.surfaceContainerLow
                        implicitHeight: dseeRow.implicitHeight + 2 * Theme.sm
                        RowLayout {
                            id: dseeRow
                            anchors.fill: parent; anchors.margins: Theme.sm; spacing: Theme.sm
                            ColumnLayout { spacing: 0
                                Text { text: "DSEE Extreme"; color: Theme.textPrimary; font.pixelSize: Theme.bodyMd; font.weight: Font.DemiBold }
                                Text { text: "Restore detail in compressed audio"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                            }
                            Item { Layout.fillWidth: true }
                            PillSwitch { checked: Hp.dsee; onToggled: (on) => Hp.dsee = on }
                        }
                    }
                    Rectangle {
                        visible: Hp.voiceGuidanceAvailable
                        Layout.fillWidth: true
                        radius: Theme.radiusMd; color: Theme.surfaceContainerLow
                        implicitHeight: vgRow.implicitHeight + 2 * Theme.sm
                        RowLayout {
                            id: vgRow
                            anchors.fill: parent; anchors.margins: Theme.sm; spacing: Theme.sm
                            ColumnLayout { spacing: 0
                                Text { text: "Voice Guidance"; color: Theme.textPrimary; font.pixelSize: Theme.bodyMd; font.weight: Font.DemiBold }
                                Text { text: "Spoken status alerts"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                            }
                            Item { Layout.fillWidth: true }
                            PillSwitch { checked: Hp.voiceGuidance; onToggled: (on) => Hp.voiceGuidance = on }
                        }
                    }
                }
            }

            // --- Multipoint ---
            Rectangle {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                visible: Hp.supportsMultipoint
                radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
                implicitHeight: mpCol.implicitHeight + 2 * Theme.lg
                ColumnLayout {
                    id: mpCol
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.lg
                    spacing: Theme.sm
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "MULTIPOINT"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm; font.weight: Font.Bold; font.letterSpacing: 1 }
                        Item { Layout.fillWidth: true }
                        Text { text: Hp.mpDevices.length + " of 2 connected"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                        Text {
                            text: "Manage"
                            color: Theme.primary
                            font.pixelSize: Theme.labelMd
                            font.weight: Font.DemiBold
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.navigate(5) }
                        }
                    }

                    Repeater {
                        model: Hp.mpDevices
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            radius: Theme.radiusMd
                            color: modelData.slot === 1 ? Theme.surfaceContainerHigh : Theme.surfaceContainerLow
                            border.color: modelData.slot === 1 ? Theme.primary : "transparent"
                            border.width: 1
                            implicitHeight: mpRow.implicitHeight + 2 * Theme.sm
                            RowLayout {
                                id: mpRow
                                anchors.fill: parent; anchors.margins: Theme.sm; spacing: Theme.sm
                                Icon { name: (modelData.name.indexOf("Phone") >= 0) ? "smartphone" : "laptop_mac"; size: 20; color: Theme.textSecondary; Layout.alignment: Qt.AlignVCenter }
                                ColumnLayout {
                                    spacing: 0
                                    Text { text: modelData.name; color: Theme.textPrimary; font.pixelSize: Theme.bodyMd; font.weight: Font.DemiBold }
                                    Text { text: modelData.status; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                                }
                                Item { Layout.fillWidth: true }
                                Icon { name: modelData.slot === 1 ? "check_circle" : "more_vert"; size: 18; color: modelData.slot === 1 ? Theme.success : Theme.textSecondary; Layout.alignment: Qt.AlignVCenter }
                            }
                        }
                    }

                    Text {
                        visible: Hp.mpDevices.length === 0
                        text: "No devices reported yet."
                        color: Theme.textSecondary
                        font.pixelSize: Theme.bodyMd
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
