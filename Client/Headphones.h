#pragma once

#include "BluetoothWrapper.h"
#include "Constants.h"

#include <bitset>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

// What we actually know about one device setting.
//
//   Unknown       we have never established the device's value (and never pushed one)
//   Requested     the user asked for a value that has not reached the transport yet
//   Acknowledged  the headphones ACKed the frame that carried the value (transport level only)
//   Confirmed     the headphones reported this value in a RET/NOTIFY reply
//   Failed        the send did not complete (ACK timeout, socket error)
//
// Acknowledged is deliberately distinct from Confirmed: an ACK says "frame received", not
// "setting applied". Only a read-back reply may promote a setting to Confirmed.
enum class SettingStatus
{
	Unknown,
	Requested,
	Acknowledged,
	Confirmed,
	Failed
};

// One device setting: what the device is believed to hold, what the UI wants, and enough
// bookkeeping to tell "already sent" from "changed again while we were sending".
//
// revision counts intent changes; sentRevision records the revision that was last handed to
// the transport. isFulfilled() is revision == sentRevision, so an ACK for revision N can
// never mark revision N+1 as sent - the stale-fulfillment bug the old value comparison had.
//
// current is the best belief about the device: what it last reported, or what it last ACKed.
// reported is narrower - only what the device itself said in a RET/NTFY this session - and is
// empty until it says something. An ACK never fills it: an ACK means the frame arrived, not that
// the setting changed.
//
// Not thread-safe on its own: Headphones owns the mutex that guards every Property.
template <class T>
struct Property {
	T current{};
	T desired{};
	std::optional<T> reported;
	SettingStatus status = SettingStatus::Unknown;
	unsigned long long revision = 0;
	unsigned long long sentRevision = 0;

	// Ask for a value. Re-selecting the value we already pushed is a no-op, but re-selecting
	// the local default while the device value is still Unknown does queue a command:
	// otherwise "set it to the value it appears to have" would silently send nothing.
	void setDesired(const T& val);

	// True when there is nothing left to hand to the transport.
	bool isFulfilled() const { return this->revision == this->sentRevision; }

	// The transport accepted (ACKed) the frame carrying revision rev. Never downgrades a
	// value the device has already reported back (a read-back can beat its own ACK).
	void markSent(unsigned long long rev, const T& sent);
	// The frame carrying revision rev never completed. Like markSent, it does not overwrite
	// a device read-back that already confirmed the same value.
	void markFailed(unsigned long long rev);
	// Like markSent, but for values the device owns (multipoint): our desired is only a
	// request, so it reverts and we wait for the device's own list reply.
	void markRequestSent(unsigned long long rev);

	// A device reply told us the real value. Only promotes to Confirmed when the reply
	// actually answers the latest intent - a reply describing the value the device still
	// holds must not hide a newer request that has not been sent yet.
	void setState(const T& val);
	// Forget everything (new session).
	void reset(const T& val);
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

// A read-only copy of every setting's status, taken under one lock so the UI never sees a
// half-updated picture.
struct SettingsStatus {
	SettingStatus ambient = SettingStatus::Unknown;
	SettingStatus equalizer = SettingStatus::Unknown;
	SettingStatus spatial = SettingStatus::Unknown;
	SettingStatus speakToChat = SettingStatus::Unknown;
	SettingStatus dsee = SettingStatus::Unknown;
	SettingStatus voiceGuidance = SettingStatus::Unknown;
	SettingStatus autoPowerOff = SettingStatus::Unknown;
	SettingStatus optimizer = SettingStatus::Unknown;
	SettingStatus multipoint = SettingStatus::Unknown;
};

// Whether the device said it supports a feature. Unknown is not "supported": it means the
// capability reply has not arrived (or the device never answers it).
enum class CapabilityState
{
	Unknown,
	Supported,
	Unsupported
};

// Whether a supported feature can be used right now, as the device reports it. Independent of
// its value: a feature can be on and temporarily unavailable, or available and off.
enum class Availability
{
	Unknown,
	Available,
	Unavailable
};

// A read-only copy of one discovered on/off control (touch panel, multipoint mode, wearing
// control). Unknown, unsupported, unavailable and off are four different answers.
struct ToggleSnapshot
{
	CapabilityState support = CapabilityState::Unknown;
	// The general-setting slot (D1-D3) the device negotiated for it, or the system selector.
	// 0 until discovery has resolved it.
	unsigned char selector = 0;
	Availability availability = Availability::Unknown;
	bool valueKnown = false;   // the device has reported the value this session
	bool value = false;        // what the device last reported (meaningful when valueKnown)
	bool requested = false;    // what the app last asked for
	SettingStatus status = SettingStatus::Unknown;
};

// Thread-safety: every accessor takes _propertyMtx. setChanges() snapshots under the lock,
// releases it while the frame is on the wire, then re-takes it to record the result - the
// Listener thread must be able to apply incoming replies while a send is in flight.
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

	// Capabilities reported by the device.
	unsigned int getCapabilities();
	// True once a valid supported-functions reply (07) has arrived this session. Tracked apart
	// from the function set itself: "the device listed nothing" and "the device has not
	// answered" are different facts.
	bool capabilitiesKnown();
	// Unknown / Supported / Unsupported - the honest answer.
	CapabilityState featureState(DEVICE_CAPABILITIES cap);
	// The same question for any advertised function ID.
	CapabilityState functionState(FUNCTION_TYPE fn);
	// Convenience for UI gating: show a control unless the device said it is unsupported.
	// Unknown counts as "show it": which V1 devices answer the capability query has not been
	// established on hardware. Use featureState() when the distinction matters.
	bool supportsFeature(DEVICE_CAPABILITIES cap);

	// Discovered controls. Support comes from the capability title/type the device reports
	// (never from an assumed slot), availability and value from their own replies.
	ToggleSnapshot getTouchPanel();
	ToggleSnapshot getMultipointMode();
	ToggleSnapshot getWearingControl();

	// Per-setting status for the UI (see SettingStatus).
	SettingsStatus getStatuses();

	bool isChanged();
	void setChanges();

	// Run the v1 connect handshake (init, then request the state we care about).
	// Replies arrive asynchronously and are applied via setStateFromReply().
	void queryState();

	// Follow-up requests that replies made necessary (capability details, availability and value
	// reads). The Listener only ever enqueues them: it must stay free to read the ACKs those
	// requests wait for. The command worker drains the queue.
	bool hasPendingQueries();
	void sendPendingQueries();

	// Apply an incoming DATA frame to our state. Called from the Listener thread. Dispatches on
	// (table, command, selector); a frame that fails its decoder changes nothing.
	void setStateFromReply(const CommandSerializer::Message& msg);

	void requestBattery();      // sends the GET; the reply updates state asynchronously
	int getBatteryLevel();      // 0-100, or -1 if unknown
	bool isBatteryCharging();

	// Drop every value back to Unknown. Called when a session ends so a reconnect cannot
	// present the previous device's settings as this device's settings.
	void resetState();

private:
	// A control the device has to tell us about before we may touch it.
	struct DiscoveredToggle
	{
		CapabilityState support = CapabilityState::Unknown;
		unsigned char selector = 0;
		Availability availability = Availability::Unknown;
		Property<bool> value;

		void reset();
		ToggleSnapshot snapshot() const;
	};

	struct PendingQuery
	{
		Buffer payload;
		DATA_TYPE dataType = DATA_TYPE::DATA_MDR;
		unsigned long long generation = 0;
	};

	void queryNcAsmState();
	void queryDeviceCapabilities();
	void queryDevices();
	void _connectDevice(int deviceIdx);
	void _disconnectDevice(int deviceIdx);

	// Reply handlers, one per table. Each decodes first and only then takes the lock.
	void _applyTable1(const CommandSerializer::Message& msg);
	void _applyTable2(const CommandSerializer::Message& msg);
	void _applySupportedFunctions(const std::vector<unsigned char>& ids);
	void _applyGeneralSettingCapability(const CommandSerializer::GeneralSettingCapability& cap);
	void _applyWearingCapability(unsigned char settingType);

	// Called with _propertyMtx held.
	void _enqueueQueryLocked(Buffer payload, DATA_TYPE dataType = DATA_TYPE::DATA_MDR);
	bool _advertisedLocked(FUNCTION_TYPE fn) const;
	DiscoveredToggle* _generalSettingForSlotLocked(unsigned char slot);
	void _settleUndiscoveredGeneralSettingsLocked();

	// Snapshot prop under the lock, send without it, then record the outcome against the
	// revision that was actually sent.
	template <class T, class SendFn>
	void _sendPending(Property<T>& prop, SendFn&& send);

	// Groups that share one frame, so all their properties are snapshotted together.
	void _pushNcAsm();
	void _pushSpatial();
	void _pushMultipoint();

	int _batteryLevel = -1;
	bool _batteryCharging = false;

	// Session state from capability discovery. resetState() forgets all of it, so a reconnect
	// rediscovers instead of trusting what a previous connection - or device - advertised.
	unsigned int _capabilities = 0;           // legacy DEVICE_CAPABILITIES view of _functions
	bool _capabilitiesKnown = false;          // a valid 07 reply arrived this session
	std::bitset<256> _functions;              // advertised function IDs
	std::bitset<3> _slotsAwaitingCapability;  // advertised D1-D3 slots whose D1 reply is outstanding

	DiscoveredToggle _touchPanel;
	DiscoveredToggle _multipointMode;
	DiscoveredToggle _wearingControl;

	std::deque<PendingQuery> _queries;
	// Bumped by resetState(). A query queued under one session is never sent in the next.
	unsigned long long _generation = 0;

	Property<bool> _ambientSoundControl;
	Property<bool> _focusOnVoice;
	Property<int> _asmLevel;
	Property<SOUND_POSITION_PRESET> _surroundPosition{ SOUND_POSITION_PRESET::OFF, SOUND_POSITION_PRESET::OFF };
	Property<int> _vptType;

	Property<OPTIMIZER_STATE> _optimizerState{ OPTIMIZER_STATE::IDLE, OPTIMIZER_STATE::IDLE };
	Property<S2C_TOGGLE> _speakToChat{ S2C_TOGGLE::INACTIVE, S2C_TOGGLE::INACTIVE };
	Property<unsigned int> _s2cOptions;

	// index 0 is the empty "no device" slot; _dev1/_dev2 index into _savedDevices
	std::vector<BluetoothDevice> _savedDevices = { BluetoothDevice{"", ""} };
	Property<int> _dev1, _dev2;

	Property<EqualizerConfig> _eq;

	// Write-only system settings: no read-back exists, so they stay Acknowledged at best.
	Property<bool> _audioUpsampling;
	Property<bool> _voiceGuidance;
	Property<AUTO_POWER_OFF> _autoPowerOff{ AUTO_POWER_OFF::OFF, AUTO_POWER_OFF::OFF };

	std::mutex _propertyMtx;

	BluetoothWrapper& _conn;
};

template<class T>
inline void Property<T>::setDesired(const T& val)
{
	const bool pending = !this->isFulfilled();
	const bool sameIntent = (val == this->desired);
	const bool deviceValueEstablished =
		this->status == SettingStatus::Acknowledged || this->status == SettingStatus::Confirmed;

	// Nothing to do only when the same value is already queued, or has already been pushed to
	// a device whose value we have actually established.
	if (sameIntent && (pending || deviceValueEstablished))
		return;

	this->desired = val;
	++this->revision;
	this->status = SettingStatus::Requested;
}

template<class T>
inline void Property<T>::markSent(unsigned long long rev, const T& sent)
{
	if (rev > this->sentRevision)
		this->sentRevision = rev;

	// A read-back can beat its own ACK: the device may report the new value before the
	// transport-level acknowledgement of the frame that carried it lands. "The device told us
	// it holds this" is strictly stronger evidence than "the device received the frame", so
	// an ACK never walks a matching Confirmed back down to Acknowledged.
	const bool alreadyConfirmed =
		this->status == SettingStatus::Confirmed && this->current == sent;

	this->current = sent;
	// Only claim "sent" when nothing newer has been requested in the meantime.
	if (rev == this->revision && !alreadyConfirmed)
		this->status = SettingStatus::Acknowledged;
}

template<class T>
inline void Property<T>::markFailed(unsigned long long rev)
{
	// Stop retrying this revision; a fresh user action bumps revision and tries again.
	if (rev > this->sentRevision)
		this->sentRevision = rev;

	// Same rule as markSent: if the device already reported back the value we were sending,
	// it applied the change and only the acknowledgement went missing. Reporting that as
	// Failed would be as untrue as reporting an unsent change as Confirmed.
	const bool deviceAlreadyConfirmedIt =
		this->status == SettingStatus::Confirmed && this->current == this->desired;

	if (rev == this->revision && !deviceAlreadyConfirmedIt)
		this->status = SettingStatus::Failed;
}

template<class T>
inline void Property<T>::markRequestSent(unsigned long long rev)
{
	if (rev > this->sentRevision)
		this->sentRevision = rev;
	if (rev == this->revision)
	{
		this->desired = this->current;
		this->status = SettingStatus::Requested;
	}
}

template<class T>
inline void Property<T>::setState(const T& val)
{
	const bool answersLatestIntent = this->isFulfilled() || val == this->desired;

	this->current = val;
	this->reported = val;

	// Don't discard a change the user has queued but we haven't sent yet.
	if (this->isFulfilled())
		this->desired = val;

	// Confirmed means "the device holds what the app is showing". While a newer request is
	// still outstanding, a reply describing the value the device *currently* holds says
	// nothing about that request - claiming Confirmed there would show a settled setting the
	// user is still waiting on. The pending Requested status stands until the request itself
	// is answered. (val == desired is the read-back-beats-its-own-ACK case: the reply does
	// answer the outstanding intent, even though the ACK has not landed yet.)
	if (answersLatestIntent)
		this->status = SettingStatus::Confirmed;
}

template<class T>
inline void Property<T>::reset(const T& val)
{
	this->current = val;
	this->desired = val;
	this->reported.reset();
	this->status = SettingStatus::Unknown;
	this->revision = 0;
	this->sentRevision = 0;
}

template<class T, class SendFn>
inline void Headphones::_sendPending(Property<T>& prop, SendFn&& send)
{
	T value;
	unsigned long long rev;
	{
		std::lock_guard guard(this->_propertyMtx);
		if (prop.isFulfilled())
			return;
		value = prop.desired;
		rev = prop.revision;
	}

	try
	{
		send(value);
	}
	catch (...)
	{
		std::lock_guard guard(this->_propertyMtx);
		prop.markFailed(rev);
		throw;
	}

	std::lock_guard guard(this->_propertyMtx);
	prop.markSent(rev, value);
}
