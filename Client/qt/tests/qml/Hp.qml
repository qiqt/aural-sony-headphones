pragma Singleton
import QtQuick

QtObject {
    signal eqChanged()
    property string deviceModel: "Simulated headphones"
    property var diagnostics: ({firmware: "test"})
    property bool syncing: false
    property bool ambientAvailable: true
    property int ambientMode: 1
    property int asmLevel: 10
    property bool equalizerAvailable: true
    property bool dseeAvailable: false
    property bool voiceGuidanceAvailable: false
    property bool systemSettingsAvailable: true
    property bool supportsSpeakToChat: true
    property bool supportsMultipoint: false
    property bool dsee: false
    property bool voiceGuidance: false
    property var mpDevices: []
    property string lastDisconnected: ""
    function mpDisconnect(mac) { lastDisconnected = mac }
    property var settingStatus: ({equalizer: "confirmed"})
    property int eqPreset: 9
    property bool eqSavePending: false
    property var eqPresetOptions: [0, 9]
    property bool eqBandsEditable: true
    property bool eqManual: true
    property bool eqCanSave: false
    property int eqMinimum: -10
    property int eqMaximum: 10
    property int eqClearBass: 0
    property var eqBands: [0, 0, 0, 0, 0]
    property int writes: 0
    property var v2Info: ({})
    function reset() {
        writes = 0
        v2Info = {phase: "ready", message: "Ready", audio: [],
            ambient: [{selector: 0, ready: true, mode: 2, level: 3, minimum: 1, maximum: 20, step: 1, voice: false, voiceSupported: true, status: "confirmed"}],
            equalizers: [{selector: 0, title: "Equalizer", ready: true, read: true, bandsEditable: true, minimum: -10, maximum: 10, presetIds: [], names: [], selected: -1, status: "confirmed", bands: [{index: 0, label: "400 Hz", value: 0}]}]}
    }
    function setV2Ambient(selector, mode, level, voice) {
        let next = JSON.parse(JSON.stringify(v2Info))
        next.ambient[0].level = level
        next.ambient[0].mode = mode
        next.ambient[0].voice = voice
        next.ambient[0].status = "requested"
        v2Info = next
        writes++
    }
    function setV2EqBand(selector, band, value) {
        let next = JSON.parse(JSON.stringify(v2Info))
        next.equalizers[0].bands[band].value = value
        next.equalizers[0].status = "requested"
        v2Info = next
        writes++
    }
    function refreshV2Values() {}
    function setEqBand(index, value) { let bands = eqBands.slice(); bands[index] = value; eqBands = bands }
}
