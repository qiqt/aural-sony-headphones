import QtQuick
import QtQuick.Layouts
import Aural

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.xl
        spacing: Theme.lg

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Text { text: "Connected Devices"; color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: Theme.display; font.weight: Font.Bold }
                Text { text: "Multipoint lets your headphones hold two connections at once."; color: Theme.textSecondary; font.pixelSize: Theme.bodyLg }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                radius: Theme.radiusFull; color: Theme.surfaceContainerHigh
                implicitWidth: slotsT.implicitWidth + Theme.md; implicitHeight: 26
                Text { id: slotsT; anchors.centerIn: parent; text: Hp.mpDevices.length + " of 2 slots used"; color: Theme.textSecondary; font.pixelSize: Theme.labelMd }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.lg

            Repeater {
                model: Hp.mpDevices
                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 130
                    radius: Theme.radiusLg; color: Theme.surfaceContainer; border.color: Theme.outlineVariant; border.width: 1
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.base
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.sm
                            Icon { name: (modelData.name.indexOf("Phone") >= 0) ? "smartphone" : "laptop_mac"; size: 24; color: Theme.textSecondary; Layout.alignment: Qt.AlignVCenter }
                            Text { text: modelData.name; color: Theme.textPrimary; font.pixelSize: Theme.headlineMd; font.weight: Font.DemiBold; font.family: Theme.displayFont }
                            Item { Layout.fillWidth: true }
                            Icon { name: modelData.slot === 1 ? "check_circle" : "more_vert"; size: 18; color: modelData.slot === 1 ? Theme.success : Theme.textSecondary; Layout.alignment: Qt.AlignVCenter }
                        }
                        Text { text: modelData.status; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
                        Item { Layout.fillHeight: true }
                        AuralButton { text: "Disconnect"; danger: true; onClicked: Hp.mpDisconnect(modelData.slot) }
                    }
                }
            }

            // empty slot
            Rectangle {
                visible: Hp.mpDevices.length < 2
                Layout.fillWidth: true
                Layout.preferredHeight: 130
                radius: Theme.radiusLg
                color: "transparent"
                border.color: Theme.outlineVariant; border.width: 1
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.base
                    Text { Layout.alignment: Qt.AlignHCenter; text: "+"; color: Theme.textSecondary; font.pixelSize: 28 }
                    Text { Layout.alignment: Qt.AlignHCenter; text: "Open slot"; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
                }
            }
        }

        // explainer
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusLg; color: Theme.surfaceContainerLow; border.color: Theme.outlineVariant; border.width: 1
            implicitHeight: expCol.implicitHeight + 2 * Theme.lg
            ColumnLayout {
                id: expCol
                anchors.fill: parent; anchors.margins: Theme.lg; spacing: Theme.base
                Text { text: "How Multipoint works"; color: Theme.textPrimary; font.pixelSize: Theme.labelLg; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    text: "Your headphones can connect to two devices at once. The device currently playing becomes the active audio source; to switch, just start playback on the other device. Incoming calls automatically take focus."
                    color: Theme.textSecondary; font.pixelSize: Theme.bodyMd; wrapMode: Text.WordWrap
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
