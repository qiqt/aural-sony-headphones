import QtQuick
import QtQuick.Layouts
import Aural

Item {
    id: root

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.xl, 460)
        radius: Theme.radiusLg
        color: Theme.surfaceContainer
        border.color: Theme.outlineVariant
        border.width: 1
        implicitHeight: col.implicitHeight + 2 * Theme.xl

        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: Theme.xl
            spacing: Theme.md

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 64; implicitHeight: 64; radius: 32
                color: Theme.primaryContainer
                Text { anchors.centerIn: parent; text: "A"; color: Theme.primaryContainerText; font.bold: true; font.pixelSize: 30; font.family: Theme.displayFont }
            }
            Text { Layout.alignment: Qt.AlignHCenter; text: "Aural"; color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: Theme.display; font.weight: Font.Bold }
            Text { Layout.alignment: Qt.AlignHCenter; text: "v" + Build.version; color: Theme.textSecondary; font.pixelSize: Theme.bodyMd }
            // The source this build came from - a version number alone cannot identify it.
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: Build.revision === "unknown"
                    ? "Source revision unknown"
                    : (Build.revision + " on " + Build.branch + (Build.modified ? " (modified)" : ""))
                color: Theme.textSecondary
                font.pixelSize: Theme.labelSm
            }

            Rectangle {
                Layout.fillWidth: true
                radius: Theme.radius
                color: Theme.surfaceContainerLow
                implicitHeight: disc.implicitHeight + Theme.md
                Text {
                    id: disc
                    anchors.centerIn: parent
                    width: parent.width - Theme.md
                    text: "Aural is an independent, third-party audio utility. It is not affiliated with, endorsed by, or sponsored by Sony."
                    color: Theme.textSecondary
                    font.pixelSize: Theme.bodyMd
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }

            AuralButton { Layout.fillWidth: true; primary: true; text: "View source on GitHub"; onClicked: Qt.openUrlExternally("https://github.com/qiqt/aural-sony-headphones") }

        }
    }
}
