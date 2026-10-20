#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
#include <QElapsedTimer>

#include <array>
#include <atomic>
#include <memory>

#include "BluetoothWrapper.h"
#include "Headphones.h"
#include "Listener.h"
#include "AsyncTask.h"
#include "Constants.h"

// Qt/QML bridge over the existing C++ engine (Headphones + BluetoothWrapper + Listener).
//
// A periodic pump() (QTimer, replacing the old render loop) drives discovery/connect futures,
// pushes changed settings via Headphones::setChanges(), and polls battery. UI-owned settings
// are set through WRITE setters; device-driven values are read each tick and surfaced via NOTIFY.
//
// Session ownership: every connection gets an id. Asynchronous results carry the id they were
// started under and are discarded if they arrive after that session ended, so a slow discovery
// or battery lookup from a previous device cannot describe the current one. teardownConnection()
// is the single place a session ends - it stops the reader, closes the socket, drains every
// outstanding future and resets device state.
class HeadphonesController : public QObject
{
    Q_OBJECT

    // Connection
    Q_PROPERTY(bool discovering READ discovering NOTIFY connectionChanged)
    Q_PROPERTY(bool connecting READ connecting NOTIFY connectionChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY connectionChanged)
    Q_PROPERTY(QString deviceModel READ deviceModel NOTIFY connectionChanged)
    // False when the Bluetooth name doesn't match a model we know: the UI then shows the raw
    // name and says the protocol choice is an attempt, not a fact.
    Q_PROPERTY(bool modelRecognised READ modelRecognised NOTIFY connectionChanged)
    Q_PROPERTY(bool isV2 READ isV2 NOTIFY connectionChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)

    // Battery
    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY batteryChanged)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY batteryChanged)

    // Capabilities. Most v1 devices never reply to the capability query, so the answer is
    // usually Unknown; capabilitiesKnown says whether it is an answer at all.
    //
    // What we do with Unknown depends on whether we recognise the device (SON-11): for a model
    // this client has code paths for, Unknown means "the device just doesn't answer that query"
    // and the control is offered. For a device we cannot identify, Unknown means we know
    // nothing, and offering a model-specific command would be a guess made with the user's
    // headphones - so it stays hidden. See featureAvailable().
    Q_PROPERTY(bool capabilitiesKnown READ capabilitiesKnown NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsOptimizer READ supportsOptimizer NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsSpeakToChat READ supportsSpeakToChat NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsMultipoint READ supportsMultipoint NOTIFY capabilitiesChanged)

    // Which parts of the control surface this connection actually gets.
    Q_PROPERTY(bool ambientAvailable READ ambientAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool equalizerAvailable READ equalizerAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool spatialAvailable READ spatialAvailable NOTIFY capabilitiesChanged)
    // DSEE, voice guidance and auto power off: write-only, no capability bit, no read-back.
    Q_PROPERTY(bool systemSettingsAvailable READ systemSettingsAvailable NOTIFY capabilitiesChanged)
    // Non-empty when controls are being withheld, and says why. Empty otherwise.
    Q_PROPERTY(QString limitedControlReason READ limitedControlReason NOTIFY capabilitiesChanged)

    // Per-setting status: "unknown" | "requested" | "acknowledged" | "confirmed" | "failed".
    // Keys: ambient, equalizer, spatial, speakToChat, dsee, voiceGuidance, autoPowerOff,
    // optimizer, multipoint. An "acknowledged" setting was received by the headphones; only
    // "confirmed" means the device reported that value back.
    Q_PROPERTY(QVariantMap settingStatus READ settingStatus NOTIFY settingStatusChanged)

    // Ambient Sound. ambientMode: 0 = Noise Cancelling, 1 = Ambient Sound, 2 = Off.
    // On the wire (v1): NC = (ambient on, level 0), Ambient = (ambient on, level 1-19),
    // Off = NC/ASM effect off. State is read back on connect (NCASM GET) and on device
    // NOTIFY frames (e.g. the physical ANC button), so the UI tracks the headphones.
    Q_PROPERTY(int ambientMode READ ambientMode WRITE setAmbientMode NOTIFY ambientChanged)
    Q_PROPERTY(int asmLevel READ asmLevel WRITE setAsmLevel NOTIFY ambientChanged)
    Q_PROPERTY(bool focusOnVoice READ focusOnVoice WRITE setFocusOnVoice NOTIFY ambientChanged)
    Q_PROPERTY(bool focusOnVoiceAvailable READ focusOnVoiceAvailable NOTIFY ambientChanged)
    Q_PROPERTY(bool optimizing READ optimizing NOTIFY optimizerChanged)

    // Equalizer
    Q_PROPERTY(int eqPreset READ eqPreset WRITE setEqPreset NOTIFY eqChanged)
    Q_PROPERTY(int eqClearBass READ eqClearBass WRITE setEqClearBass NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqBands READ eqBands NOTIFY eqChanged)
    Q_PROPERTY(bool eqManual READ eqManual NOTIFY eqChanged)

    // Spatial sound (mutually exclusive)
    Q_PROPERTY(int soundPosition READ soundPosition WRITE setSoundPosition NOTIFY spatialChanged)
    Q_PROPERTY(int vptType READ vptType WRITE setVptType NOTIFY spatialChanged)

    // Speak-to-Chat
    Q_PROPERTY(bool s2cEnabled READ s2cEnabled WRITE setS2cEnabled NOTIFY s2cChanged)
    Q_PROPERTY(int s2cSensitivity READ s2cSensitivity WRITE setS2cSensitivity NOTIFY s2cChanged)
    Q_PROPERTY(int s2cAutoOff READ s2cAutoOff WRITE setS2cAutoOff NOTIFY s2cChanged)
    Q_PROPERTY(bool s2cVoice READ s2cVoice WRITE setS2cVoice NOTIFY s2cChanged)

    // System / sound quality
    Q_PROPERTY(bool dsee READ dsee WRITE setDsee NOTIFY systemChanged)
    Q_PROPERTY(bool voiceGuidance READ voiceGuidance WRITE setVoiceGuidance NOTIFY systemChanged)
    Q_PROPERTY(int autoPowerOff READ autoPowerOff WRITE setAutoPowerOff NOTIFY systemChanged)

    // Multipoint
    Q_PROPERTY(QVariantList mpDevices READ mpDevices NOTIFY multipointChanged)

public:
    explicit HeadphonesController(QObject* parent = nullptr);
    // Test seam: run the whole controller - sessions, futures, teardown - against a scripted
    // transport instead of the platform one. The only difference from the constructor above
    // is where the connector comes from.
    explicit HeadphonesController(std::unique_ptr<IBluetoothConnector> connector,
                                  QObject* parent);
    ~HeadphonesController() override;

    bool discovering() const { return _conn == Conn::Discovering; }
    bool connecting() const { return _conn == Conn::Connecting; }
    bool connected() const { return _conn == Conn::Connected; }
    QString deviceName() const;
    QString deviceModel() const;
    bool modelRecognised() const;
    bool isV2() const { return _protocol == SonyProtocol::V2; }
    QVariantList devices() const { return _devices; }
    bool syncing() const { return _syncing; }

    int batteryLevel();
    bool batteryCharging();

    bool capabilitiesKnown();
    bool supportsOptimizer();
    bool supportsSpeakToChat();
    bool supportsMultipoint();

    bool ambientAvailable();
    bool equalizerAvailable();
    bool spatialAvailable();
    bool systemSettingsAvailable();
    QString limitedControlReason();

    QVariantMap settingStatus() const { return _settingStatus; }

    int ambientMode() const { return _ambientMode; }
    int asmLevel() const { return _asmLevel; }
    bool focusOnVoice() const { return _focusOnVoice; }
    bool focusOnVoiceAvailable();
    bool optimizing();
    void setAmbientMode(int m);
    void setAsmLevel(int v);
    void setFocusOnVoice(bool v);

    int eqPreset() const { return _eqPreset; }
    int eqClearBass() const { return _eqClearBass; }
    QVariantList eqBands() const;
    bool eqManual() const;
    void setEqPreset(int v);
    void setEqClearBass(int v);

    int soundPosition() const { return _soundPosition; }
    int vptType() const { return _vptType; }
    void setSoundPosition(int v);
    void setVptType(int v);

    bool s2cEnabled() const { return _s2cEnabled; }
    int s2cSensitivity() const { return _s2cSensitivity; }
    int s2cAutoOff() const { return _s2cAutoOff; }
    bool s2cVoice() const { return _s2cVoice; }
    void setS2cEnabled(bool v);
    void setS2cSensitivity(int v);
    void setS2cAutoOff(int v);
    void setS2cVoice(bool v);

    bool dsee() const { return _dsee; }
    bool voiceGuidance() const { return _voiceGuidance; }
    int autoPowerOff() const { return _autoPowerOff; }
    void setDsee(bool v);
    void setVoiceGuidance(bool v);
    void setAutoPowerOff(int v);

    QVariantList mpDevices() const { return _mpDevices; }

    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void connectTo(const QString& mac, const QString& name);
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE void refreshBattery();
    Q_INVOKABLE void startOptimize();
    Q_INVOKABLE void cancelOptimize();
    Q_INVOKABLE void setEqBand(int index, int value);
    Q_INVOKABLE void mpDisconnect(int slot);
    // Cycle Noise Cancelling -> Ambient -> Off. Used by the tray menu.
    Q_INVOKABLE void cycleAmbientMode();

signals:
    void connectionChanged();
    void devicesChanged();
    void syncingChanged();
    void batteryChanged();
    void capabilitiesChanged();
    void settingStatusChanged();
    void ambientChanged();
    void optimizerChanged();
    void eqChanged();
    void spatialChanged();
    void s2cChanged();
    void systemChanged();
    void multipointChanged();
    void errorOccurred(const QString& message);
    void toast(const QString& message);

private:
    enum class Conn { Disconnected, Discovering, Connecting, Connected };

    void init();
    // Is this feature offered on this connection? Supported -> yes, Unsupported -> no, and
    // Unknown -> only for a model we recognise (see the Q_PROPERTY block above).
    bool featureAvailable(DEVICE_CAPABILITIES cap);
    void pump();
    void handleConnectionSetup();
    void teardownConnection();
    void resetUiState();
    void pollOsBattery();
    void emitLiveDeltas();
    void refreshSettingStatus();
    void setConn(Conn c);
    void setSyncing(bool s);
    void pushEqualizer();
    void pushAmbient();
    qint64 nowMs() const { return _clock.elapsed(); }

    BluetoothWrapper _bt;       // declared before _headphones (which holds a reference to it)
    Headphones _headphones;
    std::unique_ptr<Listener> _listener;

    AsyncTask<std::vector<BluetoothDevice>> _devicesFuture;
    AsyncTask<void> _connectFuture;
    AsyncTask<void> _commandFuture;
    AsyncTask<int> _osBatteryFuture;

    // Session an outstanding future belongs to; a result from an older session is discarded.
    unsigned long long _session = 1;
    unsigned long long _devicesSession = 0;
    unsigned long long _connectSession = 0;
    unsigned long long _commandSession = 0;
    unsigned long long _osBatterySession = 0;

    // Set from the Listener thread when the headphones close the link; acted on in pump().
    std::atomic<bool> _linkLost{ false };
    std::atomic<unsigned long long> _linkLostSession{ 0 };

    QTimer _timer;
    QElapsedTimer _clock;

    Conn _conn = Conn::Disconnected;
    BluetoothDevice _device;
    DeviceIdentity _identity;
    SonyProtocol _protocol = SonyProtocol::V1;
    QVariantList _devices;
    bool _syncing = false;
    bool _connectionInitialized = false;
    bool _wantBattery = false;

    int _osBatteryLevel = -1;
    qint64 _lastOsBatteryPollMs = -100000;

    // Cached device-driven values for change detection.
    int _cBattery = -1;
    bool _cCharging = false;
    unsigned int _cCaps = 0;
    bool _cCapsKnown = false;
    bool _cOptimizing = false;
    bool _cFocusAvail = false;
    bool _cAmbientOn = false;
    int _cAsmLevel = 0;
    bool _cFocusVoice = false;

    QVariantMap _settingStatus;

    // UI state. Ambient values track the device via the NCASM read-back; the rest mirror
    // what we last pushed (write-only settings).
    int _ambientMode = 2;   // matches the engine's initial "effect off" until the read-back lands
    int _asmLevel = 10;     // remembered ambient level for when the user re-enters Ambient mode
    bool _focusOnVoice = false;
    bool _optimizeRequested = false;
    bool _settingsPushPending = false;

    int _eqPreset = 0;                          // index into the preset table
    int _eqClearBass = 0;
    std::array<int, 5> _eqBands = { 0, 0, 0, 0, 0 };

    int _soundPosition = 0;                     // index into SOUND_POSITION_PRESET_ARRAY
    int _vptType = 0;

    bool _s2cEnabled = false;
    int _s2cSensitivity = 0;
    int _s2cAutoOff = 0;
    bool _s2cVoice = false;

    bool _dsee = false;
    bool _voiceGuidance = false;
    int _autoPowerOff = 0;

    QVariantList _mpDevices;

    bool _demo = false; // dev-only (AURAL_DEMO env): fake a connected device, no Bluetooth
};
