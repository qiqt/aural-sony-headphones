#include "PacketCodec.h"
#include <algorithm>
#include "CommandSerializer.h"


namespace
{
	unsigned char u8(const Buffer& bytes, size_t index)
	{
		return static_cast<unsigned char>(bytes[index]);
	}

	// Strict UTF-8 check (no overlongs, no surrogates, nothing above U+10FFFF). Capability
	// strings are UTF-8 byte strings; a record that is not valid UTF-8 is not a valid record.
	bool isValidUtf8(const Buffer& bytes, size_t begin, size_t end)
	{
		size_t i = begin;
		while (i < end)
		{
			const unsigned char lead = u8(bytes, i);
			size_t extra = 0;
			unsigned int cp = 0;
			if (lead < 0x80) { ++i; continue; }
			else if (lead >= 0xC2 && lead <= 0xDF) { extra = 1; cp = lead & 0x1F; }
			else if (lead >= 0xE0 && lead <= 0xEF) { extra = 2; cp = lead & 0x0F; }
			else if (lead >= 0xF0 && lead <= 0xF4) { extra = 3; cp = lead & 0x07; }
			else return false;

			// The lead byte plus its continuation bytes must all lie before end.
			if (end - i <= extra)
				return false;
			for (size_t k = 1; k <= extra; ++k)
			{
				const unsigned char cont = u8(bytes, i + k);
				if ((cont & 0xC0) != 0x80)
					return false;
				cp = (cp << 6) | (cont & 0x3F);
			}
			if ((extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
				(cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
				return false;
			i += extra + 1;
		}
		return true;
	}

	bool isTable1(const CommandSerializer::Message& msg) { return msg.dataType == DATA_TYPE::DATA_MDR; }
	bool isTable2(const CommandSerializer::Message& msg) { return msg.dataType == DATA_TYPE::DATA_MDR_NO2; }
}

namespace CommandSerializer
{
    Buffer _escapeSpecials(const Buffer& src) { return SonyFrame::escape(src); }

    Buffer _unescapeSpecials(const Buffer& src) { return SonyFrame::unescape(src); }

    unsigned char _sumChecksum(const char* src, size_t size) { return SonyFrame::checksum({src, size}); }

    unsigned char _sumChecksum(const Buffer& src) { return SonyFrame::checksum(src); }

    Buffer packageDataForBt(const Buffer& src, DATA_TYPE type, unsigned int sequence)
    {
        return SonyFrame::encode(src, type, static_cast<unsigned char>(sequence));
    }

    Message unpackBtMessage(const Buffer& src)
    {
        auto frame = SonyFrame::decode(src);
        return {frame.type, frame.sequence, std::move(frame.payload)};
    }

    NC_DUAL_SINGLE_VALUE getDualSingleForAsmLevel(char level)
    {
        // 0xff disables ambient processing; 0 and 1 select the two ANC modes.
        const auto value = static_cast<unsigned char>(level);
        if (value != 0xff && value > 19) throw std::invalid_argument("Ambient level must be 0-19 or disabled");
        switch (value)
        {
        case 0: return NC_DUAL_SINGLE_VALUE::DUAL;
        case 1: return NC_DUAL_SINGLE_VALUE::SINGLE;
        default: return NC_DUAL_SINGLE_VALUE::OFF;
        }
    }

    Buffer serializeNcAndAsmSetting(NC_ASM_EFFECT effect, NC_ASM_SETTING_TYPE ncType,
        ASM_SETTING_TYPE ambientType, ASM_ID focus, char level)
    {
        return {0x68, 0x02, static_cast<char>(effect), static_cast<char>(ncType),
            static_cast<char>(getDualSingleForAsmLevel(level)), static_cast<char>(ambientType),
            static_cast<char>(focus), level};
    }

    Buffer serializeVPTSetting(VPT_INQUIRED_TYPE type, unsigned char preset)
    {
        return {0x48, static_cast<char>(type), static_cast<char>(preset)};
    }

	Buffer serializeBatteryRequest()
	{
		Buffer ret;
		ret.push_back(BATTERY_LEVEL_GET); // BATTERY_LEVEL_REQUEST (v1)
		ret.push_back(0x00); // battery type: SINGLE
		return ret;
	}

    Buffer serializeXM4OptimizeCommand(OPTIMIZER_STATE state)
    {
        return {static_cast<char>(0x84), 0x01, 0x00, static_cast<char>(state)};
    }

    Buffer serializeXM4SpeakToChat(S2C_TOGGLE state)
    {
        return {static_cast<char>(0xf8), 0x05, 0x01, static_cast<char>(state)};
    }

    Buffer serializeXM4_S2C_Options(unsigned char sensitivity, unsigned char focus, unsigned char resumeDelay)
    {
        return {static_cast<char>(0xfc), 0x05, 0x00,
            static_cast<char>(sensitivity), static_cast<char>(focus), static_cast<char>(resumeDelay)};
    }

    Buffer serializeMultiPointCommand(MULTI_POINT_COMMANDS operation, std::string address)
    {
        Buffer payload(address.size() + 4);
        payload[0] = 0x3c;
        payload[1] = 0x01;
        payload[2] = 0x00;
        payload[3] = static_cast<char>(operation);
        std::copy(address.begin(), address.end(), payload.begin() + 4);
        return payload;
    }

	Buffer serializeEqualizerPreset(unsigned char presetCode)
	{
		Buffer ret;
		ret.push_back(static_cast<unsigned char>(COMMAND_TYPE::EQUALIZER_SET));
		ret.push_back(0x01);        // EqEbb inquired type: preset EQ
		ret.push_back(presetCode);
		ret.push_back(0x00);
		return ret;
	}

	Buffer serializeEqualizerCustomBands(int clearBass, const std::vector<int>& bands)
	{
		Buffer ret;
		ret.push_back(static_cast<unsigned char>(COMMAND_TYPE::EQUALIZER_SET));
		ret.push_back(0x01);        // EqEbb inquired type: preset EQ
		ret.push_back(static_cast<char>(0xff)); // custom/manual preset marker
		ret.push_back(static_cast<unsigned char>(1 + bands.size())); // clear bass + N bands
		ret.push_back(static_cast<unsigned char>(clearBass + 10));   // bias -10..+10 -> 0..20
		for (int band : bands)
			ret.push_back(static_cast<unsigned char>(band + 10));
		return ret;
	}

	Buffer serializeAudioUpsampling(bool enabled)
	{
		// DSEE / DSEE Extreme. Payload: <cmd> 02 00 <0/1>
		Buffer ret;
		ret.push_back(static_cast<unsigned char>(COMMAND_TYPE::AUDIO_UPSAMPLING_SET));
		ret.push_back(0x02);
		ret.push_back(0x00);
		ret.push_back(enabled ? 0x01 : 0x00);
		return ret;
	}

	Buffer serializeVoiceGuidance(bool enabled)
	{
		// Voice guidance prompts. Payload: <cmd> 01 01 <0/1>. Sent as DATA_MDR_NO2 by the caller.
		Buffer ret;
		ret.push_back(static_cast<unsigned char>(COMMAND_TYPE::VOICE_NOTIFICATION_SET));
		ret.push_back(0x01);
		ret.push_back(0x01);
		ret.push_back(enabled ? 0x01 : 0x00);
		return ret;
	}

	Buffer serializeAutoPowerOff(AUTO_POWER_OFF mode)
	{
		// Auto power-off. Payload: <cmd> 04 01 <code0> <code1>. The 0xf8 command is shared with
		// other system settings; sub-byte 0x04 selects the auto-power-off function.
		unsigned char c0 = 0x11, c1 = 0x00; // default: OFF (do not power off)
		switch (mode)
		{
		case AUTO_POWER_OFF::OFF:            c0 = 0x11; c1 = 0x00; break;
		case AUTO_POWER_OFF::AFTER_5_MIN:    c0 = 0x00; c1 = 0x00; break;
		case AUTO_POWER_OFF::AFTER_30_MIN:   c0 = 0x01; c1 = 0x01; break;
		case AUTO_POWER_OFF::AFTER_1_HOUR:   c0 = 0x02; c1 = 0x02; break;
		case AUTO_POWER_OFF::AFTER_3_HOUR:   c0 = 0x03; c1 = 0x03; break;
		case AUTO_POWER_OFF::WHEN_TAKEN_OFF: c0 = 0x10; c1 = 0x00; break;
		}

		Buffer ret;
		ret.push_back(static_cast<unsigned char>(COMMAND_TYPE::AUTO_POWER_OFF_SET));
		ret.push_back(0x04);
		ret.push_back(0x01);
		ret.push_back(c0);
		ret.push_back(c1);
		return ret;
	}

	// ---- discovery and read queries -------------------------------------------------------

	Buffer serializeSupportedFunctionsQuery()
	{
		return { static_cast<char>(v1::t1::CONNECT_GET_SUPPORT_FUNCTION),
			static_cast<char>(v1::SUPPORT_FUNCTION_INQUIRY) };
	}

	Buffer serializeGeneralSettingCapabilityQuery(unsigned char slot)
	{
		return { static_cast<char>(v1::t1::GENERAL_SETTING_GET_CAPABILITY), static_cast<char>(slot),
			static_cast<char>(v1::GS_LANGUAGE_ENGLISH) };
	}

	Buffer serializeGeneralSettingStatusQuery(unsigned char slot)
	{
		return { static_cast<char>(v1::t1::GENERAL_SETTING_GET_STATUS), static_cast<char>(slot) };
	}

	Buffer serializeGeneralSettingQuery(unsigned char slot)
	{
		return { static_cast<char>(v1::t1::GENERAL_SETTING_GET_PARAM), static_cast<char>(slot) };
	}

	Buffer serializeSystemCapabilityQuery(unsigned char selector)
	{
		return { static_cast<char>(v1::t1::SYSTEM_GET_CAPABILITY), static_cast<char>(selector) };
	}

	Buffer serializeSystemStatusQuery(unsigned char selector)
	{
		return { static_cast<char>(v1::t1::SYSTEM_GET_STATUS), static_cast<char>(selector) };
	}

	Buffer serializeSystemQuery(unsigned char selector)
	{
		return { static_cast<char>(v1::t1::SYSTEM_GET_PARAM), static_cast<char>(selector) };
	}

	// ---- bounded reply decoders -------------------------------------------------------------

	std::optional<std::vector<unsigned char>> parseSupportedFunctions(const Message& msg)
	{
		// [07][inquiry][count][count function IDs]. The command, the inquiry and the count are
		// headers, not function IDs, and the declared count has to cover every byte after them.
		const Buffer& p = msg.messageBytes;
		if (!isTable1(msg) || p.size() < 3)
			return std::nullopt;
		if (u8(p, 0) != v1::t1::CONNECT_RET_SUPPORT_FUNCTION || u8(p, 1) != v1::SUPPORT_FUNCTION_INQUIRY)
			return std::nullopt;
		const size_t count = u8(p, 2);
		if (p.size() != 3 + count)
			return std::nullopt;
		std::vector<unsigned char> ids;
		ids.reserve(count);
		for (size_t i = 3; i < p.size(); ++i)
			ids.push_back(u8(p, i));
		return ids;
	}

	std::optional<BatteryReport> parseBattery(const Message& msg)
	{
		// [11|13][battery type 00][level 0-100][charging status]. Left/right and cradle
		// batteries have other layouts; only the single battery we ask for is decoded.
		const Buffer& p = msg.messageBytes;
		if (!isTable1(msg) || p.size() != 4)
			return std::nullopt;
		const unsigned char cmd = u8(p, 0);
		if (cmd != v1::t1::COMMON_RET_BATTERY_LEVEL && cmd != v1::t1::COMMON_NTFY_BATTERY_LEVEL)
			return std::nullopt;
		if (u8(p, 1) != v1::BATTERY_SINGLE || u8(p, 2) > 100)
			return std::nullopt;
		BatteryReport report;
		report.level = u8(p, 2);
		// BatteryChargingStatus: 00 not charging, 01 charging, F0 unknown.
		report.charging = u8(p, 3) == 0x01;
		return report;
	}

	std::optional<NcAsmReport> parseNcAsm(const Message& msg)
	{
		// Same 8-byte layout as SET: [67|69][02][effect][ncType][ncValue][asmType][asmId][asmLevel]
		const Buffer& p = msg.messageBytes;
		if (!isTable1(msg) || p.size() != 8)
			return std::nullopt;
		const unsigned char cmd = u8(p, 0);
		if (cmd != v1::t1::NCASM_RET_PARAM && cmd != v1::t1::NCASM_NTFY_PARAM)
			return std::nullopt;
		if (u8(p, 1) != static_cast<unsigned char>(NC_ASM_INQUIRED_TYPE::NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE))
			return std::nullopt;

		NcAsmReport r;
		r.effect = u8(p, 2);
		r.ncSettingType = u8(p, 3);
		r.ncValue = u8(p, 4);
		r.asmSettingType = u8(p, 5);
		r.asmId = u8(p, 6);
		r.asmLevel = u8(p, 7);

		const bool effectOk = r.effect == 0x00 || r.effect == 0x01 || r.effect == 0x10 || r.effect == 0x11;
		const bool ncTypeOk = r.ncSettingType <= static_cast<unsigned char>(NC_ASM_SETTING_TYPE::DUAL_SINGLE_OFF);
		// On/off carries 00/01; the level and dual/single types carry OFF/SINGLE/DUAL (00-02).
		const unsigned char maxNcValue =
			r.ncSettingType == static_cast<unsigned char>(NC_ASM_SETTING_TYPE::ON_OFF) ? 0x01 : 0x02;
		const bool asmTypeOk = r.asmSettingType <= static_cast<unsigned char>(ASM_SETTING_TYPE::LEVEL_ADJUSTMENT);
		const bool asmIdOk = r.asmId <= static_cast<unsigned char>(ASM_ID::VOICE);
		if (!effectOk || !ncTypeOk || r.ncValue > maxNcValue || !asmTypeOk || !asmIdOk)
			return std::nullopt;
		return r;
	}

	std::optional<bool> parseOptimizerStatus(const Message& msg)
	{
		// [85][01 NC optimizer][status ...]. Only the status byte's zero/non-zero meaning has been
		// exercised on hardware, so that is all this decodes.
		const Buffer& p = msg.messageBytes;
		if (!isTable1(msg) || p.size() < 3)
			return std::nullopt;
		if (u8(p, 0) != v1::t1::OPT_NTFY_STATUS || u8(p, 1) != v1::NC_OPTIMIZER)
			return std::nullopt;
		return u8(p, 2) != 0x00;
	}

	std::optional<std::vector<PairedDevice>> parsePairedDevices(const Message& msg)
	{
		// Table 2: [37|39][01][numDevices]{ [17-byte MAC][slot][nameLen][name] }*
		const Buffer& p = msg.messageBytes;
		if (!isTable2(msg) || p.size() < 3)
			return std::nullopt;
		const unsigned char cmd = u8(p, 0);
		if (cmd != v1::t2::PERIPHERAL_RET_PARAM && cmd != v1::t2::PERIPHERAL_NTFY_PARAM)
			return std::nullopt;
		if (u8(p, 1) != v1::PERIPHERAL_CLASSIC_BT)
			return std::nullopt;

		std::vector<PairedDevice> devices;
		size_t idx = 3;
		for (int remaining = u8(p, 2); remaining > 0; --remaining)
		{
			if (idx + MAC_ADDR_STR_SIZE + 2 > p.size())
				return std::nullopt;
			PairedDevice device;
			device.mac.assign(p.begin() + idx, p.begin() + idx + MAC_ADDR_STR_SIZE);
			idx += MAC_ADDR_STR_SIZE;
			device.slot = u8(p, idx++);
			const size_t nameLength = u8(p, idx++);
			if (idx + nameLength > p.size())
				return std::nullopt;
			device.name.assign(p.begin() + idx, p.begin() + idx + nameLength);
			idx += nameLength;
			devices.push_back(std::move(device));
		}
		return devices;
	}

	std::optional<GeneralSettingCapability> parseGeneralSettingCapability(const Message& msg)
	{
		// [D1][slot][string format][title length][title][summary length][summary][type][...]
		// The string record takes 3 + title + summary bytes, so the type byte sits at absolute
		// offset 5 + title + summary. Every length is checked before the bytes it covers are read.
		const Buffer& p = msg.messageBytes;
		if (!isTable1(msg) || p.size() < 3)
			return std::nullopt;
		if (u8(p, 0) != v1::t1::GENERAL_SETTING_RET_CAPABILITY || !v1::isGeneralSettingSlot(u8(p, 1)))
			return std::nullopt;

		GeneralSettingCapability cap;
		cap.slot = u8(p, 1);
		cap.stringFormat = u8(p, 2);

		size_t pos = 3;
		std::string* strings[] = { &cap.title, &cap.summary };
		for (std::string* out : strings)
		{
			if (pos >= p.size())
				return std::nullopt;
			const size_t length = u8(p, pos++);
			if (length > v1::GS_MAX_STRING_BYTES || pos + length > p.size())
				return std::nullopt;
			if (!isValidUtf8(p, pos, pos + length))
				return std::nullopt;
			out->assign(p.begin() + pos, p.begin() + pos + length);
			pos += length;
		}

		if (pos >= p.size())
			return std::nullopt;
		cap.settingType = u8(p, pos++);
		cap.typeSpecificBytes = p.size() - pos;
		return cap;
	}

	bool namesGeneralSetting(const GeneralSettingCapability& cap, const char* title)
	{
		return cap.stringFormat == v1::GS_STRING_FORMAT_ENUM_NAME && cap.title == title;
	}

	bool isBooleanGeneralSetting(const GeneralSettingCapability& cap)
	{
		return cap.settingType == v1::GS_TYPE_BOOLEAN && cap.typeSpecificBytes == 0;
	}

	std::optional<AvailabilityReport> parseGeneralSettingAvailability(const Message& msg)
	{
		// [D3|D5][slot][00 available | 01 unavailable]
		const Buffer& p = msg.messageBytes;
		if (!isTable1(msg) || p.size() != 3)
			return std::nullopt;
		const unsigned char cmd = u8(p, 0);
		if (cmd != v1::t1::GENERAL_SETTING_RET_STATUS && cmd != v1::t1::GENERAL_SETTING_NTFY_STATUS)
			return std::nullopt;
		if (!v1::isGeneralSettingSlot(u8(p, 1)) || u8(p, 2) > v1::STATUS_DISABLE)
			return std::nullopt;
		return AvailabilityReport{ u8(p, 1), u8(p, 2) == v1::STATUS_ENABLE };
	}

	std::optional<ToggleReport> parseGeneralSettingBool(const Message& msg)
	{
		// [D7|D9][slot][01 boolean type][00 off | 01 on]. A list-type value is an index, not a
		// boolean, and is rejected rather than coerced.
		const Buffer& p = msg.messageBytes;
		if (!isTable1(msg) || p.size() != 4)
			return std::nullopt;
		const unsigned char cmd = u8(p, 0);
		if (cmd != v1::t1::GENERAL_SETTING_RET_PARAM && cmd != v1::t1::GENERAL_SETTING_NTFY_PARAM)
			return std::nullopt;
		if (!v1::isGeneralSettingSlot(u8(p, 1)) || u8(p, 2) != v1::GS_TYPE_BOOLEAN || u8(p, 3) > 0x01)
			return std::nullopt;
		return ToggleReport{ u8(p, 1), u8(p, 3) == 0x01 };
	}

	std::optional<unsigned char> parseWearingCapability(const Message& msg)
	{
		// [F1][03][setting type]
		const Buffer& p = msg.messageBytes;
		if (!isTable1(msg) || p.size() != 3)
			return std::nullopt;
		if (u8(p, 0) != v1::t1::SYSTEM_RET_CAPABILITY || u8(p, 1) != v1::SYSTEM_CONTROL_BY_WEARING)
			return std::nullopt;
		return u8(p, 2);
	}

	std::optional<AvailabilityReport> parseWearingAvailability(const Message& msg)
	{
		// [F3|F5][03][00 available | 01 unavailable]. Feature availability - not whether the
		// headphones are being worn right now.
		const Buffer& p = msg.messageBytes;
		if (!isTable1(msg) || p.size() != 3)
			return std::nullopt;
		const unsigned char cmd = u8(p, 0);
		if (cmd != v1::t1::SYSTEM_RET_STATUS && cmd != v1::t1::SYSTEM_NTFY_STATUS)
			return std::nullopt;
		if (u8(p, 1) != v1::SYSTEM_CONTROL_BY_WEARING || u8(p, 2) > v1::STATUS_DISABLE)
			return std::nullopt;
		return AvailabilityReport{ u8(p, 1), u8(p, 2) == v1::STATUS_ENABLE };
	}

	std::optional<ToggleReport> parseWearingSetting(const Message& msg)
	{
		// [F7|F9][03][00 ON_OFF type][00 off | 01 on]
		const Buffer& p = msg.messageBytes;
		if (!isTable1(msg) || p.size() != 4)
			return std::nullopt;
		const unsigned char cmd = u8(p, 0);
		if (cmd != v1::t1::SYSTEM_RET_PARAM && cmd != v1::t1::SYSTEM_NTFY_PARAM)
			return std::nullopt;
		if (u8(p, 1) != v1::SYSTEM_CONTROL_BY_WEARING || u8(p, 2) != v1::PROTOCOL_SETTING_TYPE_ON_OFF ||
			u8(p, 3) > 0x01)
			return std::nullopt;
		return ToggleReport{ u8(p, 1), u8(p, 3) == 0x01 };
	}

}

