import QtQuick
import QtQuick.Layouts
import Aural

Item {
    id: root
    readonly property var positions: ["Front Left","Front Right","Front","Rear Left","Rear Right"]   // -> soundPosition 1..5
    readonly property var environments: ["Outdoor Festival","Arena","Concert Hall","Club"]           // -> vptType 1..4

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.xl
        spacing: Theme.lg

        ColumnLayout {
            spacing: 4
            Text { text: "Spatial Sound"; color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: Theme.display; font.weight: Font.Bold }
            Text { text: "Virtual positioning and surround effects. Only one can be active at a time."; color: Theme.textSecondary; font.pixelSize: Theme.bodyLg }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.lg

            // --- Sound Position ---
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.sm
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sm
                        Icon { name: "place"; size: 22; color: Theme.primary; Layout.alignment: Qt.AlignVCenter }
                        ColumnLayout { spacing: 0
                            Text { text: "Sound Position"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold; font.family: Theme.displayFont }
                            Text { text: "Direct audio from a fixed point"; color: Theme.textSecondary; font.pixelSize: Theme.labelMd }
                        }
                        Item { Layout.fillWidth: true }
                        PillSwitch { checked: Hp.soundPosition !== 0; onToggled: (on) => Hp.soundPosition = on ? 3 : 0 }
                    }
                    Item { Layout.fillHeight: true }
                    Text { text: "Select position"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                    AuralCombo {
                        Layout.fillWidth: true
                        enabled: Hp.soundPosition !== 0
                        model: root.positions
                        onActivated: (i) => Hp.soundPosition = i + 1
                        Binding on currentIndex { value: Hp.soundPosition > 0 ? Hp.soundPosition - 1 : 0 }
                    }
                }
            }

            // --- Surround (VPT) ---
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.sm
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sm
                        Icon { name: "graphic_eq"; size: 22; color: Theme.primary; Layout.alignment: Qt.AlignVCenter }
                        ColumnLayout { spacing: 0
                            Text { text: "Surround (VPT)"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold; font.family: Theme.displayFont }
                            Text { text: "Virtual venue acoustics"; color: Theme.textSecondary; font.pixelSize: Theme.labelMd }
                        }
                        Item { Layout.fillWidth: true }
                        PillSwitch { checked: Hp.vptType !== 0; onToggled: (on) => Hp.vptType = on ? 1 : 0 }
                    }
                    Item { Layout.fillHeight: true }
                    Text { text: "Select environment"; color: Theme.textSecondary; font.pixelSize: Theme.labelSm }
                    AuralCombo {
                        Layout.fillWidth: true
                        enabled: Hp.vptType !== 0
                        model: root.environments
                        onActivated: (i) => Hp.vptType = i + 1
                        Binding on currentIndex { value: Hp.vptType > 0 ? Hp.vptType - 1 : 0 }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: Hp.soundPosition !== 0 || Hp.vptType !== 0
            radius: Theme.radius
            color: Theme.surfaceContainerLow
            border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: exRow.implicitHeight + Theme.md
            RowLayout {
                id: exRow
                anchors.fill: parent; anchors.margins: Theme.sm; spacing: Theme.base
                Rectangle { width: 6; height: 6; radius: 3; color: Theme.primary; Layout.alignment: Qt.AlignVCenter }
                Text {
                    Layout.fillWidth: true
                    text: Hp.soundPosition !== 0
                          ? "Sound Position is active. Surround (VPT) is turned off - only one spatial effect can run at a time."
                          : "Surround (VPT) is active. Sound Position is turned off - only one spatial effect can run at a time."
                    color: Theme.textSecondary; font.pixelSize: Theme.labelMd; wrapMode: Text.WordWrap
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
