#include "Headphones.h"
#include "CommandSerializer.h"

#include <stdexcept>

Headphones::Headphones(BluetoothWrapper& conn) : _conn(conn)
{
}

void Headphones::setAmbientSoundControl(bool value)
{
    std::scoped_lock lock(_propertyMtx);
    _ambientSoundControl.desired = value;
}

bool Headphones::getAmbientSoundControl()
{
    std::scoped_lock lock(_propertyMtx);
    return _ambientSoundControl.current;
}

bool Headphones::isFocusOnVoiceAvailable()
{
    std::scoped_lock lock(_propertyMtx);
    return _asmLevel.current > MINIMUM_VOICE_FOCUS_STEP && _ambientSoundControl.current;
}

void Headphones::setFocusOnVoice(bool value)
{
    std::scoped_lock lock(_propertyMtx);
    _focusOnVoice.desired = value;
}

bool Headphones::getFocusOnVoice()
{
    std::scoped_lock lock(_propertyMtx);
    return _focusOnVoice.current;
}

bool Headphones::isSetAsmLevelAvailable()
{
    return getAmbientSoundControl();
}

void Headphones::setAsmLevel(int value)
{
    std::scoped_lock lock(_propertyMtx);
    _asmLevel.desired = value;
}

int Headphones::getAsmLevel()
{
    std::scoped_lock lock(_propertyMtx);
    return _asmLevel.current;
}

void Headphones::setSurroundPosition(SOUND_POSITION_PRESET value)
{
    std::scoped_lock lock(_propertyMtx);
    _surroundPosition.desired = value;
}

SOUND_POSITION_PRESET Headphones::getSurroundPosition()
{
    std::scoped_lock lock(_propertyMtx);
    return _surroundPosition.current;
}

void Headphones::setVptType(int value)
{
    std::scoped_lock lock(_propertyMtx);
    _vptType.desired = value;
}

int Headphones::getVptType()
{
    std::scoped_lock lock(_propertyMtx);
    return _vptType.current;
}

void Headphones::setOptimizerState(OPTIMIZER_STATE val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_optimizerState.desired = val;
}

OPTIMIZER_STATE Headphones::getOptimizerState()
{
	return this->_optimizerState.current;
}

void Headphones::setS2CToggle(S2C_TOGGLE val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_speakToChat.desired = val;
}

void Headphones::setS2COptions(int sensitivity, bool voice, int offTime)
{
	unsigned int sens = static_cast<unsigned int>(sensitivity);
	unsigned int time = static_cast<unsigned int>(offTime);
	std::lock_guard guard(this->_propertyMtx);
	this->_s2cOptions.desired = (sens << 16) | (static_cast<unsigned int>(voice) << 8) | time;
}

S2C_TOGGLE Headphones::getS2CToggle()
{
	return this->_speakToChat.current;
}

unsigned int Headphones::getS2COptions()
{
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
		this->_dev1.desired = newDevice;
	else
		this->_dev2.desired = newDevice;
}

unsigned int Headphones::getCapabilities()
{
	return this->_capabilities;
}

bool Headphones::supportsFeature(DEVICE_CAPABILITIES cap)
{
	// Show a feature when we haven't read capabilities yet, or when the device reports it.
	return this->_capabilities == 0 || (this->_capabilities & cap);
}

void Headphones::setEqualizer(const EqualizerConfig& cfg)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_eq.desired = cfg;
}

EqualizerConfig Headphones::getEqualizer()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_eq.current;
}

bool Headphones::isChanged()
{
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
		this->_eq.isFulfilled());
}

void Headphones::setChanges()
{
	if (!(this->_optimizerState.isFulfilled()))
	{
		this->_conn.sendCommand(CommandSerializer::serializeXM4OptimizeCommand(this->_optimizerState.desired));

		std::lock_guard guard(this->_propertyMtx);
		this->_optimizerState.fulfill();
	}

	if (!(this->_speakToChat.isFulfilled()))
	{
		this->_conn.sendCommand(CommandSerializer::serializeXM4SpeakToChat(this->_speakToChat.desired));

		std::lock_guard guard(this->_propertyMtx);
		this->_speakToChat.fulfill();
	}

	if (!(this->_s2cOptions.isFulfilled()))
	{
		auto s2cOptions = this->_s2cOptions.desired;
		unsigned char sensitivity = static_cast<unsigned char>((s2cOptions >> 16) & 0xff);
		unsigned char voice = static_cast<unsigned char>((s2cOptions >> 8) & 0xff);
		unsigned char offTime = static_cast<unsigned char>(s2cOptions & 0xff);

		this->_conn.sendCommand(CommandSerializer::serializeXM4_S2C_Options(sensitivity, voice, offTime));

		std::lock_guard guard(this->_propertyMtx);
		this->_s2cOptions.fulfill();
	}

	if (!(this->_eq.isFulfilled()))
	{
		auto cfg = this->_eq.desired;
		// MANUAL means "apply these band sliders"; every other id just selects the preset.
		if (cfg.preset == static_cast<int>(EQ_PRESET::MANUAL))
			this->_conn.sendCommand(CommandSerializer::serializeEqualizerCustomBands(cfg.clearBass, cfg.bands));
		else
			this->_conn.sendCommand(CommandSerializer::serializeEqualizerPreset(static_cast<unsigned char>(cfg.preset)));

		std::lock_guard guard(this->_propertyMtx);
		this->_eq.fulfill();
	}

    {
        bool enabled, voice, pending;
        int level;
        {
            std::lock_guard lock(_propertyMtx);
            pending = !_ambientSoundControl.isFulfilled() || !_focusOnVoice.isFulfilled() || !_asmLevel.isFulfilled();
            enabled = _ambientSoundControl.desired;
            voice = _focusOnVoice.desired;
            level = _asmLevel.desired;
        }
        if (pending)
        {
            _conn.sendCommand(CommandSerializer::serializeNcAndAsmSetting(
                enabled ? NC_ASM_EFFECT::ADJUSTMENT_COMPLETION : NC_ASM_EFFECT::OFF,
                NC_ASM_SETTING_TYPE::LEVEL_ADJUSTMENT, ASM_SETTING_TYPE::LEVEL_ADJUSTMENT,
                voice ? ASM_ID::VOICE : ASM_ID::NORMAL, static_cast<char>(enabled ? level : 0xff)));
            std::lock_guard lock(_propertyMtx);
            _ambientSoundControl.current = enabled;
            _focusOnVoice.current = voice;
            _asmLevel.current = level;
        }
    }

    {
        bool pending;
        int effect;
        SOUND_POSITION_PRESET position, previousPosition;
        {
            std::lock_guard lock(_propertyMtx);
            pending = !_vptType.isFulfilled() || !_surroundPosition.isFulfilled();
            effect = _vptType.desired;
            position = _surroundPosition.desired;
            previousPosition = _surroundPosition.current;
        }
        if (pending)
        {
            const bool usePosition = effect == 0 &&
                (position != SOUND_POSITION_PRESET::OFF || previousPosition != SOUND_POSITION_PRESET::OFF);
            _conn.sendCommand(CommandSerializer::serializeVPTSetting(
                usePosition ? VPT_INQUIRED_TYPE::SOUND_POSITION : VPT_INQUIRED_TYPE::VPT,
                static_cast<unsigned char>(usePosition ? static_cast<int>(position) : effect)));
            std::lock_guard lock(_propertyMtx);
            _vptType.current = effect;
            _surroundPosition.current = position;
        }
    }

	// Multipoint: a connect/disconnect is a *request*; the actual connected set comes back
	// via the device reply, so revert desired and let setStateFromReply update current.
	if (!(this->_dev1.isFulfilled()))
	{
		if (this->_dev1.current != 0)
			this->_disconnectDevice(this->_dev1.current);
		if (this->_dev1.desired != 0)
			this->_connectDevice(this->_dev1.desired);

		std::lock_guard guard(this->_propertyMtx);
		this->_dev1.desired = this->_dev1.current;
	}

	if (!(this->_dev2.isFulfilled()))
	{
		if (this->_dev2.current != 0)
			this->_disconnectDevice(this->_dev2.current);
		if (this->_dev2.desired != 0)
			this->_connectDevice(this->_dev2.desired);

		std::lock_guard guard(this->_propertyMtx);
		this->_dev2.desired = this->_dev2.current;
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
	try { this->queryDevices(); }                    catch (const RecoverableException&) {}
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
	if (deviceIdx <= 0 || deviceIdx >= static_cast<int>(this->_savedDevices.size()))
		return;
	this->_conn.sendCommand(
		CommandSerializer::serializeMultiPointCommand(MULTI_POINT_COMMANDS::CONNECT, this->_savedDevices[deviceIdx].mac),
		DATA_TYPE::DATA_MDR_NO2);
}

void Headphones::_disconnectDevice(int deviceIdx)
{
	if (deviceIdx <= 0 || deviceIdx >= static_cast<int>(this->_savedDevices.size()))
		return;
	this->_conn.sendCommand(
		CommandSerializer::serializeMultiPointCommand(MULTI_POINT_COMMANDS::DISCONNECT, this->_savedDevices[deviceIdx].mac),
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
		// Other replies (NC/ASM, EQ, ...) are handled in later phases.
		break;
	}
}

int Headphones::getBatteryLevel()
{
	return this->_batteryLevel;
}

bool Headphones::isBatteryCharging()
{
	return this->_batteryCharging;
}
