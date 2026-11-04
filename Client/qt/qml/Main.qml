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
        { label: "Dashboard",         screen: 0, cap: "",        icon: "dashboard" },
        { label: "Ambient Sound",     screen: 1, cap: "ambient", icon: "hearing" },
        { label: "Equalizer",         screen: 2, cap: "eq",      icon: "equalizer" },
        { label: "Spatial Sound",     screen: 3, cap: "spatial", icon: "surround_sound" },
        { label: "Speak-to-Chat",     screen: 4, cap: "s2c",     icon: "record_voice_over" },
        { label: "Connected Devices", screen: 5, cap: "mp",      icon: "devices" }
    ]
    readonly property var bottomNavItems: [
        { label: "System",            screen: 6, cap: "",        icon: "settings" },
        { label: "About",             screen: 7, cap: "",        icon: "info" }
    ]

    // What this connection is actually allowed to control. The controller decides (SON-11);
    // this only mirrors it, so the nav and the wire can never disagree.
    function capEnabled(cap) {
        if (cap === "ambient") return Hp.ambientAvailable
        if (cap === "eq")      return Hp.equalizerAvailable
        if (cap === "spatial") return Hp.spatialAvailable
        if (cap === "s2c")     return Hp.supportsSpeakToChat
        if (cap === "mp")      return Hp.supportsMultipoint
        return true
    }

    // A screen that stops being available must not stay on screen - switching device can take
    // the current one away underneath the user.
    function screenAvailable(screen) {
        for (var i = 0; i < navItems.length; ++i)
            if (navItems[i].screen === screen)
                return capEnabled(navItems[i].cap)
        return true
    }

    Connections {
        target: Hp
        function onCapabilitiesChanged() {
            if (!win.screenAvailable(win.currentScreen))
                win.currentScreen = 0
        }
    }

    // Theme is an app preference, not a device setting: restore it from AppSettings on
    // startup and write it back whenever it changes. Native title-bar chrome is handled by
    // the Platform helper, not by the device controller.
    Component.onCompleted: {
        Theme.dark = App.darkTheme
        Platform.applyTheme(win, Theme.dark)
    }
    Connections {
        target: Theme
        function onDarkChanged() {
            App.darkTheme = Theme.dark
            Platform.applyTheme(win, Theme.dark)
        }
    }

    // Dev convenience: cycle screens for screenshots without a mouse.
    Shortcut {
        sequences: ["F8"]
        onActivated: win.currentScreen = (win.currentScreen + 1) % 8
    }

    // Theme toggle. Also the keyboard route to the System screen's Light/Dark control.
    Shortcut {
        sequences: ["Ctrl+T"]
        onActivated: Theme.dark = !Theme.dark
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
                        implicitWidth: 36; implicitHeight: 36; radius: 18
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
                            text: "v" + Build.version
                            color: Theme.textSecondary
                            font.pixelSize: Theme.labelSm
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                Repeater {
                    model: win.navItems
                    delegate: NavItem {
                        // With no headphones connected, the first entry is the way back to the
                        // device list (screens 0-5 show it then). Without it, System and About
                        // were a dead end.
                        readonly property bool devicesEntry: modelData.screen === 0 && !Hp.connected
                        Layout.fillWidth: true
                        label: devicesEntry ? "Devices" : modelData.label
                        iconName: devicesEntry ? "headset" : modelData.icon
                        selected: win.currentScreen === modelData.screen
                        enabled: modelData.screen === 0 || (Hp.connected && !Hp.isV2 && win.capEnabled(modelData.cap))
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

                    // The Bluetooth name didn't match a model we know, so we are not
                    // claiming one - and the protocol choice is an attempt, not a fact.
                    Rectangle {
                        visible: Hp.connected && !Hp.modelRecognised
                        radius: Theme.radiusFull
                        color: Theme.surfaceContainerHigh
                        border.width: 1
                        border.color: Theme.outlineVariant
                        implicitWidth: unknownLabel.implicitWidth + Theme.md
                        implicitHeight: 24
                        Text {
                            id: unknownLabel
                            anchors.centerIn: parent
                            text: "Unrecognised model"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.labelMd
                        }
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
                            Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: Theme.success }
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

            // Says out loud why most of the app is missing, rather than leaving the user to
            // wonder whether it is broken.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: limitedText.implicitHeight + Theme.lg
                visible: Hp.limitedControlReason !== "" && win.currentScreen < 6
                color: Theme.surfaceContainerHigh

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.lg
                    anchors.rightMargin: Theme.lg
                    spacing: Theme.sm

                    Icon { name: "info"; size: 18; color: Theme.textSecondary }
                    Text {
                        id: limitedText
                        Layout.fillWidth: true
                        text: Hp.limitedControlReason
                        color: Theme.textSecondary
                        font.pixelSize: Theme.labelLg
                        wrapMode: Text.WordWrap
                    }
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

                V2Screen {
                    anchors.fill: parent
                    visible: Hp.connected && Hp.isV2 && win.currentScreen < 6
                }

                StackLayout {
                    anchors.fill: parent
                    visible: (Hp.connected && !Hp.isV2) || win.currentScreen >= 6
                    currentIndex: win.currentScreen

                    // Screens ask to navigate; Main owns which screen is showing.
                    DashboardScreen { onNavigate: (screen) => win.currentScreen = screen }
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

    // Shown whichever screen is open: the headphones ask when they are ready to.
    MultipointConfirmDialog {}

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
