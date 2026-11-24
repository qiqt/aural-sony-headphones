import QtQuick
import QtTest
import Aural

Item {
    width: 1000; height: 900
    FontLoader { source: "../../fonts/Manrope.ttf" }
    FontLoader { source: "../../fonts/SpaceGrotesk.ttf" }
    FontLoader { source: "../../fonts/MaterialIconsOutlined.otf" }
    Component { id: v2; V2Screen { width: 1000; height: 900; Rectangle { anchors.fill: parent; color: Theme.bg; z: -1 } } }
    Component { id: dashboard; DashboardScreen { width: 1000; height: 720; Rectangle { anchors.fill: parent; color: Theme.bg; z: -1 } } }
    Component { id: equalizer; EqualizerScreen { width: 1000; height: 800; Rectangle { anchors.fill: parent; color: Theme.bg; z: -1 } } }
    Component { id: devices; ConnectedDevicesScreen { width: 1000; height: 720; Rectangle { anchors.fill: parent; color: Theme.bg; z: -1 } } }
    TestCase {
        name: "Controls"
        when: windowShown
        function init() { Hp.reset(); Hp.settingStatus = {equalizer: "confirmed"}; Hp.dseeAvailable = false }
        function findText(item, text) {
            if (item.text !== undefined && item.text.indexOf(text) === 0) return item
            for (let child of item.children) { let found = findText(child, text); if (found) return found }
            return null
        }
        function test_v2_drag_survives_updates_data() {
            return [{tag: "ambient", target: "v2AmbientSlider"}, {tag: "equalizer", target: "v2EqSlider"}]
        }
        function test_v2_drag_survives_updates(data) {
            let screen = createTemporaryObject(v2, parent)
            verify(screen)
            wait(100)
            let slider = findChild(screen, data.target)
            verify(slider)
            const y = slider.height / 2
            const start = slider.leftPadding + slider.visualPosition * slider.availableWidth
            mousePress(slider, start, y)
            mouseMove(slider, slider.width * 0.65, y, 40)
            wait(30)
            compare(findChild(screen, data.target), slider)
            verify(slider.pressed)
            const heldValue = slider.value
            let stale = JSON.parse(JSON.stringify(Hp.v2Info))
            if (data.tag === "ambient") stale.ambient[0].level = 1
            else stale.equalizers[0].bands[0].value = -10
            Hp.v2Info = stale
            wait(10)
            compare(slider.value, heldValue)
            const first = Hp.writes
            mouseMove(slider, slider.width * 0.85, y, 40)
            verify(Hp.writes > first)
            mouseRelease(slider, slider.width * 0.85, y)
            slider.forceActiveFocus()
            const value = slider.value
            keyClick(Qt.Key_Left)
            verify(slider.value < value)
            grabImage(screen).save("v2-" + data.tag + "-controls.png")
        }
        function test_v2_clear_and_shrink_without_warnings() {
            failOnWarning(/.*/)
            let populated = JSON.parse(JSON.stringify(Hp.v2Info))
            populated.audio = [{title: "DSEE", ready: true, selector: 1, value: 0, status: "confirmed"}]
            Hp.v2Info = populated
            let screen = createTemporaryObject(v2, parent)
            verify(screen)
            wait(100)
            let smaller = JSON.parse(JSON.stringify(Hp.v2Info))
            smaller.equalizers[0].bands = []
            smaller.audio = []
            Hp.v2Info = smaller
            wait(20)
            Hp.v2Info = {}
            wait(20)
            compare(findChild(screen, "v2AmbientSlider"), null)
            compare(findChild(screen, "v2EqSlider"), null)
            Hp.reset()
            wait(20)
            verify(findChild(screen, "v2AmbientSlider"))
            verify(findChild(screen, "v2EqSlider"))
        }
        function test_quick_features_visibility() {
            let screen = createTemporaryObject(dashboard, parent)
            wait(100)
            let label = findText(screen, "QUICK FEATURES")
            verify(label)
            compare(label.visible, false)
            Hp.dseeAvailable = true
            tryCompare(label, "visible", true)
            grabImage(screen).save("dashboard-controls.png")
        }
        function test_disconnect_targets_displayed_address() {
            const mac = "AA:BB:CC:DD:EE:FF"
            Hp.mpDevices = [{name: "Phone", mac: mac, status: "Active", slot: 1}]
            Hp.lastDisconnected = ""
            let screen = createTemporaryObject(devices, parent)
            wait(100)
            let button = findText(screen, "Disconnect")
            verify(button)
            mouseClick(button)
            compare(Hp.lastDisconnected, mac)
            Hp.lastDisconnected = ""
            button.forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(Hp.lastDisconnected, mac)
            grabImage(screen).save("connected-devices-controls.png")
        }
        function test_equalizer_notice_does_not_move_pending_controls() {
            let screen = createTemporaryObject(equalizer, parent)
            wait(100)
            let notice = findText(screen, "Not yet read")
            verify(notice)
            compare(notice.visible, false)
            for (let status of ["requested", "acknowledged", "failed"]) {
                Hp.settingStatus = {equalizer: status}
                wait(10)
                compare(notice.visible, false)
            }
            Hp.settingStatus = {equalizer: "unknown"}
            tryCompare(notice, "visible", true)
            grabImage(screen).save("equalizer-controls.png")
        }
    }
}
