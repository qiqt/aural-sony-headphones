#pragma once

#include "BluetoothWrapper.h"
#include "Constants.h"

#include <mutex>

template <class T>
struct Property {
	T current;
	T desired;

	void fulfill();
	bool isFulfilled();
	void setState(T val);   // sync both current and desired from a device reply
};

// v1 equalizer state: a preset plus the manual band levels (clear bass + 5 bands), each -10..+10.
struct EqualizerConfig {
	int preset = 0;                              // EQ_PRESET code
	int clearBass = 0;                           // -10..+10
	std::vector<int> bands = { 0, 0, 0, 0, 0 };  // -10..+10 each

	bool operator==(const EqualizerConfig& other) const {
		return preset == other.preset && clearBass == other.clearBass && bands == other.bands;
	}
};

class Headphones {
public:
	Headphones(BluetoothWrapper& conn);

	void setAmbientSoundControl(bool val);
	bool getAmbientSoundControl();

	bool isFocusOnVoiceAvailable();
	void setFocusOnVoice(bool val);
	bool getFocusOnVoice();

	bool isSetAsmLevelAvailable();
	void setAsmLevel(int val);
	int getAsmLevel();

	void setSurroundPosition(SOUND_POSITION_PRESET val);
	SOUND_POSITION_PRESET getSurroundPosition();

	void setVptType(int val);
	int getVptType();

	// ANC / pressure optimizer
	void setOptimizerState(OPTIMIZER_STATE val);
	OPTIMIZER_STATE getOptimizerState();

	// Speak-to-Chat
	void setS2CToggle(S2C_TOGGLE val);
	void setS2COptions(int sensitivity, bool voice, int offTime);
	S2C_TOGGLE getS2CToggle();
	unsigned int getS2COptions();

	// Multipoint. Returns copies because the Listener thread can rewrite the list at any time.
	std::vector<BluetoothDevice> getDevices();
	std::pair<int, int> getConnectedDevices();
	void setMultiPointConnection(int connectionId, int newDevice, int oldDevice);

	// Equalizer
	void setEqualizer(const EqualizerConfig& cfg);
	EqualizerConfig getEqualizer();

	// DSEE / DSEE Extreme upscaling
	void setAudioUpsampling(bool val);
	bool getAudioUpsampling();

	// Voice guidance prompts
	void setVoiceGuidance(bool val);
	bool getVoiceGuidance();

	// Auto power-off
	void setAutoPowerOff(AUTO_POWER_OFF val);
	AUTO_POWER_OFF getAutoPowerOff();

	// Capabilities reported by the device. 0 until the reply arrives.
	unsigned int getCapabilities();
	// True if a feature is supported, or if capabilities haven't been read yet.
	bool supportsFeature(DEVICE_CAPABILITIES cap);

	bool isChanged();
	void setChanges();

	// Run the v1 connect handshake (init, then request the state we care about).
	// Replies arrive asynchronously and are applied via setStateFromReply().
	void queryState();

	// Apply an incoming DATA frame to our state. Called from the Listener thread.
	void setStateFromReply(const CommandSerializer::Message& msg);

	void requestBattery();      // sends the GET; the reply updates state asynchronously
	int getBatteryLevel();      // 0-100, or -1 if unknown
	bool isBatteryCharging();
private:
	void queryNcAsmState();
	void queryDeviceCapabilities();
	void queryDevices();
	void _connectDevice(int deviceIdx);
	void _disconnectDevice(int deviceIdx);

	int _batteryLevel = -1;
	bool _batteryCharging = false;

	unsigned int _capabilities = 0;

	Property<bool> _ambientSoundControl = { 0 };
	Property<bool> _focusOnVoice = { 0 };
	Property<int> _asmLevel = { 0 };
	// Starts fulfilled (OFF/OFF) so connecting doesn't push a spurious "position off" write;
	// the actual device state is unknown and we only send when the user changes something.
	Property<SOUND_POSITION_PRESET> _surroundPosition = { SOUND_POSITION_PRESET::OFF, SOUND_POSITION_PRESET::OFF };
	Property<int> _vptType = { 0 };

	Property<OPTIMIZER_STATE> _optimizerState = { OPTIMIZER_STATE::IDLE, OPTIMIZER_STATE::IDLE };
	Property<S2C_TOGGLE> _speakToChat = { S2C_TOGGLE::INACTIVE, S2C_TOGGLE::INACTIVE };
	Property<unsigned int> _s2cOptions = { 0 };

	// index 0 is the empty "no device" slot; _dev1/_dev2 index into _savedDevices
	std::vector<BluetoothDevice> _savedDevices = { BluetoothDevice{"", ""} };
	Property<int> _dev1 = { 0, 0 }, _dev2 = { 0, 0 };

	Property<EqualizerConfig> _eq;

	// Write-only system settings (no read-back, to avoid clobbering the device on connect).
	Property<bool> _audioUpsampling = { false, false };
	Property<bool> _voiceGuidance = { false, false };
	Property<AUTO_POWER_OFF> _autoPowerOff = { AUTO_POWER_OFF::OFF, AUTO_POWER_OFF::OFF };

	std::mutex _propertyMtx;

	BluetoothWrapper& _conn;
};

template<class T>
inline void Property<T>::fulfill()
{
	this->current = this->desired;
}

template<class T>
inline bool Property<T>::isFulfilled()
{
	return this->desired == this->current;
}

template<class T>
inline void Property<T>::setState(T val)
{
	this->current = val;
	this->desired = val;
}
