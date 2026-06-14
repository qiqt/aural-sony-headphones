#pragma once
#include "Constants.h"
#include <cstddef>
#include <vector>
#include <stdexcept>
#include "Exceptions.h"

constexpr int MINIMUM_VOICE_FOCUS_STEP = 2;
constexpr unsigned int ASM_LEVEL_DISABLED = -1;

namespace CommandSerializer
{
	struct Message
	{
		DATA_TYPE dataType;
		unsigned char seqNumber;
		Buffer messageBytes; // the unescaped command payload (the DATA section)
	};

	//escape special chars

	Buffer _escapeSpecials(const Buffer& src);
	Buffer _unescapeSpecials(const Buffer& src);
	unsigned char _sumChecksum(const char* src, size_t size);
	unsigned char _sumChecksum(const Buffer& src);
	//Package a serialized command according to the protocol
	/*
	References:
	* DataType
	* CommandBluetoothSender.sendCommandWithRetries
	* BluetoothSenderWrapper.sendCommandViaBluetooth
	* 
	* Serialized data format: <START_MARKER>ESCAPE_SPECIALS(<DATA_TYPE><SEQ_NUMBER><BIG ENDIAN 4 BYTE SIZE OF UNESCAPED DATA><DATA><1 BYTE CHECKSUM>)<END_MARKER>
	*/
	Buffer packageDataForBt(const Buffer& src, DATA_TYPE dataType, unsigned int seqNumber);

	Message unpackBtMessage(const Buffer& src);

	NC_DUAL_SINGLE_VALUE getDualSingleForAsmLevel(char asmLevel);
	Buffer serializeNcAndAsmSetting(NC_ASM_EFFECT ncAsmEffect, NC_ASM_SETTING_TYPE ncAsmSettingType, ASM_SETTING_TYPE asmSettingType, ASM_ID asmId, char asmLevel);
	Buffer serializeVPTSetting(VPT_INQUIRED_TYPE type, unsigned char preset);

	// v1 battery level request (BATTERY_LEVEL_REQUEST 0x10, single battery 0x00). Reply is 0x11.
	Buffer serializeBatteryRequest();

	// v1 feature serializers (proven on XM4, from PR #103)
	Buffer serializeXM4OptimizeCommand(OPTIMIZER_STATE state);
	Buffer serializeXM4SpeakToChat(S2C_TOGGLE s2cState);
	Buffer serializeXM4_S2C_Options(unsigned char sensitivity, unsigned char voice, unsigned char offTime);
	Buffer serializeMultiPointCommand(MULTI_POINT_COMMANDS cmd, std::string macAddr);

	// v1 equalizer (EqEbb). Either select a preset, or push custom band levels (clear bass + 5
	// bands), each in the range -10..+10 (sent biased by +10).
	Buffer serializeEqualizerPreset(unsigned char presetCode);
	Buffer serializeEqualizerCustomBands(int clearBass, const std::vector<int>& bands);
}

