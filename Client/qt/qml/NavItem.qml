import QtQuick
import Aural

Item {
    id: root
    property string label: ""
    property string iconName: ""
    property bool selected: false
    signal clicked()

    implicitHeight: 46
    implicitWidth: Theme.sidebarWidth
    opacity: enabled ? 1.0 : 0.4

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.sm
        anchors.rightMargin: Theme.sm
        anchors.topMargin: 2
        anchors.bottomMargin: 2
        radius: Theme.radius
        color: root.selected ? Theme.surfaceContainerHigh : "transparent"
    }

    Rectangle {
        visible: root.selected
        width: 3
        height: parent.height * 0.45
        anchors.verticalCenter: parent.verticalCenter
        radius: 2
        color: Theme.primary
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Theme.lg
        spacing: Theme.sm

        Icon {
            name: root.iconName
            size: 20
            anchors.verticalCenter: parent.verticalCenter
            color: root.selected ? Theme.primary : Theme.textSecondary
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.selected ? Theme.textPrimary : Theme.textSecondary
            font.pixelSize: Theme.bodyLg
            font.weight: root.selected ? Font.DemiBold : Font.Normal
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
