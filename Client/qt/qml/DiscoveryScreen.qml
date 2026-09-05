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
            spacing: Theme.md

            ColumnLayout {
                spacing: 4
                Text {
                    text: Hp.connecting ? "Connecting..." : "Devices Nearby"
                    color: Theme.textPrimary
                    font.family: Theme.displayFont
                    font.pixelSize: Theme.display
                    font.weight: Font.Bold
                }
                Text {
                    text: "Select a headphone to connect."
                    color: Theme.textSecondary
                    font.pixelSize: Theme.bodyLg
                }
            }

            Item { Layout.fillWidth: true }

            AuralButton {
                text: Hp.discovering ? "Scanning..." : "Scan again"
                enabled: !Hp.discovering && !Hp.connecting
                onClicked: Hp.refreshDevices()
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.md

            Repeater {
                model: Hp.devices
                delegate: Rectangle {
                    width: 240
                    height: 188
                    radius: Theme.radiusLg
                    color: Theme.surfaceContainer
                    border.color: Theme.outlineVariant
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.md
                        spacing: Theme.sm

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 64
                            radius: Theme.radius
                            color: Theme.surfaceContainerHigh
                            Icon { anchors.centerIn: parent; name: "headset"; size: 32; color: Theme.textSecondary }
                        }

                        Text {
                            text: modelData.model
                            color: Theme.textPrimary
                            font.pixelSize: Theme.bodyLg
                            font.weight: Font.DemiBold
                        }
                        Row {
                            spacing: 6
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                anchors.verticalCenter: parent.verticalCenter
                                color: modelData.isV2 ? Theme.error : Theme.success
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.badge
                                color: Theme.textSecondary
                                font.pixelSize: Theme.labelMd
                            }
                        }

                        Item { Layout.fillHeight: true }

                        AuralButton {
                            Layout.fillWidth: true
                            primary: true
                            text: "Connect"
                            enabled: !Hp.connecting
                            onClicked: Hp.connectTo(modelData.mac, modelData.name)
                        }
                    }
                }
            }
        }

        Text {
            visible: Hp.devices.length === 0 && !Hp.discovering
            Layout.fillWidth: true
            text: "No connected headphones found. Pair and connect them in Windows Bluetooth settings, then press Scan again."
            color: Theme.textSecondary
            font.pixelSize: Theme.bodyMd
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }

        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 2
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Don't see your device?"
                color: Theme.textPrimary
                font.pixelSize: Theme.bodyMd
                font.weight: Font.DemiBold
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Make sure it's paired and connected in Windows Bluetooth settings."
                color: Theme.textSecondary
                font.pixelSize: Theme.labelMd
            }
        }
    }
}
