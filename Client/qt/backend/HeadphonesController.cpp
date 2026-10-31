// Platform connector header must come before Qt headers on Windows so winsock2.h is seen
// first. WIN32_LEAN_AND_MEAN (set in CMake) also keeps windows.h from pulling winsock 1.
#ifdef _WIN32
#include "windows/WindowsBluetoothConnector.h"
#endif

#include "HeadphonesController.h"
#include "Exceptions.h"
#include "ProtocolTrace.h"

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
    m["touchPanel"] = statusName(s.touchPanel);
    m["wearingControl"] = statusName(s.wearingControl);
    m["multipointMode"] = statusName(s.multipointMode);
    return m;
}

// Multipoint timing. None of it has been measured on hardware; it bounds how long Aural waits,
// it does not describe the headset.
//   A written change the headphones neither confirm, ask about, nor drop the link for.
constexpr qint64 MULTIPOINT_SILENCE_MS = 20000;
//   Reconnecting after the headphones dropped the link mid-change.
constexpr qint64 RECONNECT_FIRST_DELAY_MS = 1500;
constexpr qint64 RECONNECT_RETRY_MS = 3000;
constexpr qint64 RECONNECT_GIVE_UP_MS = 60000;
//   Back on the headphones: reading the multipoint value.
constexpr qint64 VERIFY_TIMEOUT_MS = 15000;

// A WH-1000XM4 once left the first command on a new link unacknowledged; a fresh connection a
// few seconds later answered at once. A failed handshake is retried once after this pause.
constexpr qint64 HANDSHAKE_RETRY_DELAY_MS = 1500;

// A discovered control as QML sees it; the keys are documented on the touchPanel property.
QVariantMap toggleMap(const ToggleSnapshot& t)
{
    QString phase = QStringLiteral("ready");
    if (t.support != CapabilityState::Supported)
        phase = QStringLiteral("absent");
    else if (t.availability == Availability::Unavailable)
        phase = QStringLiteral("unavailable");
    else if (t.availability == Availability::Unknown || !t.valueKnown)
        phase = QStringLiteral("loading");

    // Until a read answers the request, the switch shows what was asked for and the status says
    // it is not confirmed. After that it shows what the device reported, whatever was asked.
    const bool pending = t.status == SettingStatus::Requested || t.status == SettingStatus::Acknowledged;
    QVariantMap m;
    m["phase"] = phase;
    m["valueKnown"] = t.valueKnown;
    m["checked"] = pending ? t.requested : t.value;
    m["pending"] = pending;
    m["status"] = statusName(t.status);
    return m;
}

QString multipointStepName(MultipointStep step)
{
    switch (step)
    {
    case MultipointStep::Requested:  return QStringLiteral("sending");
    case MultipointStep::Sent:       return QStringLiteral("waiting");
    case MultipointStep::Confirming: return QStringLiteral("confirming");
    case MultipointStep::Answering:  return QStringLiteral("answering");
    case MultipointStep::Accepted:   return QStringLiteral("applying");
    case MultipointStep::None:       break;
    }
    return QStringLiteral("idle");
}

QString multipointNote(MultipointStep step)
{
    switch (step)
    {
    case MultipointStep::Requested:  return QObject::tr("Sending the change to the headphones...");
    case MultipointStep::Sent:       return QObject::tr("Waiting for the headphones to respond...");
    case MultipointStep::Confirming: return QObject::tr("Waiting for your confirmation.");
    case MultipointStep::Answering:  return QObject::tr("Sending your answer...");
    case MultipointStep::Accepted:   return QObject::tr("The headphones are applying the change and may disconnect briefly.");
    case MultipointStep::None:       break;
    }
    return QString();
}

// The touch-panel keys plus where the change stands; documented on the multipointMode property.
QVariantMap multipointMap(const ToggleSnapshot& t, const MultipointChange& c)
{
    QVariantMap m = toggleMap(t);
    m["step"] = multipointStepName(c.step);
    m["busy"] = c.step != MultipointStep::None;
    m["note"] = multipointNote(c.step);
    return m;
}

QVariantMap alertMap(const MultipointChange& c)
{
    QVariantMap m;
    if (c.step != MultipointStep::Confirming || !c.alert)
        return m;
    m["messageId"] = static_cast<int>(c.alert->messageId);
    m["turningOn"] = c.alert->messageId == v1::ALERT_MSG_MULTIPOINT_TO_ON;
    m["choice"] = c.alert->actionType == v1::ALERT_ACTION_POSITIVE_NEGATIVE;
    return m;
}

// For the protocol trace: a discovered control's state in words, logged when it changes.
void traceControl(const char* name, const QVariantMap& before, const QVariantMap& after)
{
    if (!ProtocolTrace::enabled())
        return;
    auto describe = [](const QVariantMap& m) {
        QString s = m.value("phase").toString();
        if (m.value("valueKnown").toBool())
            s += m.value("checked").toBool() ? QStringLiteral(", on") : QStringLiteral(", off");
        s += QStringLiteral(", ") + m.value("status").toString();
        const QString step = m.value("step").toString();
        if (!step.isEmpty() && step != QStringLiteral("idle"))
            s += QStringLiteral(", ") + step;
        return s;
    };
    const QString now = describe(after);
    if (describe(before) != now)
        ProtocolTrace::line(std::string("   UI: ") + name + " " + now.toStdString());
}

// Demo mode has no engine behind it: a described, available control the device "confirmed".
ToggleSnapshot demoToggle(bool on)
{
    ToggleSnapshot t;
    t.support = CapabilityState::Supported;
    t.availability = Availability::Available;
    t.valueKnown = true;
    t.value = on;
    t.requested = on;
    t.status = SettingStatus::Confirmed;
    return t;
}

// UI preset index -> engine EQ_PRESET. Indices 9..11 (Manual/Custom1/Custom2) use the bands.
const EQ_PRESET EQ_PRESETS[] = {
    EQ_PRESET::OFF, EQ_PRESET::BRIGHT, EQ_PRESET::EXCITED, EQ_PRESET::MELLOW,
    EQ_PRESET::RELAXED, EQ_PRESET::VOCAL, EQ_PRESET::TREBLE_BOOST, EQ_PRESET::BASS_BOOST,
    EQ_PRESET::SPEECH, EQ_PRESET::MANUAL, EQ_PRESET::CUSTOM_1, EQ_PRESET::CUSTOM_2
};
constexpr int EQ_MANUAL_INDEX = 9;

// EqPresetId byte as the engine holds it -> UI index, or -1 for a preset the table lacks.
int eqPresetIndex(int code)
{
    for (int i = 0; i < static_cast<int>(std::size(EQ_PRESETS)); ++i)
        if (static_cast<unsigned char>(EQ_PRESETS[i]) == code)
            return i;
    return -1;
}

// The auto power-off choices a model is known to offer. The XM4 has a "when taken off" switch
// rather than timers (Gadgetbridge models it that way, and the earlier XM4 notes agree); other
// recognised V1 models keep the full list until their capability replies are captured.
QVariantList knownAutoPowerOffChoices(SonyModel model)
{
    if (model == SonyModel::WH_1000XM4)
        return { static_cast<int>(AUTO_POWER_OFF::OFF), static_cast<int>(AUTO_POWER_OFF::WHEN_TAKEN_OFF) };
    return { static_cast<int>(AUTO_POWER_OFF::OFF), static_cast<int>(AUTO_POWER_OFF::AFTER_5_MIN),
             static_cast<int>(AUTO_POWER_OFF::AFTER_30_MIN), static_cast<int>(AUTO_POWER_OFF::AFTER_1_HOUR),
             static_cast<int>(AUTO_POWER_OFF::AFTER_3_HOUR), static_cast<int>(AUTO_POWER_OFF::WHEN_TAKEN_OFF) };
}

int soundPositionIndex(SOUND_POSITION_PRESET position)
{
    for (int i = 0; i < 6; ++i)
        if (SOUND_POSITION_PRESET_ARRAY[i] == position)
            return i;
    return 0;
}

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
    _touchPanel = toggleMap(ToggleSnapshot{});
    _wearingControl = toggleMap(ToggleSnapshot{});
    _multipointMode = multipointMap(ToggleSnapshot{}, MultipointChange{});

    // The UI shows multipoint alerts (MultipointConfirmDialog) and sends the user's answer, so the
    // engine may enable them on the headphones before a multipoint change.
    _headphones.installAlertHandling();

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
        _autoPowerOff = static_cast<int>(AUTO_POWER_OFF::WHEN_TAKEN_OFF); // one of the XM4's choices
        _soundPosition = 3; // Front
        _mpDevices = QVariantList{
            mpEntry("MacBook Pro", "Active audio source", 1),
            mpEntry("iPhone 15", "Standby", 2)
        };
        _touchPanel = toggleMap(demoToggle(true));
        _wearingControl = toggleMap(demoToggle(true));
        _multipointMode = multipointMap(demoToggle(true), MultipointChange{});

        // Demo mode is a UI preview, so it also previews the honest status vocabulary: most
        // settings read back as Confirmed, one is still only Sent, one was never read.
        SettingsStatus demoStatus;
        demoStatus.ambient = SettingStatus::Confirmed;
        demoStatus.equalizer = SettingStatus::Confirmed;
        demoStatus.spatial = SettingStatus::Acknowledged;
        demoStatus.speakToChat = SettingStatus::Confirmed;
        demoStatus.dsee = SettingStatus::Confirmed;
        demoStatus.voiceGuidance = SettingStatus::Unknown;
        demoStatus.autoPowerOff = SettingStatus::Confirmed;
        demoStatus.optimizer = SettingStatus::Confirmed;
        demoStatus.multipoint = SettingStatus::Confirmed;
        demoStatus.touchPanel = SettingStatus::Confirmed;
        demoStatus.wearingControl = SettingStatus::Confirmed;
        demoStatus.multipointMode = SettingStatus::Confirmed;
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

// The same rule for any function ID the capability reply can list.
bool HeadphonesController::functionAvailable(FUNCTION_TYPE fn)
{
    if (!connected() || _protocol != SonyProtocol::V1) return false;

    switch (_headphones.functionState(fn))
    {
    case CapabilityState::Supported:   return true;
    case CapabilityState::Unsupported: return false;
    case CapabilityState::Unknown:     break;
    }
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

// The device can say it has a preset equalizer (51), but the preset table the UI offers is
// model-specific (Custom 1/2 do not exist everywhere) and the EQ capability layout has not been
// recovered. So: never where the device says there is none, and otherwise only for a model we
// recognise.
bool HeadphonesController::equalizerAvailable()
{
    return connected() && _protocol == SonyProtocol::V1 && _identity.recognised
        && _headphones.functionState(FUNCTION_TYPE::PRESET_EQ) != CapabilityState::Unsupported;
}

bool HeadphonesController::vptAvailable() { return functionAvailable(FUNCTION_TYPE::VPT); }
bool HeadphonesController::soundPositionAvailable() { return functionAvailable(FUNCTION_TYPE::SOUND_POSITION); }
bool HeadphonesController::spatialAvailable() { return vptAvailable() || soundPositionAvailable(); }

// Plain on/off settings with a read-back: the device's own advertisement is enough evidence.
bool HeadphonesController::dseeAvailable() { return functionAvailable(FUNCTION_TYPE::UPSCALING); }
bool HeadphonesController::voiceGuidanceAvailable() { return functionAvailable(FUNCTION_TYPE::VOICE_GUIDANCE); }

// Auto power-off's choices are model knowledge until its capability layout is recovered, so an
// unrecognised device does not get it even when it advertises the function.
bool HeadphonesController::autoPowerOffAvailable()
{
    return connected() && _protocol == SonyProtocol::V1 && _identity.recognised
        && _headphones.functionState(FUNCTION_TYPE::AUTO_POWER_OFF) != CapabilityState::Unsupported;
}

bool HeadphonesController::systemSettingsAvailable()
{
    return dseeAvailable() || voiceGuidanceAvailable() || autoPowerOffAvailable();
}

QString HeadphonesController::limitedControlReason()
{
    if (!connected() || _protocol != SonyProtocol::V1 || _identity.recognised)
        return QString();
    if (_headphones.capabilitiesKnown())
        return tr("Aural doesn't recognise this device, so it offers only what the headphones "
                  "report supporting. Model-specific settings such as the equalizer presets "
                  "and auto power-off timers stay hidden.");
    return tr("Aural doesn't recognise this device, so it only offers the control it can "
              "verify. Ambient sound reports its state back; everything else is "
              "model-specific and would be a guess.");
}


// For the protocol trace: what this connection offers, logged whenever the capabilities move.
// Tells "the control was withdrawn" apart from "the write went out and was ignored".
void HeadphonesController::traceOffer()
{
    if (!ProtocolTrace::enabled())
        return;
    const auto yn = [](bool b) { return b ? std::string("yes") : std::string("no"); };
    const Buffer ids(_cFunctions.begin(), _cFunctions.end());
    ProtocolTrace::line(std::string("   UI: capabilities ") + (_cCapsKnown ? "known" : "not known") +
        ", advertised [" + ProtocolTrace::hex(ids) + "]" +
        "; offers ambient " + yn(ambientAvailable()) + ", equalizer " + yn(equalizerAvailable()) +
        ", spatial " + yn(spatialAvailable()) + ", speak-to-chat " + yn(supportsSpeakToChat()) +
        ", DSEE " + yn(dseeAvailable()) + ", voice guidance " + yn(voiceGuidanceAvailable()) +
        ", auto power-off " + yn(autoPowerOffAvailable()) + ", device list " + yn(supportsMultipoint()) +
        "; model " + (_identity.recognised ? "recognised" : "not recognised"));
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
    if (!ambientAvailable())
    {
        ProtocolTrace::line("   UI: ambient mode change refused: not offered on this connection");
        return;
    }
    if (m < 0 || m > 2 || _ambientMode == m) return;
    ProtocolTrace::line("   UI: ambient mode -> " + std::to_string(m) + " (0 NC, 1 ambient, 2 off)");
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

// The editor is clear bass + five bands. Until the device reports its layout that is the
// assumption the client always made; once it reports another shape the editor would not fit.
bool HeadphonesController::eqBandsEditable()
{
    const int levels = _headphones.getEqualizerLevelCount();
    return levels < 0 || levels == 6;
}

void HeadphonesController::pushEqualizer()
{
    // A preset the UI table does not contain (reported by the device) has nothing to push.
    if (_eqPreset < 0 || _eqPreset >= static_cast<int>(std::size(EQ_PRESETS)))
        return;
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
    if (!equalizerAvailable() || !eqManual() || !eqBandsEditable()) return;
    if (_eqClearBass == v) return;
    _eqClearBass = v;
    pushEqualizer();
    emit eqChanged();
}

void HeadphonesController::setEqBand(int index, int value)
{
    if (!equalizerAvailable() || !eqManual() || !eqBandsEditable()) return;
    if (index < 0 || index >= 5 || _eqBands[index] == value) return;
    _eqBands[index] = value;
    pushEqualizer();
    emit eqChanged();
}

// ---- Spatial (mutually exclusive) ----
void HeadphonesController::setSoundPosition(int v)
{
    if (!soundPositionAvailable()) return;
    if (v < 0 || v > 5) return;
    _soundPosition = v;
    if (v != 0) _vptType = 0;
    _headphones.setSurroundPosition(SOUND_POSITION_PRESET_ARRAY[_soundPosition]);
    _headphones.setVptType(_vptType);
    emit spatialChanged();
}

void HeadphonesController::setVptType(int v)
{
    if (!vptAvailable()) return;
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
    if (!dseeAvailable()) return;
    if (_dsee == v) return;
    _dsee = v;
    _headphones.setAudioUpsampling(v);
    emit systemChanged();
}

void HeadphonesController::setVoiceGuidance(bool v)
{
    if (!voiceGuidanceAvailable()) return;
    if (_voiceGuidance == v) return;
    _voiceGuidance = v;
    _headphones.setVoiceGuidance(v);
    emit systemChanged();
}

QVariantList HeadphonesController::autoPowerOffOptions()
{
    QVariantList options = knownAutoPowerOffChoices(_identity.model);
    // Whatever the device reports holding is, by definition, a choice it supports.
    if (auto reported = _headphones.getReportedAutoPowerOffElement())
    {
        const int mode = static_cast<int>(autoPowerOffMode(*reported));
        if (!options.contains(mode))
            options.push_back(mode);
    }
    return options;
}

void HeadphonesController::setAutoPowerOff(int v)
{
    if (!autoPowerOffAvailable()) return;
    if (_autoPowerOff == v || !autoPowerOffOptions().contains(v)) return;
    _autoPowerOff = v;
    _headphones.setAutoPowerOff(static_cast<AUTO_POWER_OFF>(v));
    emit systemChanged();
}

// ---- Headphone controls ----
// The engine refuses a request unless the device has described the control, reports it
// available and has reported its value - the facts that make the phase "ready".
void HeadphonesController::setTouchPanelEnabled(bool on)
{
    if (_demo)
    {
        _touchPanel = toggleMap(demoToggle(on));
        emit controlsChanged();
        return;
    }
    if (!connected() || _protocol != SonyProtocol::V1) return;
    if (_headphones.setTouchPanel(on))
        syncControls();
    else
        ProtocolTrace::line("   UI: touch panel change refused: not described, unavailable, or value unread");
}

void HeadphonesController::setWearingControlEnabled(bool on)
{
    if (_demo)
    {
        _wearingControl = toggleMap(demoToggle(on));
        emit controlsChanged();
        return;
    }
    if (!connected() || _protocol != SonyProtocol::V1) return;
    if (_headphones.setWearingControl(on))
        syncControls();
    else
        ProtocolTrace::line("   UI: wearing control change refused: not described, unavailable, or value unread");
}

void HeadphonesController::syncControls()
{
    const QVariantMap touch = toggleMap(_headphones.getTouchPanel());
    const QVariantMap wearing = toggleMap(_headphones.getWearingControl());
    if (touch == _touchPanel && wearing == _wearingControl)
        return;
    traceControl("touch panel", _touchPanel, touch);
    traceControl("wearing control", _wearingControl, wearing);
    _touchPanel = touch;
    _wearingControl = wearing;
    emit controlsChanged();
}

// ---- Multipoint mode ----
void HeadphonesController::setMultipointEnabled(bool on)
{
    if (_demo)
    {
        // Preview the confirmation the way the headphones would ask for it.
        if (_multipointMode.value("busy").toBool() || _multipointMode.value("checked").toBool() == on)
            return;
        MultipointChange change;
        change.step = MultipointStep::Confirming;
        change.requested = on;
        change.alert = CommandSerializer::MultipointAlert{
            on ? v1::ALERT_MSG_MULTIPOINT_TO_ON : v1::ALERT_MSG_MULTIPOINT_TO_OFF, v1::ALERT_ACTION_POSITIVE_NEGATIVE };
        ToggleSnapshot t = demoToggle(!on);
        t.requested = on;
        t.status = SettingStatus::Acknowledged;
        _multipointMode = multipointMap(t, change);
        _multipointAlert = alertMap(change);
        emit controlsChanged();
        emit multipointAlertChanged();
        return;
    }
    // While Aural is checking an earlier change on its own, that check comes first.
    if (!connected() || _protocol != SonyProtocol::V1 || _mpReconnect) return;
    if (_headphones.setMultipointMode(on))
        syncMultipoint();
    else
        ProtocolTrace::line("   UI: multipoint change refused: not ready, or another change is in progress");
}

void HeadphonesController::answerMultipointAlert(int messageId, bool accept)
{
    if (messageId < 0 || messageId > 0xFF) return;
    if (_demo)
    {
        if (_multipointAlert.value("messageId").toInt() != messageId || _multipointAlert.isEmpty()) return;
        const bool on = _multipointAlert.value("turningOn").toBool();
        _multipointMode = multipointMap(demoToggle(accept ? on : !on), MultipointChange{});
        _multipointAlert.clear();
        emit controlsChanged();
        emit multipointAlertChanged();
        emit toast(accept ? (on ? tr("Multipoint is now on.") : tr("Multipoint is now off."))
                          : tr("Multipoint change cancelled."));
        return;
    }
    // The engine checks the ID against the alert it is holding: an answer to anything else,
    // including an alert that has since been settled, goes nowhere.
    if (_headphones.answerMultipointAlert(static_cast<unsigned char>(messageId), accept))
    {
        _mpCancelled = !accept;
        syncMultipoint();
    }
}

QString HeadphonesController::reconnectReason() const
{
    if (!_mpReconnect || _mpReconnect->connectedMs >= 0)
        return QString();
    return tr("Reconnecting to %1 to check the multipoint change...").arg(displayModel(_mpReconnect->device.name));
}

void HeadphonesController::cancelReconnect()
{
    if (!_mpReconnect) return;
    _mpReconnect.reset();
    emit reconnectChanged();
}

void HeadphonesController::syncMultipoint()
{
    const MultipointChange change = _headphones.getMultipointChange();
    const ToggleSnapshot mode = _headphones.getMultipointMode();

    // A change that ended within the session. (One the session ended under is followed up by
    // endSessionAfterLinkLoss and driveMultipointReconnect instead.)
    if (_mpLastStep != MultipointStep::None && change.step == MultipointStep::None)
    {
        if (_mpCancelled)
            emit toast(tr("Multipoint change cancelled."));
        else if (_mpAbandoned)
            emit toast(tr("The headphones didn't confirm the multipoint change. Showing the setting they report."));
        else if (mode.valueKnown && mode.value == _mpLastRequested)
            emit toast(_mpLastRequested ? tr("Multipoint is now on.") : tr("Multipoint is now off."));
        else if (_mpLastStep == MultipointStep::Requested)
            emit toast(tr("Multipoint can't be changed right now."));
        _mpCancelled = false;
        _mpAbandoned = false;
    }

    if (change.step != _mpLastStep)
        _mpStepSinceMs = nowMs();
    _mpLastStep = change.step;
    _mpLastRequested = change.requested;

    // Written, and the headphones have neither confirmed it, asked about it, nor dropped the
    // link for it. Stop waiting and read the value; nothing is resent.
    if ((change.step == MultipointStep::Sent || change.step == MultipointStep::Accepted) &&
        nowMs() - _mpStepSinceMs > MULTIPOINT_SILENCE_MS)
    {
        _mpAbandoned = true;
        _headphones.abandonMultipointChange();
    }

    QVariantMap map = multipointMap(mode, change);
    if (_mpReconnect && _mpReconnect->connectedMs >= 0)
    {
        // Back on the headphones after the link dropped mid-change: until the value is read, the
        // change is still a request, not a fact.
        map["step"] = QStringLiteral("verifying");
        map["busy"] = true;
        map["pending"] = true;
        map["checked"] = _mpReconnect->requested;
        map["note"] = tr("Checking whether the headphones applied the change...");
    }
    if (map != _multipointMode)
    {
        traceControl("multipoint mode", _multipointMode, map);
        _multipointMode = map;
        emit controlsChanged();
    }

    const QVariantMap alert = alertMap(change);
    if (alert != _multipointAlert)
    {
        _multipointAlert = alert;
        emit multipointAlertChanged();
    }
}

void HeadphonesController::endSessionAfterLinkLoss(const QString& message)
{
    const BluetoothDevice device = _device;
    MultipointChange change;
    teardownConnection(&change);

    // A change that was out when the link went - written, asked about, answered - may be why
    // it went: the headphones can drop the connection to apply it. Reconnect and read the value.
    const bool changeOut = change.step == MultipointStep::Sent || change.step == MultipointStep::Confirming ||
        change.step == MultipointStep::Answering || change.step == MultipointStep::Accepted;
    const qint64 now = nowMs();
    if (_mpReconnect)
    {
        // Lost again while checking an earlier change: keep checking, within the same deadline.
        _mpReconnect->connectedMs = -1;
        _mpReconnect->nextAttemptMs = now + RECONNECT_FIRST_DELAY_MS;
        emit reconnectChanged();
    }
    else if (changeOut && !device.mac.empty())
    {
        _mpReconnect = MultipointReconnect{ device, change.requested, now, now + RECONNECT_FIRST_DELAY_MS, -1 };
        emit reconnectChanged();
        emit toast(tr("The headphones disconnected during the multipoint change. Reconnecting to check it..."));
    }
    else
    {
        emit errorOccurred(message);
    }
    refreshDevices();
}

void HeadphonesController::driveMultipointReconnect()
{
    if (!_mpReconnect) return;
    MultipointReconnect& r = *_mpReconnect;
    const qint64 now = nowMs();

    if (connected())
    {
        if (_device.mac != r.device.mac)
        {
            // Other headphones: the change cannot be checked on these.
            _mpReconnect.reset();
            emit reconnectChanged();
            return;
        }
        if (r.connectedMs < 0)
        {
            r.connectedMs = now;
            emit reconnectChanged();
        }
        // The new session rediscovers the setting and reads it; the old request is not resent.
        const ToggleSnapshot mode = _headphones.getMultipointMode();
        if (mode.valueKnown)
        {
            if (mode.value == r.requested)
                finishMultipointReconnect(mode.value ? tr("Multipoint is now on.") : tr("Multipoint is now off."));
            else
                finishMultipointReconnect(mode.value ? tr("The headphones kept multipoint on.")
                                                     : tr("The headphones kept multipoint off."));
        }
        else if (mode.support == CapabilityState::Unsupported)
            finishMultipointReconnect(tr("Couldn't check the multipoint change: the headphones no longer report the setting."));
        else if (now - r.connectedMs > VERIFY_TIMEOUT_MS)
            finishMultipointReconnect(tr("Couldn't read the multipoint setting after reconnecting."));
        return;
    }

    if (_connectFuture.valid() || _bt.isConnected())
        return;   // an attempt is under way
    if (now - r.startedMs > RECONNECT_GIVE_UP_MS)
    {
        finishMultipointReconnect(tr("Couldn't reconnect to check the multipoint change. Connect again to see the current setting."));
        return;
    }
    if (now >= r.nextAttemptMs)
    {
        r.nextAttemptMs = now + RECONNECT_RETRY_MS;
        startConnect(QString::fromStdString(r.device.mac), QString::fromStdString(r.device.name));
    }
}

void HeadphonesController::finishMultipointReconnect(const QString& message)
{
    _mpReconnect.reset();
    emit reconnectChanged();
    emit toast(message);
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
    // The user picked other headphones: there is nothing left to check on the old ones.
    if (_mpReconnect && mac.toStdString() != _mpReconnect->device.mac)
    {
        _mpReconnect.reset();
        emit reconnectChanged();
    }
    // A connection the user starts is a first attempt, with its own retry.
    _retryConnect.reset();
    _handshakeRetried = false;
    startConnect(mac, name);
}

void HeadphonesController::handshakeFailed(const QString& reason)
{
    // Checking a multipoint change already reconnects on its own schedule.
    if (_mpReconnect)
    {
        endSessionAfterLinkLoss(reason);
        return;
    }

    const BluetoothDevice device = _device;
    teardownConnection();
    if (!_handshakeRetried && !device.mac.empty())
    {
        _handshakeRetried = true;
        _retryConnect = RetryConnect{ device, nowMs() + HANDSHAKE_RETRY_DELAY_MS };
        ProtocolTrace::line("   UI: the headphones did not answer the handshake (" + reason.toStdString() +
            "); reconnecting once");
        emit toast(tr("The headphones didn't answer. Reconnecting..."));
    }
    else
    {
        _handshakeRetried = false;
        emit errorOccurred(tr("The headphones didn't respond. Make sure they're connected in Windows, then try again."));
    }
    refreshDevices();
}

void HeadphonesController::startConnect(const QString& mac, const QString& name)
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
    // The user's disconnect also ends any reconnecting Aural was doing on its own.
    if (_mpReconnect)
    {
        _mpReconnect.reset();
        emit reconnectChanged();
    }
    _retryConnect.reset();
    _handshakeRetried = false;
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
            endSessionAfterLinkLoss(QStringLiteral("The headphones closed the connection"));
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
        // While reconnecting on its own to check a multipoint change, Aural expects attempts to
        // fail until the headphones are back; driveMultipointReconnect reports the outcome once.
        catch (const RecoverableException& exc)
        {
            if (!stale)
            {
                if (exc.shouldDisconnect) _bt.disconnect();
                if (!_mpReconnect) emit errorOccurred(exc.what());
                setConn(Conn::Disconnected);
                refreshDevices();
            }
        }
        catch (const std::exception& exc)
        {
            if (!stale)
            {
                if (!_mpReconnect) emit errorOccurred(exc.what());
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
        const bool wasHandshake = _handshakePending;
        _handshakePending = false;
        try
        {
            _commandFuture.get();
            // "Sent" is the honest word: the headphones acknowledged the frames. Only a
            // read-back reply can promote a setting to confirmed (see settingStatus).
            if (!stale && wasSettingsPush)
                emit toast("Settings sent");
            // A handshake that went through: a later failure earns its own retry.
            if (!stale && wasHandshake)
                _handshakeRetried = false;
        }
        catch (const RecoverableException& exc)
        {
            if (!stale)
            {
                if (exc.shouldDisconnect && wasHandshake)
                    handshakeFailed(QString::fromUtf8(exc.what()));
                else if (exc.shouldDisconnect)
                    endSessionAfterLinkLoss(QStringLiteral("Disconnected due to: ") + exc.what());
                else
                    emit errorOccurred(exc.what());
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

    // The one automatic retry after a failed handshake, once its pause is over.
    if (_retryConnect && !_bt.isConnected() && !_connectFuture.valid() && nowMs() >= _retryConnect->atMs)
    {
        const BluetoothDevice device = _retryConnect->device;
        _retryConnect.reset();
        startConnect(QString::fromStdString(device.mac), QString::fromStdString(device.name));
    }

    driveMultipointReconnect();
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

    // The whole advertised set, not only the legacy bits: every per-setting gate reads from it.
    unsigned int caps = _headphones.getCapabilities();
    bool capsKnown = _headphones.capabilitiesKnown();
    std::vector<unsigned char> functions = _headphones.supportedFunctions();
    if (caps != _cCaps || capsKnown != _cCapsKnown || functions != _cFunctions)
    {
        _cCaps = caps;
        _cCapsKnown = capsKnown;
        _cFunctions = std::move(functions);
        emit capabilitiesChanged();
        traceOffer();
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

    syncReadBack(_headphones.getStatuses());
    syncControls();
    syncMultipoint();

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

// Same rule as the ambient block above: compare against the last engine value we copied, not
// the UI value, so a change the user made is only overwritten by something the device says
// afterwards. An engine value moves on an ACK (to what we sent - already on screen) and on a
// device report (which is what we want shown, including a value the device chose over ours).
void HeadphonesController::syncReadBack(const SettingsStatus& statuses)
{
    if (statuses.equalizer != SettingStatus::Requested)
    {
        const EqualizerConfig eq = _headphones.getEqualizer();
        const int levels = _headphones.getEqualizerLevelCount();
        if (!(eq == _cEq) || levels != _cEqLevelCount)
        {
            _cEq = eq;
            _cEqLevelCount = levels;
            _eqPreset = eqPresetIndex(eq.preset);
            _eqClearBass = eq.clearBass;
            for (int i = 0; i < 5 && i < static_cast<int>(eq.bands.size()); ++i)
                _eqBands[i] = eq.bands[i];
            emit eqChanged();
        }
    }

    if (statuses.spatial != SettingStatus::Requested)
    {
        const int vpt = _headphones.getVptType();
        const SOUND_POSITION_PRESET position = _headphones.getSurroundPosition();
        if (vpt != _cVpt || position != _cPosition)
        {
            _cVpt = vpt;
            _cPosition = position;
            // The UI has the four venues; the reserved presets have no control to show them on.
            _vptType = (vpt >= 0 && vpt <= 4) ? vpt : 0;
            _soundPosition = soundPositionIndex(position);
            emit spatialChanged();
        }
    }

    if (statuses.speakToChat != SettingStatus::Requested)
    {
        const S2C_TOGGLE toggle = _headphones.getS2CToggle();
        const unsigned int options = _headphones.getS2COptions();
        if (toggle != _cS2c || options != _cS2cOptions)
        {
            _cS2c = toggle;
            _cS2cOptions = options;
            _s2cEnabled = toggle == S2C_TOGGLE::ACTIVE;
            _s2cSensitivity = static_cast<int>((options >> 16) & 0xff);
            _s2cVoice = ((options >> 8) & 0xff) != 0;
            _s2cAutoOff = static_cast<int>(options & 0xff);
            emit s2cChanged();
        }
    }

    bool systemDelta = false;
    if (statuses.dsee != SettingStatus::Requested)
    {
        const bool dsee = _headphones.getAudioUpsampling();
        if (dsee != _cDsee) { _cDsee = dsee; _dsee = dsee; systemDelta = true; }
    }
    if (statuses.voiceGuidance != SettingStatus::Requested)
    {
        const bool guidance = _headphones.getVoiceGuidance();
        if (guidance != _cVoiceGuidance) { _cVoiceGuidance = guidance; _voiceGuidance = guidance; systemDelta = true; }
    }
    if (statuses.autoPowerOff != SettingStatus::Requested)
    {
        const AutoPowerOffConfig powerOff = _headphones.getAutoPowerOffConfig();
        if (!(powerOff == _cAutoPowerOff))
        {
            _cAutoPowerOff = powerOff;
            _autoPowerOff = static_cast<int>(autoPowerOffMode(powerOff.active));
            systemDelta = true;
        }
    }
    if (systemDelta)
        emit systemChanged();

    // A reported element can widen the offered choices.
    const auto reported = _headphones.getReportedAutoPowerOffElement();
    const int reportedElement = reported ? static_cast<int>(*reported) : -1;
    if (reportedElement != _cReportedPowerOff)
    {
        _cReportedPowerOff = reportedElement;
        emit capabilitiesChanged();
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
        _handshakePending = true;
        _commandFuture.setFromAsync([this]() { _headphones.queryState(); });
    }
}

// The single place a session ends. Stop the reader, close the socket, drain every
// outstanding future, then reset device and UI state so nothing survives into the next
// connection. changeAtEnd, when given, receives the multipoint change as the session left it:
// read once nothing can move it any more, and before it is forgotten.
void HeadphonesController::teardownConnection(MultipointChange* changeAtEnd)
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

    if (changeAtEnd)
        *changeAtEnd = _headphones.getMultipointChange();
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
    _handshakePending = false;
    _optimizeRequested = false;

    _device = BluetoothDevice{ "", "" };
    _identity = DeviceIdentity{};
    _protocol = SonyProtocol::V1;

    _cBattery = -1;
    _cCharging = false;
    _cCaps = 0;
    _cCapsKnown = false;
    _cFunctions.clear();
    _cOptimizing = false;
    _cFocusAvail = false;
    _cAmbientOn = false;
    _cAsmLevel = 0;
    _cFocusVoice = false;
    _cEq = EqualizerConfig{};
    _cEqLevelCount = -1;
    _cVpt = 0;
    _cPosition = SOUND_POSITION_PRESET::OFF;
    _cS2c = S2C_TOGGLE::INACTIVE;
    _cS2cOptions = 0;
    _cDsee = false;
    _cVoiceGuidance = false;
    _cAutoPowerOff = AutoPowerOffConfig{};
    _cReportedPowerOff = -1;

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
    _touchPanel = toggleMap(ToggleSnapshot{});
    _wearingControl = toggleMap(ToggleSnapshot{});
    // The multipoint change ends with the session; a reconnect to check it (_mpReconnect) is
    // not session state and stays.
    _multipointMode = multipointMap(ToggleSnapshot{}, MultipointChange{});
    _multipointAlert.clear();
    _mpLastStep = MultipointStep::None;
    _mpLastRequested = false;
    _mpStepSinceMs = 0;
    _mpCancelled = false;
    _mpAbandoned = false;
    _mpDevices.clear();
    _settingStatus = statusMap(SettingsStatus{});

    emit ambientChanged();
    emit eqChanged();
    emit spatialChanged();
    emit s2cChanged();
    emit systemChanged();
    emit controlsChanged();
    emit multipointAlertChanged();
    emit multipointChanged();
    emit batteryChanged();
    emit capabilitiesChanged();
    emit optimizerChanged();
    emit settingStatusChanged();
    emit connectionChanged();
}
