#pragma once
#include "Constants.h"
#include <cstddef>
#include <optional>
#include <string>
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

    Buffer _escapeSpecials(const Buffer& src);
    Buffer _unescapeSpecials(const Buffer& src);
    unsigned char _sumChecksum(const char* src, size_t size);
    unsigned char _sumChecksum(const Buffer& src);
    // Frame markers, escaping, checksum and length are handled by PacketCodec.
    Buffer packageDataForBt(const Buffer& src, DATA_TYPE type, unsigned int sequence);

	Message unpackBtMessage(const Buffer& src);

	NC_DUAL_SINGLE_VALUE getDualSingleForAsmLevel(char asmLevel);
	Buffer serializeNcAndAsmSetting(NC_ASM_EFFECT ncAsmEffect, NC_ASM_SETTING_TYPE ncAsmSettingType, ASM_SETTING_TYPE asmSettingType, ASM_ID asmId, char asmLevel);
	Buffer serializeVPTSetting(VPT_INQUIRED_TYPE type, unsigned char preset);

	// v1 battery level request (BATTERY_LEVEL_REQUEST 0x10, single battery 0x00). Reply is 0x11.
	Buffer serializeBatteryRequest();

	// V1 feature payloads; byte layouts are covered by protocol tests.
	Buffer serializeXM4OptimizeCommand(OPTIMIZER_STATE state);
	Buffer serializeXM4SpeakToChat(S2C_TOGGLE s2cState);
	Buffer serializeXM4_S2C_Options(unsigned char sensitivity, unsigned char voice, unsigned char offTime);
	Buffer serializeMultiPointCommand(MULTI_POINT_COMMANDS cmd, std::string macAddr);

	// v1 equalizer (EqEbb). Either select a preset, or push custom band levels (clear bass + 5
	// bands), each in the range -10..+10 (sent biased by +10).
	Buffer serializeEqualizerPreset(unsigned char presetCode);
	Buffer serializeEqualizerCustomBands(int clearBass, const std::vector<int>& bands);

	// v1 sound-quality / system serializers (from Gadgetbridge SonyProtocolImplV1).
	Buffer serializeAudioUpsampling(bool enabled);   // DSEE / DSEE Extreme; send as DATA_MDR
	Buffer serializeVoiceGuidance(bool enabled);     // voice guidance; send as DATA_MDR_NO2
	Buffer serializeAutoPowerOff(AUTO_POWER_OFF mode);

	// Auto power-off carries two element IDs: the active one and the selected timer. Both are
	// written as given; see Headphones::setAutoPowerOff for how the second one is preserved.
	Buffer serializeAutoPowerOff(unsigned char activeElement, unsigned char selectedTimeElement);

	// D8 slot 01 value: a BOOLEAN_TYPE general setting. The slot must come from discovery.
	Buffer serializeGeneralSettingBool(unsigned char slot, bool enabled);
	// F8 03 00 value: wearing control, ProtocolSettingType.ON_OFF.
	Buffer serializeWearingControl(bool enabled);

	// 94 01 00: ask the device to send fixed-message alerts (CommonStatus.ENABLE). Only once the
	// host can show an alert and answer it.
	Buffer serializeAlertEnable();
	// 98 01 id choice: the answer to a multipoint alert, echoing the message ID the device sent.
	// Throws std::invalid_argument for any other message ID: there is no answer to compose
	// for an alert that was never decoded.
	Buffer serializeMultipointAlertResponse(unsigned char messageId, bool accept);

	// ---- discovery and read queries (table 1 / DATA_MDR unless noted) ------------------------
	Buffer serializeNcAsmQuery();                                      // 66 02
	Buffer serializeEqualizerQuery();                                  // 56 01
	Buffer serializeVptQuery(VPT_INQUIRED_TYPE type);                  // 46 01 (VPT) / 46 02 (position)
	Buffer serializeAudioQuery(unsigned char selector);                // E6 selector (02 = DSEE)
	Buffer serializeSpeakToChatDetailsQuery();                         // FA 05
	Buffer serializeVoiceGuidanceQuery();                              // 46 01 01 - table 2 (DATA_MDR_NO2)
	Buffer serializeSupportedFunctionsQuery();                         // 06 01
	Buffer serializeGeneralSettingCapabilityQuery(unsigned char slot); // D0 slot 01 (English)
	Buffer serializeGeneralSettingStatusQuery(unsigned char slot);     // D2 slot
	Buffer serializeGeneralSettingQuery(unsigned char slot);           // D6 slot
	Buffer serializeSystemCapabilityQuery(unsigned char selector);     // F0 selector
	Buffer serializeSystemStatusQuery(unsigned char selector);         // F2 selector
	Buffer serializeSystemQuery(unsigned char selector);               // F6 selector

	// ---- bounded reply decoders ---------------------------------------------------------------
	// Each decoder checks the table (data type), the command, the selector and the exact length
	// before it reads a byte, and validates every enumerated value. nullopt means "this frame is
	// not that reply" - malformed, truncated, from the other table, or carrying a value outside
	// the recovered enum - and the caller must leave its state exactly as it was.

	// 07 01 count ids... -> the advertised function IDs (count honoured, headers excluded).
	std::optional<std::vector<unsigned char>> parseSupportedFunctions(const Message& msg);

	// 11/13 00 level charging (single battery only).
	struct BatteryReport
	{
		int level = 0;
		bool charging = false;
	};
	std::optional<BatteryReport> parseBattery(const Message& msg);

	// 67/69 02 effect ncType ncValue asmType asmId asmLevel. Every field is kept: the NC setting
	// type and value are needed to tell some modes apart, not only the effect and level.
	struct NcAsmReport
	{
		unsigned char effect = 0;          // NC_ASM_EFFECT
		unsigned char ncSettingType = 0;   // NC_ASM_SETTING_TYPE
		unsigned char ncValue = 0;         // meaning depends on ncSettingType
		unsigned char asmSettingType = 0;  // ASM_SETTING_TYPE
		unsigned char asmId = 0;           // ASM_ID
		unsigned char asmLevel = 0;        // raw byte; the client writes 0xFF while ambient is off

		bool operator==(const NcAsmReport& o) const
		{
			return effect == o.effect && ncSettingType == o.ncSettingType && ncValue == o.ncValue &&
				asmSettingType == o.asmSettingType && asmId == o.asmId && asmLevel == o.asmLevel;
		}
	};
	std::optional<NcAsmReport> parseNcAsm(const Message& msg);

	// 85 01 status ... (NC optimizer). true while the optimizer reports a non-idle status.
	std::optional<bool> parseOptimizerStatus(const Message& msg);

	// Table 2: 37/39 01 count { 17-byte MAC, slot, nameLength, name }*
	struct PairedDevice
	{
		std::string mac;
		std::string name;
		unsigned char slot = 0;   // 1 / 2 = connected in that position, otherwise just paired
	};
	std::optional<std::vector<PairedDevice>> parsePairedDevices(const Message& msg);

	// D1 slot format titleLen title summaryLen summary type [type-specific data]. Lengths are
	// UTF-8 byte counts, at most GS_MAX_STRING_BYTES each.
	struct GeneralSettingCapability
	{
		unsigned char slot = 0;
		unsigned char stringFormat = 0;   // GS_STRING_FORMAT_ENUM_NAME for a named setting
		std::string title;
		std::string summary;
		unsigned char settingType = 0;    // GS_TYPE_BOOLEAN / GS_TYPE_LIST / ...
		size_t typeSpecificBytes = 0;     // capability data after the type byte (lists carry some)
	};
	std::optional<GeneralSettingCapability> parseGeneralSettingCapability(const Message& msg);
	// Sony matches a title only when the record uses the ENUM_NAME string format.
	bool namesGeneralSetting(const GeneralSettingCapability& cap, const char* title);
	// A boolean setting: BOOLEAN_TYPE. A WH-1000XM4 follows the type byte with one more byte (00)
	// on both of its boolean records; what it means is not recovered, and it changes nothing about
	// how the value is read or written, so it is allowed. A list type carries an index, not a
	// bool, and must not be coerced into one.
	bool isBooleanGeneralSetting(const GeneralSettingCapability& cap);

	// D3/D5 slot status, F3/F5 03 status. available == (status == STATUS_ENABLE).
	struct AvailabilityReport
	{
		unsigned char selector = 0;   // general-setting slot, or the system selector
		bool available = false;
	};
	std::optional<AvailabilityReport> parseGeneralSettingAvailability(const Message& msg);
	std::optional<AvailabilityReport> parseWearingAvailability(const Message& msg);

	// D7/D9 slot 01 value (boolean general setting), F7/F9 03 00 value (wearing control).
	struct ToggleReport
	{
		unsigned char selector = 0;
		bool enabled = false;
	};
	std::optional<ToggleReport> parseGeneralSettingBool(const Message& msg);
	std::optional<ToggleReport> parseWearingSetting(const Message& msg);

	// F1 03 type -> the setting type the device uses for wearing control.
	std::optional<unsigned char> parseWearingCapability(const Message& msg);

	// 57/59 01 preset count levels... The levels are raw bytes: how they map to dB (and which
	// one is Clear Bass) is the existing client convention, not something the APK settles.
	struct EqualizerReport
	{
		unsigned char preset = 0;               // EqPresetId
		std::vector<unsigned char> levels;      // exactly `count` bytes
	};
	std::optional<EqualizerReport> parseEqualizer(const Message& msg);

	// 47/49 type preset (table 1). type 01 = VPT preset, 02 = sound position preset.
	struct SpatialReport
	{
		VPT_INQUIRED_TYPE type = VPT_INQUIRED_TYPE::NO_USE;
		unsigned char preset = 0;
	};
	std::optional<SpatialReport> parseVpt(const Message& msg);

	// Speak-to-Chat toggle. RET is F7 05 00 v (SmartTalkingModeSettingType.ON_OFF); NTFY is
	// F9 05 01 v (MODE_ON_OFF) or F9 05 02 v (PREVIEW_MODE_ON_OFF). A preview is not the
	// persistent setting and must never be applied as one.
	struct SpeakToChatToggleReport
	{
		bool preview = false;
		bool enabled = false;
	};
	std::optional<SpeakToChatToggleReport> parseSpeakToChatToggle(const Message& msg);

	// FB/FD 05 00 sensitivity voiceFocus exitTime
	struct SpeakToChatDetailsReport
	{
		unsigned char sensitivity = 0;   // 00 auto, 01 high, 02 low
		bool voiceFocus = false;
		unsigned char exitTime = 0;      // 00 FAST, 01 MID, 02 SLOW, 03 NONE - durations unverified
	};
	std::optional<SpeakToChatDetailsReport> parseSpeakToChatDetails(const Message& msg);

	// E7/E9 02 00 v: the DSEE *setting* (off / auto). Not the 15/17 "processing now" indicator.
	std::optional<bool> parseUpscaling(const Message& msg);

	// Table 2: 47/49 01 01 v (voice guidance setting, ON_OFF detail).
	std::optional<bool> parseVoiceGuidance(const Message& msg);

	// F7/F9 04 01 active selectedTime
	struct AutoPowerOffReport
	{
		unsigned char activeElement = 0;
		unsigned char selectedTimeElement = 0;
	};
	std::optional<AutoPowerOffReport> parseAutoPowerOff(const Message& msg);

	// 99 01 id action: the device asks the user to confirm a multipoint change. Only the two
	// multipoint message IDs are decoded; every other fixed message is not ours to answer.
	struct MultipointAlert
	{
		unsigned char messageId = 0;    // ALERT_MSG_MULTIPOINT_TO_ON (06) / _TO_OFF (07)
		unsigned char actionType = 0;   // 00 confirmation only, 01 accept or cancel

		bool operator==(const MultipointAlert& o) const
		{
			return messageId == o.messageId && actionType == o.actionType;
		}
	};
	std::optional<MultipointAlert> parseMultipointAlert(const Message& msg);
}

