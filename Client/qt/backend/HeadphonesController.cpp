// Platform connector header must come before Qt headers on Windows so winsock2.h is seen
// first. WIN32_LEAN_AND_MEAN (set in CMake) also keeps windows.h from pulling winsock 1.
#ifdef _WIN32
#include "windows/WindowsBluetoothConnector.h"
#endif

#include "HeadphonesController.h"
#include "Exceptions.h"

#include <QDebug>

#include <algorithm>

namespace {

std::unique_ptr<IBluetoothConnector> makeConnector()
{
#ifdef _WIN32
    return std::make_unique<WindowsBluetoothConnector>();
#else
    // Aural is Windows-only for now. The engine's Linux/macOS connectors belong to the
    // original client and are not wired up here; returning null would crash on first use.
    return nullptr;
#endif
}

// Never invent a model. A Bluetooth name is user-editable, so we only show a marketing name
// when the name actually matches a product code (see identifyDevice).
QString displayModel(const std::string& name)
{
    const DeviceIdentity identity = identifyDevice(name);
    if (identity.recognised)
        return QString::fromLatin1(identity.marketingName);
    return QString::fromStdString(name);
}

QString statusName(SettingStatus status)
{
    switch (status)
    {
    case SettingStatus::Requested:    return QStringLiteral("requested");
    case SettingStatus::Acknowledged: return QStringLiteral("acknowledged");
    case SettingStatus::Confirmed:    return QStringLiteral("confirmed");
    case SettingStatus::Failed:       return QStringLiteral("failed");
    case SettingStatus::Unknown:      break;
    }
    return QStringLiteral("unknown");
}

QVariantMap statusMap(const SettingsStatus& s)
{
    QVariantMap m;
    m["ambient"] = statusName(s.ambient);
    m["equalizer"] = statusName(s.equalizer);
    m["spatial"] = statusName(s.spatial);
    m["speakToChat"] = statusName(s.speakToChat);
    m["dsee"] = statusName(s.dsee);
    m["voiceGuidance"] = statusName(s.voiceGuidance);
    m["autoPowerOff"] = statusName(s.autoPowerOff);
    m["optimizer"] = statusName(s.optimizer);
    m["multipoint"] = statusName(s.multipoint);
    return m;
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
    : HeadphonesController(makeConnector(), parent)
{
}

HeadphonesController::HeadphonesController(std::unique_ptr<IBluetoothConnector> connector,
                                           QObject* parent)
    : QObject(parent)
    , _bt(std::move(connector))
    , _headphones(_bt)
{
    init();
}

void HeadphonesController::init()
{
    _clock.start();
    _settingStatus = statusMap(SettingsStatus{});

    _demo = qEnvironmentVariableIsSet("AURAL_DEMO");
    if (_demo)
    {
        // Dev-only: simulate a connected XM4 with sample settings so every screen previews
        // without hardware.
        // AURAL_DEMO_DEVICE previews a different name - "Leon's Headphones" exercises the
        // unrecognised path (SON-11), which otherwise cannot be seen without hardware.
        _device.name = qEnvironmentVariable("AURAL_DEMO_DEVICE", QStringLiteral("WH-1000XM4"))
                           .toStdString();
        _identity = identifyDevice(_device.name);
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

        // Demo mode is a UI preview, so it also previews the honest status vocabulary:
        // ambient has a read-back, the write-only settings do not.
        SettingsStatus demoStatus;
        demoStatus.ambient = SettingStatus::Confirmed;
        demoStatus.equalizer = SettingStatus::Acknowledged;
        demoStatus.spatial = SettingStatus::Acknowledged;
        demoStatus.speakToChat = SettingStatus::Acknowledged;
        demoStatus.dsee = SettingStatus::Acknowledged;
        demoStatus.voiceGuidance = SettingStatus::Unknown;
        demoStatus.autoPowerOff = SettingStatus::Acknowledged;
        demoStatus.optimizer = SettingStatus::Confirmed;
        demoStatus.multipoint = SettingStatus::Confirmed;
        _settingStatus = statusMap(demoStatus);
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
QString HeadphonesController::deviceModel() const { return displayModel(_device.name); }
bool HeadphonesController::modelRecognised() const { return _identity.recognised; }

int HeadphonesController::batteryLevel()
{
    if (_demo) return 90;
    int protocolBattery = _headphones.getBatteryLevel();
    return (_osBatteryLevel >= 0) ? _osBatteryLevel : protocolBattery;
}

bool HeadphonesController::batteryCharging() { return _headphones.isBatteryCharging(); }

bool HeadphonesController::capabilitiesKnown() { return _demo ? true : _headphones.capabilitiesKnown(); }

// SON-11. Two sources of evidence, in order: what the device said, then what we know about the
// model. An unidentified device gives us neither, so the answer is no - the app does not
// discover what someone's headphones support by sending them commands and seeing what happens.
bool HeadphonesController::featureAvailable(DEVICE_CAPABILITIES cap)
{
    // Demo mode deliberately goes through the same gates: a preview that showed controls the
    // real app would withhold would be worse than useless.
    if (!connected() || _protocol != SonyProtocol::V1) return false;

    switch (_headphones.featureState(cap))
    {
    case CapabilityState::Supported:   return true;
    case CapabilityState::Unsupported: return false;
    case CapabilityState::Unknown:     break;
    }
    // No valid capability reply yet (or none ever, on a device that does not answer the query):
    // the model is what tells a WH-1000XM4 apart from a stranger.
    return _identity.recognised;
}

bool HeadphonesController::supportsOptimizer() { return featureAvailable(DEVICE_CAPABILITIES::OPTIMIZER); }
bool HeadphonesController::supportsSpeakToChat() { return featureAvailable(DEVICE_CAPABILITIES::SPEAK_TO_CHAT); }
bool HeadphonesController::supportsMultipoint() { return featureAvailable(DEVICE_CAPABILITIES::MULTI_POINT); }

// The deliberate exception to featureAvailable(): ambient sound is the only setting with a
// read-back, so an unrecognised device does not have to be taken on trust. Either it answers
// the NC/ASM query - and the status chip says "Confirmed" - or it does not, and the chip keeps
// saying "not read from device". Offering it is what makes the legacy attempt worth making;
// offering the rest would be guesswork.
bool HeadphonesController::ambientAvailable()
{
    if (!connected() || _protocol != SonyProtocol::V1) return false;
    return _headphones.featureState(DEVICE_CAPABILITIES::NC_ASM) != CapabilityState::Unsupported;
}

// No capability bit covers the equalizer, and the preset table is model-specific (Custom 1/2
// do not exist on every model), so recognition is the only evidence there is.
bool HeadphonesController::equalizerAvailable()
{
    return connected() && _protocol == SonyProtocol::V1 && _identity.recognised;
}

bool HeadphonesController::spatialAvailable() { return featureAvailable(DEVICE_CAPABILITIES::VPT); }

// DSEE, voice guidance and auto power off share a shape: write-only, no capability bit, no
// read-back, and encodings that are model-specific (the auto-power-off timeout table most
// obviously). Sending the XM4 table to something else is exactly the guess this gate prevents,
// and with no read-back the app would never find out it had guessed wrong.
bool HeadphonesController::systemSettingsAvailable()
{
    return connected() && _protocol == SonyProtocol::V1 && _identity.recognised;
}

QString HeadphonesController::limitedControlReason()
{
    if (!connected() || _protocol != SonyProtocol::V1 || _identity.recognised)
        return QString();
    return tr("Aural doesn't recognise this device, so it only offers the control it can "
              "verify. Ambient sound reports its state back; everything else is "
              "model-specific and would be a guess.");
}


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
    // Guarded here as well as in QML: the tray menu and any stale binding come through the
    // same door, and a gated control must not reach the wire by a side route (SON-11).
    if (!ambientAvailable()) return;
    if (m < 0 || m > 2 || _ambientMode == m) return;
    _ambientMode = m;
    pushAmbient();
    emit ambientChanged();
}

void HeadphonesController::cycleAmbientMode()
{
    setAmbientMode((_ambientMode + 1) % 3);
}

void HeadphonesController::setAsmLevel(int v)
{
    if (!ambientAvailable()) return;
    v = std::clamp(v, 1, 19);
    if (_asmLevel == v) return;
    _asmLevel = v;
    if (_ambientMode == 1)
        pushAmbient();
    emit ambientChanged();
}

void HeadphonesController::setFocusOnVoice(bool v)
{
    if (!ambientAvailable()) return;
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
    if (!equalizerAvailable()) return;
    if (v < 0 || v >= static_cast<int>(std::size(EQ_PRESETS)) || _eqPreset == v) return;
    _eqPreset = v;
    pushEqualizer();
    emit eqChanged();
}

void HeadphonesController::setEqClearBass(int v)
{
    if (!equalizerAvailable()) return;
    if (_eqClearBass == v) return;
    _eqClearBass = v;
    pushEqualizer();
    emit eqChanged();
}

void HeadphonesController::setEqBand(int index, int value)
{
    if (!equalizerAvailable()) return;
    if (index < 0 || index >= 5 || _eqBands[index] == value) return;
    _eqBands[index] = value;
    pushEqualizer();
    emit eqChanged();
}

// ---- Spatial (mutually exclusive) ----
void HeadphonesController::setSoundPosition(int v)
{
    if (!spatialAvailable()) return;
    if (v < 0 || v > 5) return;
    _soundPosition = v;
    if (v != 0) _vptType = 0;
    _headphones.setSurroundPosition(SOUND_POSITION_PRESET_ARRAY[_soundPosition]);
    _headphones.setVptType(_vptType);
    emit spatialChanged();
}

void HeadphonesController::setVptType(int v)
{
    if (!spatialAvailable()) return;
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
    if (!supportsSpeakToChat()) return;
    if (_s2cEnabled == v) return;
    _s2cEnabled = v;
    _headphones.setS2CToggle(v ? S2C_TOGGLE::ACTIVE : S2C_TOGGLE::INACTIVE);
    _headphones.setS2COptions(_s2cSensitivity, _s2cVoice, _s2cAutoOff);
    emit s2cChanged();
}

void HeadphonesController::setS2cSensitivity(int v)
{
    if (!supportsSpeakToChat()) return;
    if (_s2cSensitivity == v) return;
    _s2cSensitivity = v;
    _headphones.setS2COptions(_s2cSensitivity, _s2cVoice, _s2cAutoOff);
    emit s2cChanged();
}

void HeadphonesController::setS2cAutoOff(int v)
{
    if (!supportsSpeakToChat()) return;
    if (_s2cAutoOff == v) return;
    _s2cAutoOff = v;
    _headphones.setS2COptions(_s2cSensitivity, _s2cVoice, _s2cAutoOff);
    emit s2cChanged();
}

void HeadphonesController::setS2cVoice(bool v)
{
    if (!supportsSpeakToChat()) return;
    if (_s2cVoice == v) return;
    _s2cVoice = v;
    _headphones.setS2COptions(_s2cSensitivity, _s2cVoice, _s2cAutoOff);
    emit s2cChanged();
}

// ---- System ----
void HeadphonesController::setDsee(bool v)
{
    if (!systemSettingsAvailable()) return;
    if (_dsee == v) return;
    _dsee = v;
    _headphones.setAudioUpsampling(v);
    emit systemChanged();
}

void HeadphonesController::setVoiceGuidance(bool v)
{
    if (!systemSettingsAvailable()) return;
    if (_voiceGuidance == v) return;
    _voiceGuidance = v;
    _headphones.setVoiceGuidance(v);
    emit systemChanged();
}

void HeadphonesController::setAutoPowerOff(int v)
{
    if (!systemSettingsAvailable()) return;
    if (v < 0 || v > 5 || _autoPowerOff == v) return;
    _autoPowerOff = v;
    _headphones.setAutoPowerOff(static_cast<AUTO_POWER_OFF>(v));
    emit systemChanged();
}

void HeadphonesController::mpDisconnect(int slot)
{
    if (!supportsMultipoint()) return;
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
    _devicesSession = _session;
    _devicesFuture.setFromAsync([this]() { return _bt.getConnectedDevices(); });
}

void HeadphonesController::connectTo(const QString& mac, const QString& name)
{
    if (_connectFuture.valid() || _bt.isConnected()) return;

    // A connection attempt is a new session: results from the previous one are stale.
    ++_session;
    _linkLost.store(false);

    _device.name = name.toStdString();
    _device.mac = mac.toStdString();
    _identity = identifyDevice(_device.name);
    _protocol = _identity.protocol;
    setConn(Conn::Connecting);
    std::string addr = _device.mac;
    SonyProtocol proto = _protocol;
    _connectSession = _session;
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
    if (!supportsOptimizer()) return;
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
    // Which controls are offered depends on being connected and on which device, so the
    // availability properties move with the connection, not only with a capability reply.
    emit capabilitiesChanged();
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

    // --- The headphones closed the link (reported by the reader thread) ---
    if (_linkLost.exchange(false))
    {
        if (_linkLostSession.load() == _session && _conn != Conn::Disconnected)
        {
            emit errorOccurred("The headphones closed the connection");
            teardownConnection();
            refreshDevices();
            return;
        }
    }

    // --- Discovery future ---
    if (_devicesFuture.ready())
    {
        const bool stale = (_devicesSession != _session);
        try
        {
            auto found = _devicesFuture.get();
            if (!stale)
            {
                _devices.clear();
                for (const auto& d : found)
                {
                    const DeviceIdentity identity = identifyDevice(d.name);
                    QVariantMap m;
                    m["name"] = QString::fromStdString(d.name);
                    m["mac"] = QString::fromStdString(d.mac);
                    m["model"] = displayModel(d.name);
                    m["recognised"] = identity.recognised;
                    const bool v2 = identity.protocol == SonyProtocol::V2;
                    m["isV2"] = v2;
                    if (!identity.recognised)
                        m["badge"] = QStringLiteral("Unrecognised - will try legacy protocol");
                    else
                        m["badge"] = v2 ? QStringLiteral("Limited support") : QStringLiteral("Ready to connect");
                    _devices.push_back(m);
                }
                emit devicesChanged();
            }
        }
        catch (const RecoverableException& exc) { if (!stale) emit errorOccurred(exc.what()); }
        catch (const std::exception& exc) { if (!stale) emit errorOccurred(exc.what()); }
        if (!stale && !_bt.isConnected()) setConn(Conn::Disconnected);
    }

    // --- Connect future ---
    if (_connectFuture.ready())
    {
        const bool stale = (_connectSession != _session);
        try
        {
            _connectFuture.get();
            if (!stale)
            {
                setConn(_bt.isConnected() ? Conn::Connected : Conn::Disconnected);
                if (_bt.isConnected())
                    emit toast("Connected to " + deviceModel());
            }
        }
        catch (const RecoverableException& exc)
        {
            if (!stale)
            {
                if (exc.shouldDisconnect) _bt.disconnect();
                emit errorOccurred(exc.what());
                setConn(Conn::Disconnected);
                refreshDevices();
            }
        }
        catch (const std::exception& exc)
        {
            if (!stale)
            {
                emit errorOccurred(exc.what());
                setConn(Conn::Disconnected);
                refreshDevices();
            }
        }
    }

    if (_bt.isConnected() && !_connectionInitialized) handleConnectionSetup();

    // --- Command future (settings push) + battery, v1 only ---
    if (_commandFuture.ready())
    {
        const bool stale = (_commandSession != _session);
        setSyncing(false);
        bool wasSettingsPush = _settingsPushPending;
        _settingsPushPending = false;
        try
        {
            _commandFuture.get();
            // "Sent" is the honest word: the headphones acknowledged the frames. Only a
            // read-back reply can promote a setting to confirmed (see settingStatus).
            if (!stale && wasSettingsPush)
                emit toast("Settings sent");
        }
        catch (const RecoverableException& exc)
        {
            if (!stale)
            {
                QString prefix;
                if (exc.shouldDisconnect) { teardownConnection(); refreshDevices(); prefix = "Disconnected due to: "; }
                emit errorOccurred(prefix + exc.what());
            }
        }
        catch (const std::exception& exc) { if (!stale) emit errorOccurred(exc.what()); }
    }
    else if (_commandFuture.valid())
    {
        // still sending
    }
    else if (_bt.isConnected() && _connectionInitialized && _protocol == SonyProtocol::V1)
    {
        if (_headphones.hasPendingQueries())
        {
            // Follow-up reads the device's own replies asked for: capability details, then
            // availability and values. They run before writes, so a write is decided against
            // what the device reported rather than what we assumed.
            setSyncing(true);
            _commandSession = _session;
            _commandFuture.setFromAsync([this]() { _headphones.sendPendingQueries(); });
        }
        else if (_headphones.isChanged())
        {
            setSyncing(true);
            _settingsPushPending = true;
            _commandSession = _session;
            _commandFuture.setFromAsync([this]() { _headphones.setChanges(); });
        }
        else if (_wantBattery)
        {
            _wantBattery = false;
            _commandSession = _session;
            _commandFuture.setFromAsync([this]() { _headphones.requestBattery(); });
        }
    }

    if (_bt.isConnected())
    {
        if (_protocol == SonyProtocol::V1) pollOsBattery();
        emitLiveDeltas();
    }

    refreshSettingStatus();
}

void HeadphonesController::pollOsBattery()
{
    if (_osBatteryFuture.ready())
    {
        const bool stale = (_osBatterySession != _session);
        try
        {
            int level = _osBatteryFuture.get();
            if (!stale) _osBatteryLevel = level;
        }
        catch (...) {}
    }
    if (!_osBatteryFuture.valid() && (nowMs() - _lastOsBatteryPollMs) > 5000)
    {
        _lastOsBatteryPollMs = nowMs();
        std::string name = _device.name;
        _osBatterySession = _session;
        _osBatteryFuture.setFromAsync([this, name]() { return _bt.getDeviceBatteryLevel(name); });
    }
}

void HeadphonesController::refreshSettingStatus()
{
    QVariantMap next = statusMap(_headphones.getStatuses());
    if (next != _settingStatus)
    {
        _settingStatus = next;
        emit settingStatusChanged();
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
    bool capsKnown = _headphones.capabilitiesKnown();
    if (caps != _cCaps || capsKnown != _cCapsKnown)
    {
        _cCaps = caps;
        _cCapsKnown = capsKnown;
        emit capabilitiesChanged();
    }

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
    if (supportsMultipoint())
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
    // Runs on the reader thread: record it and let pump() (Qt thread) own the teardown.
    const unsigned long long session = _session;
    _listener->setDisconnectHandler([this, session]() {
        _linkLostSession.store(session);
        _linkLost.store(true);
    });
    _listener->start();

    if (!_commandFuture.valid())
    {
        setSyncing(true);
        _commandSession = _session;
        _commandFuture.setFromAsync([this]() { _headphones.queryState(); });
    }
}

// The single place a session ends. Stop the reader, close the socket, drain every
// outstanding future, then reset device and UI state so nothing survives into the next
// connection.
void HeadphonesController::teardownConnection()
{
    ++_session;
    _linkLost.store(false);

    if (_listener)
    {
        // Tell the reader this shutdown is ours before closing under it, so it doesn't
        // report a lost link we already know about.
        _listener->requestStop();
    }
    _bt.disconnect();
    if (_listener)
    {
        _listener->stop();
        _listener.reset();
    }

    // Drain in dependency order: the command future can be blocked on the socket we just
    // closed, discovery and the OS battery lookup are independent.
    auto drain = [](auto& future) {
        if (!future.valid()) return;
        try { future.get(); }
        catch (...) {}
    };
    drain(_commandFuture);
    drain(_connectFuture);
    drain(_devicesFuture);
    drain(_osBatteryFuture);

    _headphones.resetState();
    resetUiState();

    setSyncing(false);
    setConn(Conn::Disconnected);
}

// Everything the controller mirrors locally goes back to its default, so a second device
// never inherits the first one's UI values.
void HeadphonesController::resetUiState()
{
    _connectionInitialized = false;
    _osBatteryLevel = -1;
    _wantBattery = false;
    _settingsPushPending = false;
    _optimizeRequested = false;

    _device = BluetoothDevice{ "", "" };
    _identity = DeviceIdentity{};
    _protocol = SonyProtocol::V1;

    _cBattery = -1;
    _cCharging = false;
    _cCaps = 0;
    _cCapsKnown = false;
    _cOptimizing = false;
    _cFocusAvail = false;
    _cAmbientOn = false;
    _cAsmLevel = 0;
    _cFocusVoice = false;

    _ambientMode = 2;
    _asmLevel = 10;
    _focusOnVoice = false;
    _eqPreset = 0;
    _eqClearBass = 0;
    _eqBands = { 0, 0, 0, 0, 0 };
    _soundPosition = 0;
    _vptType = 0;
    _s2cEnabled = false;
    _s2cSensitivity = 0;
    _s2cAutoOff = 0;
    _s2cVoice = false;
    _dsee = false;
    _voiceGuidance = false;
    _autoPowerOff = 0;
    _mpDevices.clear();
    _settingStatus = statusMap(SettingsStatus{});

    emit ambientChanged();
    emit eqChanged();
    emit spatialChanged();
    emit s2cChanged();
    emit systemChanged();
    emit multipointChanged();
    emit batteryChanged();
    emit capabilitiesChanged();
    emit optimizerChanged();
    emit settingStatusChanged();
    emit connectionChanged();
}
