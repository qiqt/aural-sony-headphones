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
#include <optional>

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
    Q_PROPERTY(QVariantMap v2Info READ v2Info NOTIFY extrasChanged)
    Q_PROPERTY(int asmMaximum READ asmMaximum NOTIFY ambientChanged)
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)
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

    // Capabilities. capabilitiesKnown says whether a valid supported-functions reply (07) has
    // arrived this session; until then every answer is Unknown. Whether a given V1 model answers
    // the query at all has not been established on hardware yet (see PROTOCOL.md).
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

    // Which parts of the control surface this connection actually gets. Each follows its own
    // advertised function ID once the device has answered the capability query; until then a
    // recognised model keeps the controls it always had.
    Q_PROPERTY(bool ambientAvailable READ ambientAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool equalizerAvailable READ equalizerAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool spatialAvailable READ spatialAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool vptAvailable READ vptAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool soundPositionAvailable READ soundPositionAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool dseeAvailable READ dseeAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool voiceGuidanceAvailable READ voiceGuidanceAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool autoPowerOffAvailable READ autoPowerOffAvailable NOTIFY capabilitiesChanged)
    // Any of DSEE, voice guidance or auto power-off.
    Q_PROPERTY(bool systemSettingsAvailable READ systemSettingsAvailable NOTIFY capabilitiesChanged)
    // Non-empty when controls are being withheld, and says why. Empty otherwise.
    Q_PROPERTY(QString limitedControlReason READ limitedControlReason NOTIFY capabilitiesChanged)

    // Per-setting status: "unknown" | "requested" | "acknowledged" | "confirmed" | "failed".
    // Keys: ambient, equalizer, spatial, speakToChat, dsee, voiceGuidance, autoPowerOff,
    // optimizer, multipoint (paired-device operations), touchPanel, wearingControl,
    // multipointMode (the on/off setting). An "acknowledged" setting was received by the
    // headphones; only "confirmed" means the device reported that value back.
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

    // Equalizer. eqPreset is an index into the UI preset table, or -1 when the device reports
    // a preset that table does not contain.
    Q_PROPERTY(int eqPreset READ eqPreset WRITE setEqPreset NOTIFY eqChanged)
    Q_PROPERTY(int eqClearBass READ eqClearBass WRITE setEqClearBass NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqBands READ eqBands NOTIFY eqChanged)
    Q_PROPERTY(bool eqManual READ eqManual NOTIFY eqChanged)
    // False once the device reports a band layout other than clear bass + 5 bands.
    Q_PROPERTY(bool eqBandsEditable READ eqBandsEditable NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqPresetOptions READ eqPresetOptions NOTIFY eqChanged)
    Q_PROPERTY(int eqMinimum READ eqMinimum NOTIFY eqChanged)
    Q_PROPERTY(int eqMaximum READ eqMaximum NOTIFY eqChanged)
    Q_PROPERTY(bool eqSavePending READ eqSavePending NOTIFY eqChanged)
    Q_PROPERTY(bool eqCanSave READ eqCanSave NOTIFY eqChanged)

    // Spatial sound (mutually exclusive)
    Q_PROPERTY(int soundPosition READ soundPosition WRITE setSoundPosition NOTIFY spatialChanged)
    Q_PROPERTY(int vptType READ vptType WRITE setVptType NOTIFY spatialChanged)

    // Speak-to-Chat
    Q_PROPERTY(bool s2cEnabled READ s2cEnabled WRITE setS2cEnabled NOTIFY s2cChanged)
    Q_PROPERTY(int s2cSensitivity READ s2cSensitivity WRITE setS2cSensitivity NOTIFY s2cChanged)
    Q_PROPERTY(int s2cAutoOff READ s2cAutoOff WRITE setS2cAutoOff NOTIFY s2cChanged)
    Q_PROPERTY(bool s2cVoice READ s2cVoice WRITE setS2cVoice NOTIFY s2cChanged)

    // System / sound quality. Values follow the device's read-back when it gives one.
    Q_PROPERTY(bool dsee READ dsee WRITE setDsee NOTIFY systemChanged)
    Q_PROPERTY(QVariantMap audioInfo READ audioInfo NOTIFY extrasChanged)
    Q_PROPERTY(QVariantList buttonAssignments READ buttonAssignments NOTIFY extrasChanged)
    Q_PROPERTY(bool voiceGuidance READ voiceGuidance WRITE setVoiceGuidance NOTIFY systemChanged)
    // An AUTO_POWER_OFF value: 0 off, 1-4 timers, 5 when taken off.
    Q_PROPERTY(int autoPowerOff READ autoPowerOff WRITE setAutoPowerOff NOTIFY systemChanged)
    // Device-advertised choices take precedence; model/current-state fallback until decoded.
    // Nothing outside this list is sent. Updated as capabilities arrive or the session resets.
    Q_PROPERTY(QVariantList autoPowerOffOptions READ autoPowerOffOptions NOTIFY capabilitiesChanged)

    // Controls the headphones have to describe before the app may touch them. Each is a map:
    //   phase      "absent"      not offered: no V1 connection, not described (yet), or the
    //                            device has no such setting
    //              "loading"     described, but its availability or current value is unread
    //              "unavailable" the device reports it cannot be changed right now
    //              "ready"       available and its current value is known
    //   valueKnown the device has reported the value this session
    //   checked    what the switch shows: the request while one is unanswered, otherwise the
    //              value the device last reported
    //   pending    a request has not been answered by a read yet
    //   status     the settingStatus vocabulary
    // Touch panel turns the ear-cup touch controls on or off; it does not change gestures.
    // Wearing control is the setting that lets the headset act on wearing detection, not a
    // reading of whether the headphones are being worn.
    Q_PROPERTY(QVariantMap touchPanel READ touchPanel NOTIFY controlsChanged)
    Q_PROPERTY(QVariantMap wearingControl READ wearingControl NOTIFY controlsChanged)

    // Multipoint mode, the setting that lets the headphones hold two connections. The same keys,
    // plus:
    //   step   "idle" | "sending" | "waiting" | "confirming" | "answering" | "applying" | "verifying"
    //   busy   a change is in progress; the switch takes no other until it ends
    //   note   what the change is waiting for, in words; empty when idle
    // A change can need the user's consent (multipointAlert) and can make the headphones drop the
    // connection to apply it. Aural then reconnects and reads the value; it never resends it.
    Q_PROPERTY(QVariantMap multipointMode READ multipointMode NOTIFY controlsChanged)
    // The confirmation the headphones asked for, or an empty map. Keys: messageId (quote it back
    // in answerMultipointAlert), turningOn, choice (true: accept or cancel; false: acknowledge only).
    Q_PROPERTY(QVariantMap multipointAlert READ multipointAlert NOTIFY multipointAlertChanged)
    // Non-empty while Aural is reconnecting on its own to check a multipoint change, and says so.
    Q_PROPERTY(QString reconnectReason READ reconnectReason NOTIFY reconnectChanged)

    // Multipoint
    Q_PROPERTY(QVariantList mpDevices READ mpDevices NOTIFY multipointChanged)

public:
    explicit HeadphonesController(QObject* parent = nullptr);
    // Inject a transport; tests can disable startup connection for manual-flow cases.
    explicit HeadphonesController(std::unique_ptr<IBluetoothConnector> connector,
                                  QObject* parent, bool autoConnectOnStartup = true);
    ~HeadphonesController() override;

    QVariantMap diagnostics();
    QVariantMap v2Info() const { return _v2Info; }
    int asmMaximum();
    Q_INVOKABLE void refreshV2Values();
    Q_INVOKABLE void setV2Audio(int selector, int value);
    Q_INVOKABLE void setV2EqPreset(int selector, int preset);
    Q_INVOKABLE void setV2EqBand(int selector, int band, int value);
    Q_INVOKABLE void setV2Ambient(int selector, int mode, int level, bool voice);
    Q_INVOKABLE QString exportDiagnostics();
    bool discovering() const { return _conn == Conn::Discovering; }
    bool connecting() const { return _conn == Conn::Connecting; }
    bool connected() const { return _conn == Conn::Connected; }
    QString deviceName() const;
    QString deviceModel();
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
    bool vptAvailable();
    bool soundPositionAvailable();
    bool dseeAvailable();
    bool voiceGuidanceAvailable();
    bool autoPowerOffAvailable();
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
    bool eqBandsEditable();
    QVariantList eqPresetOptions();
    int eqMinimum();
    int eqMaximum();
    bool eqSavePending();
    bool eqCanSave();
    Q_INVOKABLE void saveEqToCustom(int slot);
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
    QVariantList autoPowerOffOptions();
    void setDsee(bool v);
    void setVoiceGuidance(bool v);
    void setAutoPowerOff(int v);
    QVariantMap audioInfo() const { return _audioInfo; }
    QVariantList buttonAssignments() const { return _buttonAssignments; }
    Q_INVOKABLE void setConnectionPreference(int value);
    Q_INVOKABLE void setButtonAssignment(int keyIndex, int presetId);

    QVariantMap touchPanel() const { return _touchPanel; }
    QVariantMap wearingControl() const { return _wearingControl; }
    // Ignored unless the control's phase is "ready".
    Q_INVOKABLE void setTouchPanelEnabled(bool on);
    Q_INVOKABLE void setWearingControlEnabled(bool on);

    QVariantMap multipointMode() const { return _multipointMode; }
    QVariantMap multipointAlert() const { return _multipointAlert; }
    QString reconnectReason() const;
    // Ignored unless the phase is "ready" and no change is in progress.
    Q_INVOKABLE void setMultipointEnabled(bool on);
    // The user's answer to multipointAlert. Ignored unless messageId is the alert's own.
    Q_INVOKABLE void answerMultipointAlert(int messageId, bool accept);
    // Stop reconnecting on our own; the device list comes back.
    Q_INVOKABLE void cancelReconnect();

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
    void diagnosticsChanged();
    void extrasChanged();
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
    void controlsChanged();
    void multipointAlertChanged();
    void reconnectChanged();
    void multipointChanged();
    void errorOccurred(const QString& message);
    void toast(const QString& message);

private:
    void syncExtras();
    QVariantMap _v2Info;
    QVariantMap _lastDiagnostics;
    QVariantMap _audioInfo;
    QVariantList _buttonAssignments;
    QVariantMap _extraSignature;
    enum class Conn { Disconnected, Discovering, Connecting, Connected };

    void init();
    // Is this feature offered on this connection? Supported -> yes, Unsupported -> no, and
    // Unknown -> only for a model we recognise (see the Q_PROPERTY block above).
    bool featureAvailable(DEVICE_CAPABILITIES cap);
    bool functionAvailable(FUNCTION_TYPE fn);
    void pump();
    void handleConnectionSetup();
    void teardownConnection(MultipointChange* changeAtEnd = nullptr);
    void resetUiState();
    void pollOsBattery();
    void emitLiveDeltas();
    // AURAL_TRACE only: log what this connection offers after a capability change.
    void traceOffer();
    // Copy values the device reported into the UI state. Skips a setting while a user change
    // to it is still waiting to be sent, so a notification cannot snap the control back.
    void syncReadBack(const SettingsStatus& statuses);
    // Re-read the discovered controls; emits controlsChanged when a map moved.
    void syncControls();
    // Follow the multipoint change: its map and alert, how it ended, and its timeout.
    void syncMultipoint();
    // The link went away underneath us (the reader saw it close, or a send failed). If a
    // multipoint change was out, that may be the headphones applying it: reconnect and check.
    void endSessionAfterLinkLoss(const QString& message);
    // The connect handshake failed: the headphones left a command on the new link unacknowledged.
    // Reconnect once on a fresh link before reporting it.
    void handshakeFailed(const QString& reason);
    // Reconnect attempts and verification for a multipoint change that outlived its session.
    void driveMultipointReconnect();
    void finishMultipointReconnect(const QString& message);
    void startConnect(const QString& mac, const QString& name);
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
    bool _autoConnectPending = true;
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
    std::vector<unsigned char> _cFunctions;
    bool _cOptimizing = false;
    bool _cFocusAvail = false;
    bool _cAmbientOn = false;
    int _cAsmLevel = 0;
    bool _cFocusVoice = false;
    // Last engine values copied into the UI, per read-back setting.
    EqualizerConfig _cEq;
    int _cEqLevelCount = -1;
    int _cVpt = 0;
    SOUND_POSITION_PRESET _cPosition = SOUND_POSITION_PRESET::OFF;
    S2C_TOGGLE _cS2c = S2C_TOGGLE::INACTIVE;
    unsigned int _cS2cOptions = 0;
    bool _cDsee = false;
    bool _cVoiceGuidance = false;
    AutoPowerOffConfig _cAutoPowerOff;
    int _cReportedPowerOff = -1;

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

    // Built from the engine's snapshots (see syncControls); never edited from the UI side.
    QVariantMap _touchPanel;
    QVariantMap _wearingControl;
    QVariantMap _multipointMode;
    QVariantMap _multipointAlert;

    // The multipoint change as last seen, to tell how it ended and how long it has waited.
    MultipointStep _mpLastStep = MultipointStep::None;
    bool _mpLastRequested = false;
    qint64 _mpStepSinceMs = 0;
    bool _mpCancelled = false;   // the user answered the alert with Cancel
    bool _mpAbandoned = false;   // we stopped waiting for the headphones

    // A multipoint change that was out when the link dropped. Outlives the session on purpose:
    // it is what makes Aural reconnect to the same headphones and read the value.
    struct MultipointReconnect
    {
        BluetoothDevice device;
        bool requested = false;
        qint64 startedMs = 0;
        qint64 nextAttemptMs = 0;
        qint64 connectedMs = -1;   // when the new session came up; -1 while reconnecting
    };
    std::optional<MultipointReconnect> _mpReconnect;

    // The connect handshake, and the one automatic retry a failed handshake gets.
    bool _handshakePending = false;   // the command future is running queryState()
    bool _handshakeRetried = false;   // this connection attempt is already the retry
    struct RetryConnect
    {
        BluetoothDevice device;
        qint64 atMs = 0;
    };
    std::optional<RetryConnect> _retryConnect;

    QVariantList _mpDevices;

    bool _demo = false; // dev-only (AURAL_DEMO env): fake a connected device, no Bluetooth
};
