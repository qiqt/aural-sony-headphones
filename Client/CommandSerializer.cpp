#include "PacketCodec.h"
#include <algorithm>
#include "CommandSerializer.h"


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

}

