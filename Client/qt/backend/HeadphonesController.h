#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QTimer>
#include <QElapsedTimer>

#include <array>
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
class HeadphonesController : public QObject
{
    Q_OBJECT

    // Connection
    Q_PROPERTY(bool discovering READ discovering NOTIFY connectionChanged)
    Q_PROPERTY(bool connecting READ connecting NOTIFY connectionChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY connectionChanged)
    Q_PROPERTY(QString deviceModel READ deviceModel NOTIFY connectionChanged)
    Q_PROPERTY(bool isV2 READ isV2 NOTIFY connectionChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)

    // Battery
    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY batteryChanged)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY batteryChanged)

    // Capabilities
    Q_PROPERTY(bool supportsOptimizer READ supportsOptimizer NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsSpeakToChat READ supportsSpeakToChat NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsMultipoint READ supportsMultipoint NOTIFY capabilitiesChanged)

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
    ~HeadphonesController() override;

    bool discovering() const { return _conn == Conn::Discovering; }
    bool connecting() const { return _conn == Conn::Connecting; }
    bool connected() const { return _conn == Conn::Connected; }
    QString deviceName() const;
    QString deviceModel() const;
    bool isV2() const { return _protocol == SonyProtocol::V2; }
    QVariantList devices() const { return _devices; }
    bool syncing() const { return _syncing; }

    int batteryLevel();
    bool batteryCharging();

    bool supportsOptimizer();
    bool supportsSpeakToChat();
    bool supportsMultipoint();

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
    // Dark/light native window chrome (title bar). No-op off Windows.
    Q_INVOKABLE void applyWindowTheme(QObject* window, bool dark);

signals:
    void connectionChanged();
    void devicesChanged();
    void syncingChanged();
    void batteryChanged();
    void capabilitiesChanged();
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

    void pump();
    void handleConnectionSetup();
    void teardownConnection();
    void pollOsBattery();
    void emitLiveDeltas();
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

    QTimer _timer;
    QElapsedTimer _clock;

    Conn _conn = Conn::Disconnected;
    BluetoothDevice _device;
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
    bool _cOptimizing = false;
    bool _cFocusAvail = false;
    bool _cAmbientOn = false;
    int _cAsmLevel = 0;
    bool _cFocusVoice = false;

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
