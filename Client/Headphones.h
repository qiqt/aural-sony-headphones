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
// Inside Headphones, preset is the EqPresetId byte (0-255): setEqualizer() converts the signed
// EQ_PRESET value callers pass, and a reported 0xFF is recorded as MANUAL (see _applyEqualizer).
struct EqualizerConfig {
	int preset = 0;                              // EQ_PRESET code
	int clearBass = 0;                           // -10..+10
	std::vector<int> bands = { 0, 0, 0, 0, 0 };  // -10..+10 each

	bool operator==(const EqualizerConfig& other) const {
		return preset == other.preset && clearBass == other.clearBass && bands == other.bands;
	}
};

// Auto power-off is two element IDs, not one mode: the active element (disabled, when removed,
// or a timer) and the selected timer, which the device keeps while another element is active.
// The defaults are the bytes the client always sent for "don't power off".
struct AutoPowerOffConfig {
	unsigned char active = v1::APO_ELEMENT_DISABLED;
	unsigned char selectedTime = v1::APO_ELEMENT_5_MIN;

	bool operator==(const AutoPowerOffConfig& other) const {
		return active == other.active && selectedTime == other.selectedTime;
	}
};

inline unsigned char autoPowerOffElement(AUTO_POWER_OFF mode)
{
	switch (mode)
	{
	case AUTO_POWER_OFF::AFTER_5_MIN:    return v1::APO_ELEMENT_5_MIN;
	case AUTO_POWER_OFF::AFTER_30_MIN:   return v1::APO_ELEMENT_30_MIN;
	case AUTO_POWER_OFF::AFTER_1_HOUR:   return v1::APO_ELEMENT_60_MIN;
	case AUTO_POWER_OFF::AFTER_3_HOUR:   return v1::APO_ELEMENT_180_MIN;
	case AUTO_POWER_OFF::WHEN_TAKEN_OFF: return v1::APO_ELEMENT_WHEN_REMOVED;
	case AUTO_POWER_OFF::OFF:            break;
	}
	return v1::APO_ELEMENT_DISABLED;
}

inline AUTO_POWER_OFF autoPowerOffMode(unsigned char element)
{
	switch (element)
	{
	case v1::APO_ELEMENT_5_MIN:        return AUTO_POWER_OFF::AFTER_5_MIN;
	case v1::APO_ELEMENT_30_MIN:       return AUTO_POWER_OFF::AFTER_30_MIN;
	case v1::APO_ELEMENT_60_MIN:       return AUTO_POWER_OFF::AFTER_1_HOUR;
	case v1::APO_ELEMENT_180_MIN:      return AUTO_POWER_OFF::AFTER_3_HOUR;
	case v1::APO_ELEMENT_WHEN_REMOVED: return AUTO_POWER_OFF::WHEN_TAKEN_OFF;
	default:                           break;
	}
	return AUTO_POWER_OFF::OFF;
}

// The three modes the UI offers, derived from a complete NC/ASM report.
enum class AmbientMode
{
	NoiseCancelling,
	AmbientSound,
	Off
};

// Reads the NC setting type and value, not only the effect and the ambient level: a device can
// report noise cancelling while still carrying the last ambient level. For the level-adjustment
// type - what this client writes - level 0 also means noise cancelling, the rule the hardware-
// verified NC/Ambient round trip relied on.
inline AmbientMode ambientModeOf(const CommandSerializer::NcAsmReport& r)
{
	if (r.effect == static_cast<unsigned char>(NC_ASM_EFFECT::OFF))
		return AmbientMode::Off;
	switch (static_cast<NC_ASM_SETTING_TYPE>(r.ncSettingType))
	{
	case NC_ASM_SETTING_TYPE::ON_OFF:
		return r.ncValue == 0x01 ? AmbientMode::NoiseCancelling : AmbientMode::AmbientSound;
	case NC_ASM_SETTING_TYPE::DUAL_SINGLE_OFF:
		return r.ncValue != static_cast<unsigned char>(NC_DUAL_SINGLE_VALUE::OFF)
			? AmbientMode::NoiseCancelling : AmbientMode::AmbientSound;
	case NC_ASM_SETTING_TYPE::LEVEL_ADJUSTMENT:
		// The client writes SINGLE for ambient level 1, so SINGLE alone is not noise cancelling.
		return (r.ncValue == static_cast<unsigned char>(NC_DUAL_SINGLE_VALUE::DUAL) || r.asmLevel == 0)
			? AmbientMode::NoiseCancelling : AmbientMode::AmbientSound;
	}
	return AmbientMode::AmbientSound;
}

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
	SettingStatus multipoint = SettingStatus::Unknown;        // paired-device connect/disconnect
	SettingStatus touchPanel = SettingStatus::Unknown;
	SettingStatus wearingControl = SettingStatus::Unknown;
	SettingStatus multipointMode = SettingStatus::Unknown;    // the multipoint on/off setting
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

// Where a multipoint-mode change stands this session. The device can ask the user to confirm the
// change (a fixed-message alert) and can drop the link to apply it, so a change passes through
// more steps than an ordinary write, and a report of the old value does not end it.
enum class MultipointStep
{
	None,        // nothing outstanding
	Requested,   // asked for; the write has not gone out
	Sent,        // the write is out; waiting for the device to report the value, ask, or drop the link
	Confirming,  // the device asked for confirmation; waiting for the user
	Answering,   // the user answered; the answer has not gone out
	Accepted     // the user accepted; waiting for the device to apply it (it may drop the link)
};

// A read-only copy of the multipoint-mode change in progress.
struct MultipointChange
{
	MultipointStep step = MultipointStep::None;
	bool requested = false;                                   // meaningful while step != None
	std::optional<CommandSerializer::MultipointAlert> alert;  // set while step == Confirming
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

	// The last complete NC/ASM report, every field kept. Empty until the device sends one.
	std::optional<CommandSerializer::NcAsmReport> getNcAsmReport();

	// Equalizer
	void setEqualizer(const EqualizerConfig& cfg);
	EqualizerConfig getEqualizer();
	// How many level bytes the device's last EQ report carried; -1 until it sends one. The
	// band editor only fits a device that reports clear bass + 5 bands (6).
	int getEqualizerLevelCount();

	// DSEE / DSEE Extreme upscaling
	void setAudioUpsampling(bool val);
	bool getAudioUpsampling();

	// Voice guidance prompts
	void setVoiceGuidance(bool val);
	bool getVoiceGuidance();

	// Auto power-off. Selecting a non-timer mode keeps the device's selected timer in the
	// second element instead of overwriting it with a fixed value.
	void setAutoPowerOff(AUTO_POWER_OFF val);
	AUTO_POWER_OFF getAutoPowerOff();
	AutoPowerOffConfig getAutoPowerOffConfig();
	// The active element the device itself last reported, if it has.
	std::optional<unsigned char> getReportedAutoPowerOffElement();

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
	// Every function ID the device advertised this session, ascending (empty until it answers).
	std::vector<unsigned char> supportedFunctions();
	// Convenience for UI gating: show a control unless the device said it is unsupported.
	// Unknown counts as "show it": which V1 devices answer the capability query has not been
	// established on hardware. Use featureState() when the distinction matters.
	bool supportsFeature(DEVICE_CAPABILITIES cap);

	// Discovered controls. Support comes from the capability title/type the device reports
	// (never from an assumed slot), availability and value from their own replies.
	ToggleSnapshot getTouchPanel();
	ToggleSnapshot getMultipointMode();
	ToggleSnapshot getWearingControl();

	// Ask for a discovered control to change. Refused (returns false, nothing queued) unless the
	// device has described the feature, reported it available, and reported its current value:
	// a write needs the negotiated slot/type, and "unknown" must not be mistaken for "off".
	// Touch panel enables the ear-cup touch controls; it does not remap gestures. Wearing
	// control lets the headset act on wearing detection; it is not a worn/not-worn sensor.
	bool setTouchPanel(bool enabled);
	bool setWearingControl(bool enabled);

	// Multipoint mode: the named general setting that lets the headset hold two connections at
	// once. Not the paired-device list (getDevices / setMultiPointConnection).
	//
	// A change can need the user's consent: the device may answer the write with a fixed-message
	// alert, and the change stays pending until the user has answered and the device reports the
	// requested value - possibly after dropping the link to apply it. The session then ends here;
	// the host reconnects and reads the value. One change at a time, and nothing is ever resent.
	//
	// installAlertHandling(): the host can show a multipoint alert and send the user's answer.
	// Until then the device's alerts are never enabled and multipoint changes are refused, since
	// a confirmation nobody sees could not be answered. Survives resetState().
	void installAlertHandling();
	// Refused unless alert handling is installed, the device has described the control, reports
	// it available and has reported its value, and no other change is outstanding. Asking for
	// the reported value before the write has gone out withdraws the change.
	bool setMultipointMode(bool enabled);
	MultipointChange getMultipointChange();
	// Answer the alert awaiting the user, quoting the message ID the device sent. Refused for any
	// other ID. The answer goes out on the command worker.
	bool answerMultipointAlert(unsigned char messageId, bool accept);
	// Stop waiting for a change the device has neither confirmed nor asked about (the host's
	// timeout). The value is read again; the change is not resent.
	void abandonMultipointChange();

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

	// Settings whose value can be read back. A read is only ever queued for a function the
	// device advertised this session.
	enum class ReadBack
	{
		Equalizer,
		Vpt,
		SoundPosition,
		SpeakToChat,
		SpeakToChatDetails,
		Dsee,
		VoiceGuidance,
		AutoPowerOff
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

	void _applyNcAsm(const CommandSerializer::NcAsmReport& report);
	void _applyEqualizer(const CommandSerializer::EqualizerReport& report);

	// Write a discovered toggle, then queue a read of it. The gate and the slot are re-checked
	// under the lock when the frame is built: a request the feature can no longer take fails
	// instead of going to a slot or state that has changed.
	void _pushToggle(DiscoveredToggle& toggle);

	// Send whichever multipoint frame is due: the write (preceded, once per session, by the alert
	// enable) or the user's answer to an alert.
	void _pushMultipointMode();
	void _applyMultipointAlert(const CommandSerializer::MultipointAlert& alert);
	// Called with _propertyMtx held, after the device reported the multipoint value.
	void _settleMultipointChangeLocked();
	SettingStatus _multipointModeStatusLocked() const;

	// Called with _propertyMtx held.
	bool _writableLocked(const DiscoveredToggle& toggle) const;
	void _enqueueQueryLocked(Buffer payload, DATA_TYPE dataType = DATA_TYPE::DATA_MDR);
	bool _advertisedLocked(FUNCTION_TYPE fn) const;
	DiscoveredToggle* _generalSettingForSlotLocked(unsigned char slot);
	void _settleUndiscoveredGeneralSettingsLocked();
	void _queueReadBackLocked(ReadBack which);
	// After a write: ask the device what it now holds. An ACK only says the frame arrived.
	void _queueReadBack(ReadBack which);

	// Snapshot prop under the lock, send without it, then record the outcome against the
	// revision that was actually sent. Returns true when a frame went out and was ACKed.
	template <class T, class SendFn>
	bool _sendPending(Property<T>& prop, SendFn&& send);

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

	// The multipoint change in progress. _multipointMode.value only ever mirrors device reports;
	// the request lives here so a report of the old value cannot settle it.
	bool _alertHandling = false;      // set by the host once; not session state
	bool _alertsEnabled = false;      // 94 01 00 ACKed this session
	MultipointStep _mpStep = MultipointStep::None;
	bool _mpRequested = false;
	bool _mpWriteInFlight = false;    // the write is on the wire, its ACK not yet in
	std::optional<CommandSerializer::MultipointAlert> _mpAlert;
	unsigned char _mpAnswerId = 0;
	bool _mpAccept = false;

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
	int _eqLevelCount = -1;

	// Last complete NC/ASM report; the three ambient properties hold its normalised form.
	std::optional<CommandSerializer::NcAsmReport> _ncAsmReport;

	// Read back when the device advertises them; Acknowledged at best when it does not.
	Property<bool> _audioUpsampling;
	Property<bool> _voiceGuidance;
	Property<AutoPowerOffConfig> _autoPowerOff;

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
inline bool Headphones::_sendPending(Property<T>& prop, SendFn&& send)
{
	T value;
	unsigned long long rev;
	{
		std::lock_guard guard(this->_propertyMtx);
		if (prop.isFulfilled())
			return false;
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
	return true;
}
