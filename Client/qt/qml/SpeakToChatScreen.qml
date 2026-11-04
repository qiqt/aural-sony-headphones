import QtQuick
import QtQuick.Layouts
import Aural

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.xl
        spacing: Theme.lg

        ColumnLayout {
            spacing: 4
            Text { text: "Speak-to-Chat"; color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: Theme.display; font.weight: Font.Bold }
            Text { text: "Automatically pause audio and let ambient sound in when you start talking."; color: Theme.textSecondary; font.pixelSize: Theme.bodyLg }
            StatusChip {
                Layout.topMargin: Theme.xs
                active: Hp.supportsSpeakToChat
                status: Hp.settingStatus.speakToChat
            }
        }

        // master toggle
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: masterRow.implicitHeight + 2 * Theme.lg
            RowLayout {
                id: masterRow
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.md
                Rectangle {
                    implicitWidth: 40; implicitHeight: 40; radius: Theme.radius
                    color: Theme.primaryContainer
                    Icon { anchors.centerIn: parent; name: "record_voice_over"; size: 22; color: Theme.primaryContainerText }
                }
                ColumnLayout { spacing: 2
                    Text { text: "Speak-to-Chat"; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold; font.family: Theme.displayFont }
                    Text { text: "Pause audio when you speak"; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
                }
                Item { Layout.fillWidth: true }
                PillSwitch { checked: Hp.s2cEnabled; onToggled: (on) => Hp.s2cEnabled = on }
            }
        }

        // Sensitivity
        Rectangle {
            Layout.fillWidth: true
            visible: Hp.s2cEnabled
            radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: sensCol.implicitHeight + 2 * Theme.lg
            ColumnLayout {
                id: sensCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.sm
                Text { text: "Voice Detection Sensitivity"; color: Theme.textPrimary; font.pixelSize: Theme.labelLg; font.weight: Font.DemiBold }
                SegmentedControl { Layout.fillWidth: true; options: ["Auto","High","Low"]; currentIndex: Hp.s2cSensitivity; onActivated: (i) => Hp.s2cSensitivity = i }
                Text { text: "Auto adjusts to the noise level around you."; color: Theme.textSecondary; font.pixelSize: Theme.labelMd }
            }
        }

        // Auto-close
        Rectangle {
            Layout.fillWidth: true
            visible: Hp.s2cEnabled
            radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: timeCol.implicitHeight + 2 * Theme.lg
            ColumnLayout {
                id: timeCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.sm
                Text { text: "Time Until Audio Resumes"; color: Theme.textPrimary; font.pixelSize: Theme.labelLg; font.weight: Font.DemiBold }
                // Sony's FAST / MID / SLOW / NONE. The exact durations have not been verified,
                // so the labels stay relative and NONE says what it does.
                SegmentedControl { Layout.fillWidth: true; options: ["Short","Standard","Long","Never"]; currentIndex: Hp.s2cAutoOff; onActivated: (i) => Hp.s2cAutoOff = i }
                Text {
                    Layout.fillWidth: true
                    text: "How long the headphones wait after you stop talking before resuming audio. Never keeps Speak-to-Chat on until you end it."
                    color: Theme.textSecondary; font.pixelSize: Theme.labelMd; wrapMode: Text.WordWrap
                }
            }
        }

        // Voice passthrough
        Rectangle {
            Layout.fillWidth: true
            visible: Hp.s2cEnabled
            radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: vpRow.implicitHeight + 2 * Theme.lg
            RowLayout {
                id: vpRow
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.md
                ColumnLayout { spacing: 2
                    Text { text: "Focus on voices"; color: Theme.textPrimary; font.pixelSize: Theme.labelLg; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; text: "Reduce background noise while Speak-to-Chat is active."; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd; wrapMode: Text.WordWrap }
                }
                Item { Layout.fillWidth: true }
                PillSwitch { checked: Hp.s2cVoice; onToggled: (on) => Hp.s2cVoice = on }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
