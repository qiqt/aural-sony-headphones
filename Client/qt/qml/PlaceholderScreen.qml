import QtQuick
import Aural

Item {
    id: root
    property string title: ""
    property string message: ""

    Column {
        anchors.centerIn: parent
        spacing: Theme.sm
        width: Math.min(parent.width - 2 * Theme.xl, 520)

        Text {
            text: root.title
            color: Theme.textPrimary
            font.pixelSize: Theme.headlineLg
            font.weight: Font.Bold
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: root.message
            color: Theme.textSecondary
            font.pixelSize: Theme.bodyLg
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }
    }
}
