import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Aural

ApplicationWindow {
    id: win
    visible: true
    width: 1000
    height: 720
    minimumWidth: 820
    minimumHeight: 600
    title: Hp.connected ? "Aural - " + Hp.deviceModel : "Aural"
    color: Theme.bg

    // Open on the Dashboard once connected.
    property int currentScreen: 0

    // Screens 6 (System) and 7 (About) work without a device.
    readonly property var navItems: [
        { label: "Dashboard",         screen: 0, cap: "",    icon: "dashboard" },
        { label: "Ambient Sound",     screen: 1, cap: "",    icon: "hearing" },
        { label: "Equalizer",         screen: 2, cap: "",    icon: "equalizer" },
        { label: "Spatial Sound",     screen: 3, cap: "",    icon: "surround_sound" },
        { label: "Speak-to-Chat",     screen: 4, cap: "s2c", icon: "record_voice_over" },
        { label: "Connected Devices", screen: 5, cap: "mp",  icon: "devices" }
    ]
    readonly property var bottomNavItems: [
        { label: "System",            screen: 6, cap: "",    icon: "settings" },
        { label: "About",             screen: 7, cap: "",    icon: "info" }
    ]

    function capEnabled(cap) {
        if (cap === "s2c") return Hp.supportsSpeakToChat
        if (cap === "mp")  return Hp.supportsMultipoint
        return true
    }

    // Match the native title bar to the theme.
    Component.onCompleted: Hp.applyWindowTheme(win, Theme.dark)
    Connections {
        target: Theme
        function onDarkChanged() { Hp.applyWindowTheme(win, Theme.dark) }
    }

    // Dev convenience: cycle screens for screenshots without a mouse.
    Shortcut {
        sequences: ["F8"]
        onActivated: win.currentScreen = (win.currentScreen + 1) % 8
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---------------- Sidebar ----------------
        Rectangle {
            Layout.preferredWidth: Theme.sidebarWidth
            Layout.fillHeight: true
            color: Theme.surfaceContainerLow

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.lg
                    spacing: Theme.sm

                    Rectangle {
                        width: 36; height: 36; radius: 18
                        color: Theme.primaryContainer
                        Icon {
                            anchors.centerIn: parent
                            name: "headset"
                            size: 22
                            color: Theme.primaryContainerText
                        }
                    }
                    ColumnLayout {
                        spacing: 0
                        Text {
                            text: "Aural"
                            color: Theme.textPrimary
                            font.family: Theme.displayFont
                            font.pixelSize: Theme.headlineMd
                            font.weight: Font.Bold
                        }
                        Text {
                            text: "v2.1.0"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.labelSm
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                Repeater {
                    model: win.navItems
                    delegate: NavItem {
                        Layout.fillWidth: true
                        label: modelData.label
                        iconName: modelData.icon
                        selected: win.currentScreen === modelData.screen
                        enabled: Hp.connected && !Hp.isV2 && win.capEnabled(modelData.cap)
                        onClicked: win.currentScreen = modelData.screen
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.sm
                    Layout.rightMargin: Theme.sm
                    Layout.preferredHeight: 1
                    color: Theme.outlineVariant
                }

                Repeater {
                    model: win.bottomNavItems
                    delegate: NavItem {
                        Layout.fillWidth: true
                        label: modelData.label
                        iconName: modelData.icon
                        selected: win.currentScreen === modelData.screen
                        onClicked: win.currentScreen = modelData.screen
                    }
                }

                Item { Layout.preferredHeight: Theme.sm }
            }
        }

        // ---------------- Main column ----------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Top device bar (always visible)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.topBarHeight
                color: Theme.surfaceContainerLowest

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.lg
                    anchors.rightMargin: Theme.lg
                    spacing: Theme.sm

                    Text {
                        text: Hp.connected ? Hp.deviceModel : "No device connected"
                        color: Hp.connected ? Theme.textPrimary : Theme.textSecondary
                        font.family: Theme.displayFont
                        font.pixelSize: Theme.headlineMd
                        font.weight: Hp.connected ? Font.DemiBold : Font.Normal
                    }

                    Rectangle {
                        visible: Hp.connected
                        radius: Theme.radiusFull
                        color: Theme.surfaceContainerHigh
                        implicitWidth: badgeRow.implicitWidth + Theme.md
                        implicitHeight: 24
                        RowLayout {
                            id: badgeRow
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle { width: 8; height: 8; radius: 4; color: Theme.success }
                            Text {
                                text: "Connected"
                                color: Theme.textSecondary
                                font.pixelSize: Theme.labelMd
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        visible: Hp.connected && Hp.syncing
                        text: "syncing..."
                        color: Theme.textSecondary
                        font.pixelSize: Theme.labelMd
                    }
                    Icon {
                        visible: Hp.connected
                        name: "bluetooth"
                        size: 18
                        color: Theme.textSecondary
                    }
                    Row {
                        visible: Hp.connected
                        spacing: 4
                        Icon {
                            name: Hp.batteryCharging ? "battery_charging_full" : "battery_full"
                            size: 18
                            color: Hp.batteryCharging ? Theme.success : Theme.textSecondary
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: Hp.batteryLevel >= 0 ? Hp.batteryLevel + "%" : "..."
                            color: Theme.textPrimary
                            font.pixelSize: Theme.bodyMd
                        }
                    }
                    Icon {
                        visible: Hp.connected
                        name: "refresh"
                        size: 18
                        color: Theme.textSecondary
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Hp.refreshBattery() }
                    }

                    AuralButton { visible: Hp.connected; text: "Disconnect"; danger: true; onClicked: Hp.disconnectDevice() }
                }
            }

            // Content. System (6) and About (7) are reachable even with no device;
            // everything else falls back to Discovery / the v2 placeholder.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                DiscoveryScreen {
                    anchors.fill: parent
                    visible: !Hp.connected && win.currentScreen < 6
                }

                PlaceholderScreen {
                    anchors.fill: parent
                    visible: Hp.connected && Hp.isV2 && win.currentScreen < 6
                    title: "Limited support"
                    message: Hp.deviceModel + " uses Sony's v2 protocol. Basic connection works; full control is coming next."
                }

                StackLayout {
                    anchors.fill: parent
                    visible: (Hp.connected && !Hp.isV2) || win.currentScreen >= 6
                    currentIndex: win.currentScreen

                    DashboardScreen {}
                    AmbientScreen {}
                    EqualizerScreen {}
                    SpatialScreen {}
                    SpeakToChatScreen {}
                    ConnectedDevicesScreen {}
                    SystemScreen {}
                    AboutScreen {}
                }
            }
        }
    }

    Toast {
        id: appToast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.lg
    }

    Connections {
        target: Hp
        function onToast(message) { appToast.show(message, false) }
        function onErrorOccurred(message) { appToast.show(message, true) }
    }
}
