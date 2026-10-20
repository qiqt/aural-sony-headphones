#include "Headphones.h"
#include "CommandSerializer.h"

#include <memory>
#include <stdexcept>

Headphones::Headphones(BluetoothWrapper& conn) : _conn(conn)
{
}

void Headphones::setAmbientSoundControl(bool val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_ambientSoundControl.setDesired(val);
}

bool Headphones::getAmbientSoundControl()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_ambientSoundControl.current;
}

bool Headphones::isFocusOnVoiceAvailable()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_ambientSoundControl.current && this->_asmLevel.current > MINIMUM_VOICE_FOCUS_STEP;
}

void Headphones::setFocusOnVoice(bool val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_focusOnVoice.setDesired(val);
}

bool Headphones::getFocusOnVoice()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_focusOnVoice.current;
}

bool Headphones::isSetAsmLevelAvailable()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_ambientSoundControl.current;
}

void Headphones::setAsmLevel(int val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_asmLevel.setDesired(val);
}

int Headphones::getAsmLevel()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_asmLevel.current;
}

void Headphones::setSurroundPosition(SOUND_POSITION_PRESET val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_surroundPosition.setDesired(val);
}

SOUND_POSITION_PRESET Headphones::getSurroundPosition()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_surroundPosition.current;
}

void Headphones::setVptType(int val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_vptType.setDesired(val);
}

int Headphones::getVptType()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_vptType.current;
}

void Headphones::setOptimizerState(OPTIMIZER_STATE val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_optimizerState.setDesired(val);
}

OPTIMIZER_STATE Headphones::getOptimizerState()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_optimizerState.current;
}

void Headphones::setS2CToggle(S2C_TOGGLE val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_speakToChat.setDesired(val);
}

void Headphones::setS2COptions(int sensitivity, bool voice, int offTime)
{
	unsigned int sens = static_cast<unsigned int>(sensitivity);
	unsigned int time = static_cast<unsigned int>(offTime);
	std::lock_guard guard(this->_propertyMtx);
	this->_s2cOptions.setDesired((sens << 16) | (static_cast<unsigned int>(voice) << 8) | time);
}

S2C_TOGGLE Headphones::getS2CToggle()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_speakToChat.current;
}

unsigned int Headphones::getS2COptions()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_s2cOptions.current;
}

std::vector<BluetoothDevice> Headphones::getDevices()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_savedDevices;
}

std::pair<int, int> Headphones::getConnectedDevices()
{
	std::lock_guard guard(this->_propertyMtx);
	return { this->_dev1.current, this->_dev2.current };
}

void Headphones::setMultiPointConnection(int connectionId, int newDevice, int oldDevice)
{
	std::lock_guard guard(this->_propertyMtx);
	if (connectionId == 1)
		this->_dev1.setDesired(newDevice);
	else
		this->_dev2.setDesired(newDevice);
}

unsigned int Headphones::getCapabilities()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_capabilities;
}

bool Headphones::capabilitiesKnown()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_capabilitiesKnown;
}

CapabilityState Headphones::featureState(DEVICE_CAPABILITIES cap)
{
	std::lock_guard guard(this->_propertyMtx);
	if (!this->_capabilitiesKnown)
		return CapabilityState::Unknown;
	return (this->_capabilities & cap) ? CapabilityState::Supported : CapabilityState::Unsupported;
}

bool Headphones::supportsFeature(DEVICE_CAPABILITIES cap)
{
	return this->featureState(cap) != CapabilityState::Unsupported;
}

SettingsStatus Headphones::getStatuses()
{
	std::lock_guard guard(this->_propertyMtx);

	// A group is only as settled as its least settled member.
	auto worst = [](SettingStatus a, SettingStatus b) {
		auto rank = [](SettingStatus s) {
			switch (s)
			{
			case SettingStatus::Failed:       return 0;
			case SettingStatus::Unknown:      return 1;
			case SettingStatus::Requested:    return 2;
			case SettingStatus::Acknowledged: return 3;
			case SettingStatus::Confirmed:    return 4;
			}
			return 1;
		};
		return rank(a) <= rank(b) ? a : b;
	};

	SettingsStatus s;
	s.ambient = worst(worst(this->_ambientSoundControl.status, this->_asmLevel.status), this->_focusOnVoice.status);
	s.equalizer = this->_eq.status;
	s.spatial = worst(this->_surroundPosition.status, this->_vptType.status);
	s.speakToChat = worst(this->_speakToChat.status, this->_s2cOptions.status);
	s.dsee = this->_audioUpsampling.status;
	s.voiceGuidance = this->_voiceGuidance.status;
	s.autoPowerOff = this->_autoPowerOff.status;
	s.optimizer = this->_optimizerState.status;
	s.multipoint = worst(this->_dev1.status, this->_dev2.status);
	return s;
}

void Headphones::setEqualizer(const EqualizerConfig& cfg)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_eq.setDesired(cfg);
}

EqualizerConfig Headphones::getEqualizer()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_eq.current;
}

void Headphones::setAudioUpsampling(bool val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_audioUpsampling.setDesired(val);
}

bool Headphones::getAudioUpsampling()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_audioUpsampling.current;
}

void Headphones::setVoiceGuidance(bool val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_voiceGuidance.setDesired(val);
}

bool Headphones::getVoiceGuidance()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_voiceGuidance.current;
}

void Headphones::setAutoPowerOff(AUTO_POWER_OFF val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_autoPowerOff.setDesired(val);
}

AUTO_POWER_OFF Headphones::getAutoPowerOff()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_autoPowerOff.current;
}

bool Headphones::isChanged()
{
	std::lock_guard guard(this->_propertyMtx);
	return !(this->_ambientSoundControl.isFulfilled() &&
		this->_asmLevel.isFulfilled() &&
		this->_focusOnVoice.isFulfilled() &&
		this->_surroundPosition.isFulfilled() &&
		this->_vptType.isFulfilled() &&
		this->_optimizerState.isFulfilled() &&
		this->_speakToChat.isFulfilled() &&
		this->_s2cOptions.isFulfilled() &&
		this->_dev1.isFulfilled() &&
		this->_dev2.isFulfilled() &&
		this->_eq.isFulfilled() &&
		this->_audioUpsampling.isFulfilled() &&
		this->_voiceGuidance.isFulfilled() &&
		this->_autoPowerOff.isFulfilled());
}

void Headphones::setChanges()
{
	// One pass over every pending setting. Each group snapshots its value + revision under
	// the lock, sends with the lock released (the Listener needs it to apply replies), then
	// records the result against the revision that actually went out. A value changed while
	// a frame was in flight stays pending and is sent on the next pass.
	//
	// A recoverable error that does not kill the link (an ACK timeout) does not abort the
	// rest of the batch; it is remembered and rethrown once every group has had a turn.
	std::unique_ptr<RecoverableException> firstError;
	auto run = [&](auto&& fn) {
		try
		{
			fn();
		}
		catch (const RecoverableException& exc)
		{
			if (exc.shouldDisconnect)
				throw;
			if (!firstError)
				firstError = std::make_unique<RecoverableException>(exc);
		}
	};

	run([&] {
		this->_sendPending(this->_optimizerState, [&](OPTIMIZER_STATE val) {
			this->_conn.sendCommand(CommandSerializer::serializeXM4OptimizeCommand(val));
		});
	});

	run([&] {
		this->_sendPending(this->_speakToChat, [&](S2C_TOGGLE val) {
			this->_conn.sendCommand(CommandSerializer::serializeXM4SpeakToChat(val));
		});
	});

	run([&] {
		this->_sendPending(this->_s2cOptions, [&](unsigned int opts) {
			unsigned char sensitivity = static_cast<unsigned char>((opts >> 16) & 0xff);
			unsigned char voice = static_cast<unsigned char>((opts >> 8) & 0xff);
			unsigned char offTime = static_cast<unsigned char>(opts & 0xff);
			this->_conn.sendCommand(CommandSerializer::serializeXM4_S2C_Options(sensitivity, voice, offTime));
		});
	});

	run([&] {
		this->_sendPending(this->_eq, [&](const EqualizerConfig& cfg) {
			// MANUAL means "apply these band sliders"; every other id just selects the preset.
			if (cfg.preset == static_cast<int>(EQ_PRESET::MANUAL))
				this->_conn.sendCommand(CommandSerializer::serializeEqualizerCustomBands(cfg.clearBass, cfg.bands));
			else
				this->_conn.sendCommand(CommandSerializer::serializeEqualizerPreset(static_cast<unsigned char>(cfg.preset)));
		});
	});

	run([&] {
		this->_sendPending(this->_audioUpsampling, [&](bool val) {
			this->_conn.sendCommand(CommandSerializer::serializeAudioUpsampling(val));
		});
	});

	run([&] {
		this->_sendPending(this->_voiceGuidance, [&](bool val) {
			// Voice guidance lives in the COMMAND_2 namespace, so it goes out as DATA_MDR_NO2.
			this->_conn.sendCommand(CommandSerializer::serializeVoiceGuidance(val), DATA_TYPE::DATA_MDR_NO2);
		});
	});

	run([&] {
		this->_sendPending(this->_autoPowerOff, [&](AUTO_POWER_OFF val) {
			this->_conn.sendCommand(CommandSerializer::serializeAutoPowerOff(val));
		});
	});

	run([&] { this->_pushNcAsm(); });
	run([&] { this->_pushSpatial(); });
	run([&] { this->_pushMultipoint(); });

	if (firstError)
		throw RecoverableException(*firstError);
}

// NC/ambient is one frame carrying three properties, so all three are snapshotted together.
void Headphones::_pushNcAsm()
{
	bool ambient = false, voice = false;
	int level = 0;
	unsigned long long ambientRev = 0, voiceRev = 0, levelRev = 0;
	{
		std::lock_guard guard(this->_propertyMtx);
		if (this->_ambientSoundControl.isFulfilled() && this->_focusOnVoice.isFulfilled() && this->_asmLevel.isFulfilled())
			return;
		ambient = this->_ambientSoundControl.desired;
		voice = this->_focusOnVoice.desired;
		level = this->_asmLevel.desired;
		ambientRev = this->_ambientSoundControl.revision;
		voiceRev = this->_focusOnVoice.revision;
		levelRev = this->_asmLevel.revision;
	}

	auto ncAsmEffect = ambient ? NC_ASM_EFFECT::ADJUSTMENT_COMPLETION : NC_ASM_EFFECT::OFF;
	auto asmId = voice ? ASM_ID::VOICE : ASM_ID::NORMAL;
	auto asmLevel = ambient ? level : static_cast<int>(ASM_LEVEL_DISABLED);

	try
	{
		this->_conn.sendCommand(CommandSerializer::serializeNcAndAsmSetting(
			ncAsmEffect,
			NC_ASM_SETTING_TYPE::LEVEL_ADJUSTMENT,
			ASM_SETTING_TYPE::LEVEL_ADJUSTMENT,
			asmId,
			static_cast<char>(asmLevel)
		));
	}
	catch (...)
	{
		std::lock_guard guard(this->_propertyMtx);
		this->_ambientSoundControl.markFailed(ambientRev);
		this->_focusOnVoice.markFailed(voiceRev);
		this->_asmLevel.markFailed(levelRev);
		throw;
	}

	std::lock_guard guard(this->_propertyMtx);
	this->_ambientSoundControl.markSent(ambientRev, ambient);
	this->_focusOnVoice.markSent(voiceRev, voice);
	this->_asmLevel.markSent(levelRev, level);
}

// VPT and sound position are mutually exclusive and share one frame.
void Headphones::_pushSpatial()
{
	int vpt = 0;
	SOUND_POSITION_PRESET position = SOUND_POSITION_PRESET::OFF;
	SOUND_POSITION_PRESET currentPosition = SOUND_POSITION_PRESET::OFF;
	int currentVpt = 0;
	unsigned long long vptRev = 0, positionRev = 0;
	{
		std::lock_guard guard(this->_propertyMtx);
		if (this->_vptType.isFulfilled() && this->_surroundPosition.isFulfilled())
			return;
		vpt = this->_vptType.desired;
		position = this->_surroundPosition.desired;
		currentVpt = this->_vptType.current;
		currentPosition = this->_surroundPosition.current;
		vptRev = this->_vptType.revision;
		positionRev = this->_surroundPosition.revision;
	}

	VPT_INQUIRED_TYPE command;
	unsigned char preset;

	if (vpt != 0)
	{
		command = VPT_INQUIRED_TYPE::VPT;
		preset = static_cast<unsigned char>(vpt);
	}
	else if (position != SOUND_POSITION_PRESET::OFF)
	{
		command = VPT_INQUIRED_TYPE::SOUND_POSITION;
		preset = static_cast<unsigned char>(position);
	}
	else if (currentVpt != 0)
	{
		// Turning VPT off explicitly; SOUND_POSITION OFF also clears VPT on the devices we
		// know about, but say what we mean.
		command = VPT_INQUIRED_TYPE::VPT;
		preset = 0;
	}
	else
	{
		// Both requested off. Also the "device value is unknown and the user picked Off"
		// case, which the old code treated as impossible and threw on.
		command = VPT_INQUIRED_TYPE::SOUND_POSITION;
		preset = static_cast<unsigned char>(SOUND_POSITION_PRESET::OFF);
		(void)currentPosition;
	}

	try
	{
		this->_conn.sendCommand(CommandSerializer::serializeVPTSetting(command, preset));
	}
	catch (...)
	{
		std::lock_guard guard(this->_propertyMtx);
		this->_vptType.markFailed(vptRev);
		this->_surroundPosition.markFailed(positionRev);
		throw;
	}

	std::lock_guard guard(this->_propertyMtx);
	this->_vptType.markSent(vptRev, vpt);
	this->_surroundPosition.markSent(positionRev, position);
}

// Multipoint connect/disconnect is a *request*: the connected set comes back from the device,
// so the desired value reverts and setStateFromReply() supplies the truth.
void Headphones::_pushMultipoint()
{
	Property<int>* slots[2] = { &this->_dev1, &this->_dev2 };

	for (auto* slot : slots)
	{
		int desired = 0, current = 0;
		unsigned long long rev = 0;
		{
			std::lock_guard guard(this->_propertyMtx);
			if (slot->isFulfilled())
				continue;
			desired = slot->desired;
			current = slot->current;
			rev = slot->revision;
		}

		try
		{
			if (current != 0)
				this->_disconnectDevice(current);
			if (desired != 0)
				this->_connectDevice(desired);
		}
		catch (...)
		{
			std::lock_guard guard(this->_propertyMtx);
			slot->markFailed(rev);
			throw;
		}

		std::lock_guard guard(this->_propertyMtx);
		slot->markRequestSent(rev);
	}
}

void Headphones::queryState()
{
	// v1 connect handshake. Each send blocks only until it's ACKed; the actual replies
	// arrive asynchronously on the Listener thread and land in setStateFromReply().
	// Each query is independent so a request the device ignores doesn't block the others.
	try { this->_conn.sendCommand({ 0x00, 0x00 }); } catch (const RecoverableException&) {} // INIT_REQUEST
	try { this->requestBattery(); }                  catch (const RecoverableException&) {}
	try { this->queryDeviceCapabilities(); }         catch (const RecoverableException&) {}
	try { this->queryNcAsmState(); }                 catch (const RecoverableException&) {}
	try { this->queryDevices(); }                    catch (const RecoverableException&) {}
}

void Headphones::queryNcAsmState()
{
	this->_conn.sendCommand({
		static_cast<char>(COMMAND_TYPE::NCASM_GET_PARAM),
		static_cast<char>(NC_ASM_INQUIRED_TYPE::NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE)
	});
}

void Headphones::queryDeviceCapabilities()
{
	this->_conn.sendCommand({ static_cast<char>(COMMAND_TYPE::CAPABILITY_QUERY), 0x01 });
}

void Headphones::queryDevices()
{
	this->_conn.sendCommand(
		{ static_cast<char>(COMMAND_TYPE::MULTI_POINT_DEVICES_QUERY), 0x01 },
		DATA_TYPE::DATA_MDR_NO2);
}

void Headphones::_connectDevice(int deviceIdx)
{
	std::string mac;
	{
		std::lock_guard guard(this->_propertyMtx);
		if (deviceIdx <= 0 || deviceIdx >= static_cast<int>(this->_savedDevices.size()))
			return;
		mac = this->_savedDevices[deviceIdx].mac;
	}
	this->_conn.sendCommand(
		CommandSerializer::serializeMultiPointCommand(MULTI_POINT_COMMANDS::CONNECT, mac),
		DATA_TYPE::DATA_MDR_NO2);
}

void Headphones::_disconnectDevice(int deviceIdx)
{
	std::string mac;
	{
		std::lock_guard guard(this->_propertyMtx);
		if (deviceIdx <= 0 || deviceIdx >= static_cast<int>(this->_savedDevices.size()))
			return;
		mac = this->_savedDevices[deviceIdx].mac;
	}
	this->_conn.sendCommand(
		CommandSerializer::serializeMultiPointCommand(MULTI_POINT_COMMANDS::DISCONNECT, mac),
		DATA_TYPE::DATA_MDR_NO2);
}

void Headphones::requestBattery()
{
	// Ask for the battery level. The RET (and any later NOTIFY) is applied in setStateFromReply().
	this->_conn.sendCommand(CommandSerializer::serializeBatteryRequest());
}

void Headphones::setStateFromReply(const CommandSerializer::Message& msg)
{
	const Buffer& bytes = msg.messageBytes;
	if (bytes.empty())
		return;

	unsigned char cmd = static_cast<unsigned char>(bytes[0]);
	switch (cmd)
	{
	case BATTERY_LEVEL_RET:
	case BATTERY_LEVEL_NOTIFY:
	{
		// Payload: [cmd][batteryType][level 0-100][chargingStatus]
		if (bytes.size() >= 4)
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_batteryLevel = static_cast<unsigned char>(bytes[2]);
			this->_batteryCharging = bytes[3] != 0;
		}
		break;
	}

	case static_cast<unsigned char>(COMMAND_TYPE::NCASM_RET_PARAM):
	case static_cast<unsigned char>(COMMAND_TYPE::NCASM_NTFY_PARAM):
	{
		// Same 8-byte layout as SET: [cmd][inquiredType][effect][ncSettingType][dualSingle]
		// [asmSettingType][asmId][asmLevel]. Sent as RET on our GET and as NOTIFY when the
		// state changes on the device itself (e.g. the physical ANC button).
		if (bytes.size() >= 8 &&
			bytes[1] == static_cast<char>(NC_ASM_INQUIRED_TYPE::NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE))
		{
			bool effectOn = bytes[2] != static_cast<char>(NC_ASM_EFFECT::OFF);
			bool voice = bytes[6] == static_cast<char>(ASM_ID::VOICE);
			int level = static_cast<unsigned char>(bytes[7]);

			std::lock_guard guard(this->_propertyMtx);
			this->_ambientSoundControl.setState(effectOn);
			this->_focusOnVoice.setState(voice);
			this->_asmLevel.setState(level);
		}
		break;
	}

	case static_cast<unsigned char>(COMMAND_TYPE::XM4_OPTIMIZER_RESPONSE):
	{
		if (bytes.size() >= 3)
		{
			auto state = (bytes[2] != 0x00) ? OPTIMIZER_STATE::OPTIMIZING : OPTIMIZER_STATE::IDLE;
			std::lock_guard guard(this->_propertyMtx);
			this->_optimizerState.setState(state);
		}
		break;
	}

	case static_cast<unsigned char>(COMMAND_TYPE::CAPABILITY_QUERY_RESPONSE):
	{
		unsigned int caps = 0;
		for (char c : bytes)
		{
			switch (c)
			{
			case static_cast<char>(FUNCTION_TYPE::DEVICE_MANAGEMENT):   caps |= DEVICE_CAPABILITIES::MULTI_POINT;   break;
			case static_cast<char>(FUNCTION_TYPE::VPT):                 caps |= DEVICE_CAPABILITIES::VPT;           break;
			case static_cast<char>(FUNCTION_TYPE::OPTIMIZER):           caps |= DEVICE_CAPABILITIES::OPTIMIZER;     break;
			case static_cast<char>(FUNCTION_TYPE::NC_ASM):              caps |= DEVICE_CAPABILITIES::NC_ASM;        break;
			case static_cast<char>(FUNCTION_TYPE::SMART_TALKING_MODE):  caps |= DEVICE_CAPABILITIES::SPEAK_TO_CHAT; break;
			}
		}
		std::lock_guard guard(this->_propertyMtx);
		this->_capabilities = caps;
		// A reply that lists no known function is still a reply: the device answered, so we
		// know those features are absent rather than unread.
		this->_capabilitiesKnown = true;
		break;
	}

	case static_cast<unsigned char>(COMMAND_TYPE::DEVICES_QUERY_RESPONSE):
	case static_cast<unsigned char>(COMMAND_TYPE::DEVICES_STATE_RESPONSE):
	{
		// Payload: [cmd][queryType][numDevices]{ [17-byte MAC][slot][nameLen][name] }*
		if (bytes.size() < 3 || bytes[1] != 0x01)
			break;

		std::vector<BluetoothDevice> savedDevices = { BluetoothDevice{"", ""} };
		int dev1 = 0, dev2 = 0;
		size_t idx = 3;
		int numDevices = static_cast<unsigned char>(bytes[2]);
		bool malformed = false;

		for (; numDevices > 0 && !malformed; numDevices--)
		{
			if (idx + MAC_ADDR_STR_SIZE + 2 > bytes.size()) { malformed = true; break; }

			std::string mac(bytes.begin() + idx, bytes.begin() + idx + MAC_ADDR_STR_SIZE);
			idx += MAC_ADDR_STR_SIZE;

			unsigned char slot = static_cast<unsigned char>(bytes[idx++]);
			unsigned char nameLength = static_cast<unsigned char>(bytes[idx++]);
			if (idx + nameLength > bytes.size()) { malformed = true; break; }

			std::string name(bytes.begin() + idx, bytes.begin() + idx + nameLength);
			idx += nameLength;

			savedDevices.push_back(BluetoothDevice{ name, mac });
			if (slot == 0x01) dev1 = static_cast<int>(savedDevices.size()) - 1;
			if (slot == 0x02) dev2 = static_cast<int>(savedDevices.size()) - 1;
		}

		if (!malformed)
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_savedDevices = std::move(savedDevices);
			this->_dev1.setState(dev1);
			this->_dev2.setState(dev2);
		}
		break;
	}

	default:
		// Other replies (EQ, spatial, speak-to-chat, ...) have no read-back yet: those
		// settings stay Acknowledged rather than Confirmed. See SettingStatus.
		break;
	}
}

int Headphones::getBatteryLevel()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_batteryLevel;
}

bool Headphones::isBatteryCharging()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_batteryCharging;
}

void Headphones::resetState()
{
	std::lock_guard guard(this->_propertyMtx);

	this->_batteryLevel = -1;
	this->_batteryCharging = false;
	this->_capabilities = 0;
	this->_capabilitiesKnown = false;

	this->_ambientSoundControl.reset(false);
	this->_focusOnVoice.reset(false);
	this->_asmLevel.reset(0);
	this->_surroundPosition.reset(SOUND_POSITION_PRESET::OFF);
	this->_vptType.reset(0);
	this->_optimizerState.reset(OPTIMIZER_STATE::IDLE);
	this->_speakToChat.reset(S2C_TOGGLE::INACTIVE);
	this->_s2cOptions.reset(0);
	this->_eq.reset(EqualizerConfig{});
	this->_audioUpsampling.reset(false);
	this->_voiceGuidance.reset(false);
	this->_autoPowerOff.reset(AUTO_POWER_OFF::OFF);

	this->_savedDevices = { BluetoothDevice{"", ""} };
	this->_dev1.reset(0);
	this->_dev2.reset(0);
}
