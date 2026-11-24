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
