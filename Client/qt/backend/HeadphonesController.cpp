// Platform connector header must come before Qt headers on Windows so winsock2.h is seen
// first. WIN32_LEAN_AND_MEAN (set in CMake) also keeps windows.h from pulling winsock 1.
#ifdef _WIN32
#include "windows/WindowsBluetoothConnector.h"
#include <dwmapi.h>
#endif

#include "HeadphonesController.h"
#include "Exceptions.h"

#include <QDebug>
#include <QWindow>

#include <algorithm>

namespace {

std::unique_ptr<IBluetoothConnector> makeConnector()
{
#ifdef _WIN32
    return std::make_unique<WindowsBluetoothConnector>();
#else
    return nullptr;
#endif
}

QString friendlyModel(const std::string& name)
{
    auto has = [&](const char* s) { return name.find(s) != std::string::npos; };
    if (has("XM6")) return "WH-1000XM6";
    if (has("XM5")) return "WH-1000XM5";
    if (has("XM4")) return "WH-1000XM4";
    if (has("XM3")) return "WH-1000XM3";
    return QString::fromStdString(name);
}

// UI preset index -> engine EQ_PRESET. Indices 9..11 (Manual/Custom1/Custom2) use the bands.
const EQ_PRESET EQ_PRESETS[] = {
    EQ_PRESET::OFF, EQ_PRESET::BRIGHT, EQ_PRESET::EXCITED, EQ_PRESET::MELLOW,
    EQ_PRESET::RELAXED, EQ_PRESET::VOCAL, EQ_PRESET::TREBLE_BOOST, EQ_PRESET::BASS_BOOST,
    EQ_PRESET::SPEECH, EQ_PRESET::MANUAL, EQ_PRESET::CUSTOM_1, EQ_PRESET::CUSTOM_2
};
constexpr int EQ_MANUAL_INDEX = 9;

QVariantMap mpEntry(const QString& name, const QString& status, int slot)
{
    QVariantMap m;
    m["name"] = name;
    m["status"] = status;
    m["slot"] = slot;
    return m;
}

} // namespace

HeadphonesController::HeadphonesController(QObject* parent)
    : QObject(parent)
    , _bt(makeConnector())
    , _headphones(_bt)
{
    _clock.start();

    _demo = qEnvironmentVariableIsSet("AURAL_DEMO");
    if (_demo)
    {
        // Dev-only: simulate a connected XM4 with sample settings so every screen previews
        // without hardware.
        _device.name = "WH-1000XM4";
        _protocol = SonyProtocol::V1;
        _conn = Conn::Connected;
        _connectionInitialized = true;

        _ambientMode = 1; // Ambient Sound
        _eqPreset = EQ_MANUAL_INDEX;
        _eqClearBass = 2;
        _eqBands = { 4, 1, -2, 3, 6 };
        _s2cEnabled = true;
        _s2cSensitivity = 0;
        _s2cAutoOff = 1;
        _dsee = true;
        _autoPowerOff = 2;
        _soundPosition = 3; // Front
        _mpDevices = QVariantList{
            mpEntry("MacBook Pro", "Active audio source", 1),
            mpEntry("iPhone 15", "Standby", 2)
        };
        return;
    }

    _timer.setInterval(50); // ~20 Hz pump, headless equivalent of the old render loop
    connect(&_timer, &QTimer::timeout, this, &HeadphonesController::pump);
    _timer.start();

    refreshDevices();
}

HeadphonesController::~HeadphonesController()
{
    teardownConnection();
}

QString HeadphonesController::deviceName() const { return QString::fromStdString(_device.name); }
QString HeadphonesController::deviceModel() const { return friendlyModel(_device.name); }

int HeadphonesController::batteryLevel()
{
    if (_demo) return 90;
    int protocolBattery = _headphones.getBatteryLevel();
    return (_osBatteryLevel >= 0) ? _osBatteryLevel : protocolBattery;
}

bool HeadphonesController::batteryCharging() { return _headphones.isBatteryCharging(); }

bool HeadphonesController::supportsOptimizer() { return _headphones.supportsFeature(DEVICE_CAPABILITIES::OPTIMIZER); }
bool HeadphonesController::supportsSpeakToChat() { return _headphones.supportsFeature(DEVICE_CAPABILITIES::SPEAK_TO_CHAT); }
bool HeadphonesController::supportsMultipoint() { return _headphones.supportsFeature(DEVICE_CAPABILITIES::MULTI_POINT); }

bool HeadphonesController::focusOnVoiceAvailable()
{
    if (_demo) return true;
    return _headphones.isFocusOnVoiceAvailable();
}

bool HeadphonesController::optimizing()
{
    return _optimizeRequested || _headphones.getOptimizerState() == OPTIMIZER_STATE::OPTIMIZING;
}

// Translate the UI tri-state into the engine's (ambient on/off, level) pair.
// NC = (on, 0); Ambient = (on, 1-19); Off = effect off.
void HeadphonesController::pushAmbient()
{
    switch (_ambientMode)
    {
    case 0: // Noise Cancelling
        _headphones.setAmbientSoundControl(true);
        _headphones.setAsmLevel(0);
        break;
    case 1: // Ambient Sound
        _headphones.setAmbientSoundControl(true);
        _headphones.setAsmLevel(std::clamp(_asmLevel, 1, 19));
        break;
    default: // Off
        _headphones.setAmbientSoundControl(false);
        break;
    }
    _headphones.setFocusOnVoice(_focusOnVoice);
}

void HeadphonesController::setAmbientMode(int m)
{
    if (m < 0 || m > 2 || _ambientMode == m) return;
    _ambientMode = m;
    pushAmbient();
    emit ambientChanged();
}

void HeadphonesController::setAsmLevel(int v)
{
    v = std::clamp(v, 1, 19);
    if (_asmLevel == v) return;
    _asmLevel = v;
    if (_ambientMode == 1)
        pushAmbient();
    emit ambientChanged();
}

void HeadphonesController::setFocusOnVoice(bool v)
{
    if (_focusOnVoice == v) return;
    _focusOnVoice = v;
    _headphones.setFocusOnVoice(v);
    emit ambientChanged();
}

// ---- Equalizer ----
QVariantList HeadphonesController::eqBands() const
{
    QVariantList l;
    for (int b : _eqBands) l.push_back(b);
    return l;
}

// Only MANUAL transmits band values (the engine sends a preset-select for everything else,
// including Custom 1/2), so only expose band editing there.
bool HeadphonesController::eqManual() const { return _eqPreset == EQ_MANUAL_INDEX; }

void HeadphonesController::pushEqualizer()
{
    EqualizerConfig cfg;
    cfg.preset = static_cast<int>(EQ_PRESETS[_eqPreset]);
    cfg.clearBass = _eqClearBass;
    cfg.bands = { _eqBands[0], _eqBands[1], _eqBands[2], _eqBands[3], _eqBands[4] };
    _headphones.setEqualizer(cfg);
}

void HeadphonesController::setEqPreset(int v)
{
    if (v < 0 || v >= static_cast<int>(std::size(EQ_PRESETS)) || _eqPreset == v) return;
    _eqPreset = v;
    pushEqualizer();
    emit eqChanged();
}

void HeadphonesController::setEqClearBass(int v)
{
    if (_eqClearBass == v) return;
    _eqClearBass = v;
    pushEqualizer();
    emit eqChanged();
}

void HeadphonesController::setEqBand(int index, int value)
{
    if (index < 0 || index >= 5 || _eqBands[index] == value) return;
    _eqBands[index] = value;
    pushEqualizer();
    emit eqChanged();
}

// ---- Spatial (mutually exclusive) ----
void HeadphonesController::setSoundPosition(int v)
{
    if (v < 0 || v > 5) return;
    _soundPosition = v;
    if (v != 0) _vptType = 0;
    _headphones.setSurroundPosition(SOUND_POSITION_PRESET_ARRAY[_soundPosition]);
    _headphones.setVptType(_vptType);
    emit spatialChanged();
}

void HeadphonesController::setVptType(int v)
{
    if (v < 0 || v > 4) return;
    _vptType = v;
    if (v != 0) _soundPosition = 0;
    _headphones.setVptType(_vptType);
    _headphones.setSurroundPosition(SOUND_POSITION_PRESET_ARRAY[_soundPosition]);
    emit spatialChanged();
}

// ---- Speak-to-Chat ----
void HeadphonesController::setS2cEnabled(bool v)
{
    if (_s2cEnabled == v) return;
    _s2cEnabled = v;
    _headphones.setS2CToggle(v ? S2C_TOGGLE::ACTIVE : S2C_TOGGLE::INACTIVE);
    _headphones.setS2COptions(_s2cSensitivity, _s2cVoice, _s2cAutoOff);
    emit s2cChanged();
}

void HeadphonesController::setS2cSensitivity(int v)
{
    if (_s2cSensitivity == v) return;
    _s2cSensitivity = v;
    _headphones.setS2COptions(_s2cSensitivity, _s2cVoice, _s2cAutoOff);
    emit s2cChanged();
}

void HeadphonesController::setS2cAutoOff(int v)
{
    if (_s2cAutoOff == v) return;
    _s2cAutoOff = v;
    _headphones.setS2COptions(_s2cSensitivity, _s2cVoice, _s2cAutoOff);
    emit s2cChanged();
}

void HeadphonesController::setS2cVoice(bool v)
{
    if (_s2cVoice == v) return;
    _s2cVoice = v;
    _headphones.setS2COptions(_s2cSensitivity, _s2cVoice, _s2cAutoOff);
    emit s2cChanged();
}

// ---- System ----
void HeadphonesController::setDsee(bool v)
{
    if (_dsee == v) return;
    _dsee = v;
    _headphones.setAudioUpsampling(v);
    emit systemChanged();
}

void HeadphonesController::setVoiceGuidance(bool v)
{
    if (_voiceGuidance == v) return;
    _voiceGuidance = v;
    _headphones.setVoiceGuidance(v);
    emit systemChanged();
}

void HeadphonesController::setAutoPowerOff(int v)
{
    if (v < 0 || v > 5 || _autoPowerOff == v) return;
    _autoPowerOff = v;
    _headphones.setAutoPowerOff(static_cast<AUTO_POWER_OFF>(v));
    emit systemChanged();
}

void HeadphonesController::mpDisconnect(int slot)
{
    if (_demo)
    {
        QVariantList keep;
        for (const auto& v : _mpDevices)
            if (v.toMap().value("slot").toInt() != slot) keep.push_back(v);
        _mpDevices = keep;
        emit multipointChanged();
        return;
    }
    auto conn = _headphones.getConnectedDevices();
    int dev = (slot == 1) ? conn.first : conn.second;
    if (dev > 0) _headphones.setMultiPointConnection(slot, 0, dev);
}

// ---- Connection lifecycle ----
void HeadphonesController::refreshDevices()
{
    if (_devicesFuture.valid() || _bt.isConnected()) return;
    setConn(Conn::Discovering);
    _devicesFuture.setFromAsync([this]() { return _bt.getConnectedDevices(); });
}

void HeadphonesController::connectTo(const QString& mac, const QString& name)
{
    if (_connectFuture.valid() || _bt.isConnected()) return;
    _device.name = name.toStdString();
    _device.mac = mac.toStdString();
    _protocol = protocolForDeviceName(_device.name);
    setConn(Conn::Connecting);
    std::string addr = _device.mac;
    SonyProtocol proto = _protocol;
    _connectFuture.setFromAsync([this, addr, proto]() { _bt.connect(addr, proto); });
}

void HeadphonesController::disconnectDevice()
{
    teardownConnection();
    refreshDevices();
}

void HeadphonesController::refreshBattery()
{
    _wantBattery = true;
    _lastOsBatteryPollMs = -100000;
}

void HeadphonesController::startOptimize()
{
    _optimizeRequested = true;
    _headphones.setOptimizerState(OPTIMIZER_STATE::OPTIMIZING);
    emit optimizerChanged();
}

void HeadphonesController::cancelOptimize()
{
    _optimizeRequested = false;
    _headphones.setOptimizerState(OPTIMIZER_STATE::IDLE);
    emit optimizerChanged();
}

void HeadphonesController::setConn(Conn c)
{
    if (_conn == c) return;
    _conn = c;
    emit connectionChanged();
}

void HeadphonesController::setSyncing(bool s)
{
    if (_syncing == s) return;
    _syncing = s;
    emit syncingChanged();
}

void HeadphonesController::pump()
{
    if (_demo) return;

    // --- Discovery future ---
    if (_devicesFuture.ready())
    {
        try
        {
            auto found = _devicesFuture.get();
            _devices.clear();
            for (const auto& d : found)
            {
                QVariantMap m;
                m["name"] = QString::fromStdString(d.name);
                m["mac"] = QString::fromStdString(d.mac);
                m["model"] = friendlyModel(d.name);
                bool v2 = protocolForDeviceName(d.name) == SonyProtocol::V2;
                m["isV2"] = v2;
                m["badge"] = v2 ? QStringLiteral("Limited support") : QStringLiteral("Ready to connect");
                _devices.push_back(m);
            }
            emit devicesChanged();
        }
        catch (const RecoverableException& exc) { emit errorOccurred(exc.what()); }
        catch (const std::exception& exc) { emit errorOccurred(exc.what()); }
        if (!_bt.isConnected()) setConn(Conn::Disconnected);
    }

    // --- Connect future ---
    if (_connectFuture.ready())
    {
        try
        {
            _connectFuture.get();
            setConn(_bt.isConnected() ? Conn::Connected : Conn::Disconnected);
            if (_bt.isConnected())
                emit toast("Connected to " + deviceModel());
        }
        catch (const RecoverableException& exc)
        {
            if (exc.shouldDisconnect) _bt.disconnect();
            emit errorOccurred(exc.what());
            setConn(Conn::Disconnected);
            refreshDevices();
        }
        catch (const std::exception& exc)
        {
            emit errorOccurred(exc.what());
            setConn(Conn::Disconnected);
            refreshDevices();
        }
    }

    if (_bt.isConnected() && !_connectionInitialized) handleConnectionSetup();

    // --- Command future (settings push) + battery, v1 only ---
    if (_commandFuture.ready())
    {
        setSyncing(false);
        bool wasSettingsPush = _settingsPushPending;
        _settingsPushPending = false;
        try
        {
            _commandFuture.get();
            if (wasSettingsPush)
                emit toast("Settings applied");
        }
        catch (const RecoverableException& exc)
        {
            QString prefix;
            if (exc.shouldDisconnect) { teardownConnection(); refreshDevices(); prefix = "Disconnected due to: "; }
            emit errorOccurred(prefix + exc.what());
        }
        catch (const std::exception& exc) { emit errorOccurred(exc.what()); }
    }
    else if (_commandFuture.valid())
    {
        // still sending
    }
    else if (_bt.isConnected() && _connectionInitialized && _protocol == SonyProtocol::V1)
    {
        if (_headphones.isChanged())
        {
            setSyncing(true);
            _settingsPushPending = true;
            _commandFuture.setFromAsync([this]() { _headphones.setChanges(); });
        }
        else if (_wantBattery)
        {
            _wantBattery = false;
            _commandFuture.setFromAsync([this]() { _headphones.requestBattery(); });
        }
    }

    if (_bt.isConnected())
    {
        if (_protocol == SonyProtocol::V1) pollOsBattery();
        emitLiveDeltas();
    }
}

void HeadphonesController::pollOsBattery()
{
    if (_osBatteryFuture.ready())
    {
        try { _osBatteryLevel = _osBatteryFuture.get(); }
        catch (...) {}
    }
    if (!_osBatteryFuture.valid() && (nowMs() - _lastOsBatteryPollMs) > 5000)
    {
        _lastOsBatteryPollMs = nowMs();
        std::string name = _device.name;
        _osBatteryFuture.setFromAsync([this, name]() { return _bt.getDeviceBatteryLevel(name); });
    }
}

void HeadphonesController::emitLiveDeltas()
{
    int bat = batteryLevel();
    bool chg = _headphones.isBatteryCharging();
    if (bat != _cBattery || chg != _cCharging) { _cBattery = bat; _cCharging = chg; emit batteryChanged(); }

    // NC/ASM state from the device (connect read-back and NOTIFY frames, e.g. the physical
    // ANC button). Compare against the last-seen engine state, not the UI state, so a pending
    // user change isn't reverted while it's still being sent.
    bool ambientOn = _headphones.getAmbientSoundControl();
    int asmLevel = _headphones.getAsmLevel();
    bool focusVoice = _headphones.getFocusOnVoice();
    if (ambientOn != _cAmbientOn || asmLevel != _cAsmLevel || focusVoice != _cFocusVoice)
    {
        _cAmbientOn = ambientOn;
        _cAsmLevel = asmLevel;
        _cFocusVoice = focusVoice;
        _ambientMode = ambientOn ? (asmLevel == 0 ? 0 : 1) : 2;
        if (ambientOn && asmLevel > 0)
            _asmLevel = asmLevel;
        _focusOnVoice = focusVoice;
        emit ambientChanged();
    }

    unsigned int caps = _headphones.getCapabilities();
    if (caps != _cCaps) { _cCaps = caps; emit capabilitiesChanged(); }

    bool opt = _headphones.getOptimizerState() == OPTIMIZER_STATE::OPTIMIZING;
    if (opt != _cOptimizing)
    {
        if (_cOptimizing && !opt) _optimizeRequested = false;
        _cOptimizing = opt;
        emit optimizerChanged();
    }

    bool fav = _headphones.isFocusOnVoiceAvailable();
    if (fav != _cFocusAvail) { _cFocusAvail = fav; emit ambientChanged(); }

    // Multipoint list (device-driven)
    if (_headphones.supportsFeature(DEVICE_CAPABILITIES::MULTI_POINT))
    {
        auto devs = _headphones.getDevices();
        auto conn = _headphones.getConnectedDevices();
        QVariantList list;
        auto add = [&](int idx, int slot) {
            if (idx > 0 && idx < static_cast<int>(devs.size()))
                list.push_back(mpEntry(QString::fromStdString(devs[idx].name),
                                       slot == 1 ? "Active" : "Standby", slot));
        };
        add(conn.first, 1);
        add(conn.second, 2);
        if (list != _mpDevices) { _mpDevices = list; emit multipointChanged(); }
    }
}

void HeadphonesController::handleConnectionSetup()
{
    _connectionInitialized = true;
    _osBatteryLevel = -1;
    _lastOsBatteryPollMs = -100000;

    if (_protocol == SonyProtocol::V2) return; // v2 control not implemented yet

    _listener = std::make_unique<Listener>(_headphones, _bt);
    _listener->start();
    if (!_commandFuture.valid())
    {
        setSyncing(true);
        _commandFuture.setFromAsync([this]() { _headphones.queryState(); });
    }
}

void HeadphonesController::applyWindowTheme(QObject* window, bool dark)
{
#ifdef _WIN32
    if (auto* w = qobject_cast<QWindow*>(window))
    {
        BOOL useDark = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(reinterpret_cast<HWND>(w->winId()),
                              20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &useDark, sizeof(useDark));
    }
#else
    Q_UNUSED(window);
    Q_UNUSED(dark);
#endif
}

void HeadphonesController::teardownConnection()
{
    _bt.disconnect();
    _listener.reset();
    if (_commandFuture.valid())
    {
        try { _commandFuture.get(); }
        catch (...) {}
    }
    _connectionInitialized = false;
    _osBatteryLevel = -1;
    _wantBattery = false;
    _settingsPushPending = false;
    setSyncing(false);
    setConn(Conn::Disconnected);
}
