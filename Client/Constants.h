#pragma once

#include <cstddef>
#include <vector>
#include <string>

inline constexpr auto MAX_BLUETOOTH_MESSAGE_SIZE = 2048;
inline constexpr char START_MARKER{ 62 };
inline constexpr char END_MARKER{ 60 };
inline constexpr std::size_t MAC_ADDR_STR_SIZE = 17;


// Sony uses two RFCOMM "Serial HPC" service UUIDs depending on protocol generation.
// The frame format is identical; the UUID and command set differ. We pick per device.
//   V1 (legacy MDR):  WH-1000XM3 / XM4, MDR-* ...
//   V2 (mdr v2 / table1): WH-1000XM5 / XM6 ...
enum class SonyProtocol
{
	V1,
	V2
};

// V1 (legacy) service UUID
inline constexpr auto SERVICE_UUID = "96CC203E-5068-46ad-B32D-E316F5E069BA";
// V2 service UUID (WH-1000XM5 and newer)
inline constexpr auto SERVICE_UUID_V2 = "956C7B26-D49A-4BA8-B03F-B17D393CB6E2";
inline const char* serviceUuidString(SonyProtocol protocol)
{
	return protocol == SonyProtocol::V2 ? SERVICE_UUID_V2 : SERVICE_UUID;
}

// ---------------------------------------------------------------------------------------
// Model identity
//
// A Bluetooth name is user-editable, so it is a hint, not an identity. We match it against
// an explicit table of product codes rather than looking for a substring like "XM5": that
// used to turn any device whose name happened to contain those characters into a
// "WH-1000XM5", and a headset renamed to something personal into a confidently wrong model.
//
// A name we don't recognise yields SonyModel::Unknown with recognised == false. The UI shows
// the raw Bluetooth name in that case instead of inventing a marketing name.
// ---------------------------------------------------------------------------------------

enum class SonyModel
{
	Unknown = 0,
	WH_1000XM3,
	WH_1000XM4,
	WH_1000XM5,
	WH_1000XM6,
	WF_1000XM3,
	WF_1000XM4,
	WF_1000XM5
};

struct DeviceIdentity
{
	SonyModel model = SonyModel::Unknown;
	// Protocol generation to attempt. For an unrecognised name this is the V1 default, which
	// is an attempt, not a statement about the device.
	SonyProtocol protocol = SonyProtocol::V1;
	// True only when the name matched a product code in the table below.
	bool recognised = false;
	// Marketing name, empty when the model is Unknown.
	const char* marketingName = "";
};

namespace detail
{
	struct KnownModel
	{
		const char* code;          // normalised product code (upper case, no separators)
		SonyModel model;
		SonyProtocol protocol;
		const char* marketingName;
	};

	// The models this client has code paths for. Protocol generation only; presence here is
	// not a claim that every feature works on that model - see the support matrix in the
	// project notes. Only the WH-1000XM4 (V1) path has been exercised against hardware.
	inline const KnownModel KNOWN_MODELS[] = {
		{ "WH1000XM3", SonyModel::WH_1000XM3, SonyProtocol::V1, "WH-1000XM3" },
		{ "WH1000XM4", SonyModel::WH_1000XM4, SonyProtocol::V1, "WH-1000XM4" },
		{ "WH1000XM5", SonyModel::WH_1000XM5, SonyProtocol::V2, "WH-1000XM5" },
		{ "WH1000XM6", SonyModel::WH_1000XM6, SonyProtocol::V2, "WH-1000XM6" },
		{ "WF1000XM3", SonyModel::WF_1000XM3, SonyProtocol::V1, "WF-1000XM3" },
		{ "WF1000XM4", SonyModel::WF_1000XM4, SonyProtocol::V2, "WF-1000XM4" },
		{ "WF1000XM5", SonyModel::WF_1000XM5, SonyProtocol::V2, "WF-1000XM5" }
	};

	// Upper case, drop everything that is not a letter or a digit, so "WH-1000XM4",
	// "wh_1000xm4" and "WH 1000XM4 (LE)" all normalise to the same token stream.
	inline std::string normaliseDeviceName(const std::string& name)
	{
		std::string out;
		out.reserve(name.size());
		for (unsigned char c : name)
		{
			if (c >= 'a' && c <= 'z') out.push_back(static_cast<char>(c - 'a' + 'A'));
			else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out.push_back(static_cast<char>(c));
		}
		return out;
	}
}

inline DeviceIdentity identifyDevice(const std::string& bluetoothName)
{
	const std::string normalised = detail::normaliseDeviceName(bluetoothName);
	for (const auto& known : detail::KNOWN_MODELS)
	{
		// The full product code has to appear: "XM5" on its own is not enough.
		if (normalised.find(known.code) != std::string::npos)
			return DeviceIdentity{ known.model, known.protocol, true, known.marketingName };
	}
	// Unrecognised: try the legacy protocol, but say that we do not know what this is.
	return DeviceIdentity{ SonyModel::Unknown, SonyProtocol::V1, false, "" };
}

using Buffer = std::vector<char>;

enum class DATA_TYPE : signed char
{
	DATA = 0,
	ACK = 1,
    DATA_MC_NO1 = 2,
    DATA_ICD = 9,
    DATA_EV = 10,
	DATA_MDR = 12,
    DATA_COMMON = 13,
    DATA_MDR_NO2 = 14,
    SHOT =  16,
    SHOT_MC_NO1 =  18,
    SHOT_ICD =  25,
    SHOT_EV =  26,
    SHOT_MDR =  28,
    SHOT_COMMON =  29,
    SHOT_MDR_NO2 = 30,
    LARGE_DATA_COMMON =  45,
    UNKNOWN = -1
};


enum class NC_ASM_INQUIRED_TYPE : signed char
{
	NO_USE = 0,
	NOISE_CANCELLING = 1,
	NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE = 2,
	AMBIENT_SOUND_MODE = 3
};

enum class NC_ASM_EFFECT : signed char
{
	OFF = 0,
	ON = 1,
	ADJUSTMENT_IN_PROGRESS = 16,
	ADJUSTMENT_COMPLETION = 17
};

enum class NC_ASM_SETTING_TYPE : signed char
{
	ON_OFF = 0,
	LEVEL_ADJUSTMENT = 1,
	DUAL_SINGLE_OFF = 2
};

enum class ASM_SETTING_TYPE : signed char
{
	ON_OFF = 0,
	LEVEL_ADJUSTMENT = 1
};

enum class ASM_ID : signed char
{
	NORMAL = 0,
	VOICE = 1
};

enum class NC_DUAL_SINGLE_VALUE : signed char
{
	OFF = 0,
	SINGLE = 1,
	DUAL = 2
};

enum class COMMAND_TYPE : signed char
{
	VPT_SET_PARAM = 72,

	// v1 NC/ASM function block follows the usual GET/RET/SET/NOTIFY spacing (like EqEbb 0x56-0x59).
	// RET and NOTIFY carry the same 8-byte payload layout as SET (see serializeNcAndAsmSetting).
	NCASM_GET_PARAM = 102,     // 0x66
	NCASM_RET_PARAM = 103,     // 0x67
	NCASM_SET_PARAM = 104,     // 0x68
	NCASM_NTFY_PARAM = 105,    // 0x69

	// V1 feature command identifiers.
	XM4_OPTIMIZER_PARAM = (signed char)0x84,
	XM4_S2C_TOGGLE_PARAM = (signed char)0xf8,
	XM4_S2C_OPTIONS_PARAM = (signed char)0xfc,
	MULTI_POINT_PARAM = (signed char)0x3c,

	XM4_OPTIMIZER_RESPONSE = (signed char)0x85,
	DEVICES_QUERY_RESPONSE = (signed char)0x37,
	DEVICES_STATE_RESPONSE = (signed char)0x39,
	CAPABILITY_QUERY_RESPONSE = (signed char)0x07,

	CAPABILITY_QUERY = (signed char)0x06,
	MULTI_POINT_DEVICES_QUERY = (signed char)0x36,
	S2C_QUERY = (signed char)0xf6,
	S2C_OPTIONS_QUERY = (signed char)0xfa,

	// v1 equalizer (EqEbb). Verified against the MDR packet tables and Gadgetbridge.
	EQUALIZER_GET = (signed char)0x56,
	EQUALIZER_RET = (signed char)0x57,
	EQUALIZER_SET = (signed char)0x58,
	EQUALIZER_NOTIFY = (signed char)0x59,

	// v1 sound-quality / system commands (from Gadgetbridge SonyProtocolImplV1).
	AUDIO_UPSAMPLING_SET = (signed char)0xe8,    // DSEE / DSEE Extreme upscaling (sent as DATA_MDR)
	VOICE_NOTIFICATION_SET = (signed char)0x48,  // voice guidance toggle (sent as DATA_MDR_NO2)
	AUTO_POWER_OFF_SET = (signed char)0xf8       // shared system cmd; sub-byte 0x04 = auto power-off
};

// v1 equalizer preset ids. MANUAL/CUSTOM_* recall the user's saved band values.
enum class EQ_PRESET : signed char
{
	OFF = 0x00,
	BRIGHT = 0x10,
	EXCITED = 0x11,
	MELLOW = 0x12,
	RELAXED = 0x13,
	VOCAL = 0x14,
	TREBLE_BOOST = 0x15,
	BASS_BOOST = 0x16,
	SPEECH = 0x17,
	MANUAL = (signed char)0xa0,
	CUSTOM_1 = (signed char)0xa1,
	CUSTOM_2 = (signed char)0xa2
};

// v1 (legacy) battery, part of the POWER function. GET asks for the level; the headphones
// answer with RET, and also push NOTIFY frames unprompted when the level changes.
inline constexpr unsigned char BATTERY_LEVEL_GET = 0x10;
inline constexpr unsigned char BATTERY_LEVEL_RET = 0x11;
inline constexpr unsigned char BATTERY_LEVEL_NOTIFY = 0x13;

enum class VPT_PRESET_ID : signed char
{
	OFF = 0,
	OUTDOOR_FESTIVAL = 1,
	ARENA = 2,
	CONCERT_HALL = 3,
	CLUB = 4
	//Note: Sony reserved 5~15 "for future"
};

enum class SOUND_POSITION_PRESET : signed char
{
	OFF = 0,
	FRONT_LEFT = 1,
	FRONT_RIGHT = 2,
	FRONT = 3,
	REAR_LEFT = 17,
	REAR_RIGHT = 18,
	OUT_OF_RANGE = -1
};

// Display order for the spatial-position selector.
inline const SOUND_POSITION_PRESET SOUND_POSITION_PRESET_ARRAY[] = {
	SOUND_POSITION_PRESET::OFF,
	SOUND_POSITION_PRESET::FRONT_LEFT,
	SOUND_POSITION_PRESET::FRONT_RIGHT,
	SOUND_POSITION_PRESET::FRONT,
	SOUND_POSITION_PRESET::REAR_LEFT,
	SOUND_POSITION_PRESET::REAR_RIGHT,
	SOUND_POSITION_PRESET::OUT_OF_RANGE
};

enum class VPT_INQUIRED_TYPE : signed char
{
	NO_USE = 0,
	VPT = 1,
	SOUND_POSITION = 2,
	OUT_OF_RANGE = -1
};

enum class OPTIMIZER_STATE : signed char
{
	IDLE = 0,
	OPTIMIZING = 1
};

enum class S2C_TOGGLE : signed char
{
	ACTIVE = 1,
	INACTIVE = 0
};

// Auto power-off selectable options (v1). Each maps to a 2-byte code in the serializer.
// Note: the WH-1000XM3 exposes the timed options; the XM4 typically offers only OFF and
// WHEN_TAKEN_OFF. Unsupported options are simply ignored by the device.
enum class AUTO_POWER_OFF : signed char
{
	OFF = 0,            // never power off automatically
	AFTER_5_MIN = 1,
	AFTER_30_MIN = 2,
	AFTER_1_HOUR = 3,
	AFTER_3_HOUR = 4,
	WHEN_TAKEN_OFF = 5
};

enum class MULTI_POINT_COMMANDS : signed char
{
	CONNECT = (signed char)0x01,
	DISCONNECT = (signed char)0x00,
	UNPAIR = (signed char)0x02
};

// Bitset of device features, populated from the capability query reply.
// Kept for the original client; new code asks Headphones::functionState() about a FUNCTION_TYPE.
enum DEVICE_CAPABILITIES
{
	NC_ASM = 0x01 << 0,
	VPT = 0x01 << 1,
	MULTI_POINT = 0x01 << 2,
	OPTIMIZER = 0x01 << 3,
	SPEAK_TO_CHAT = 0x01 << 4
};

// Function IDs listed in the supported-functions reply (07). These name a feature the device
// advertises - they are not command bytes: F5 here means "Speak-to-Chat exists", not the F5
// SYSTEM_NTFY_STATUS command. Values from Sound Connect 13.3.0's FunctionType enum. The enum
// is app-wide; a headset advertises the subset it implements.
enum class FUNCTION_TYPE : signed char
{
	BATTERY_LEVEL = 0x11,
	UPSCALING_INDICATOR = 0x12,
	CODEC_INDICATOR = 0x13,
	DEVICE_MANAGEMENT = 0x38,                  // PAIRING_DEVICE_MANAGEMENT_CLASSIC_BT
	VOICE_GUIDANCE = 0x39,
	VPT = 0x41,
	SOUND_POSITION = 0x42,
	PRESET_EQ = 0x51,
	EBB = 0x52,
	PRESET_EQ_NONCUSTOMIZABLE = 0x53,
	NOISE_CANCELLING = 0x61,
	NC_ASM = 0x62,                             // NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE
	AMBIENT_SOUND_MODE = 0x63,
	OPTIMIZER = (signed char)0x81,             // NC_OPTIMIZER
	GENERAL_SETTING1 = (signed char)0xD1,
	GENERAL_SETTING2 = (signed char)0xD2,
	GENERAL_SETTING3 = (signed char)0xD3,
	CONNECTION_MODE = (signed char)0xE1,       // sound quality / connection priority
	UPSCALING = (signed char)0xE2,             // DSEE setting
	CONTROL_BY_WEARING = (signed char)0xF3,
	AUTO_POWER_OFF = (signed char)0xF4,
	SMART_TALKING_MODE = (signed char)0xF5,    // Speak-to-Chat
	ASSIGNABLE_SETTINGS = (signed char)0xF6
};

// ---------------------------------------------------------------------------------------
// Sony Tandem V1 (MDR) command bytes and selectors, recovered from Sound Connect 13.3.0 (see
// "Sony Sound Connect 13.3.0 - Protocol Findings" in the project notes).
//
// A command byte means nothing without its table: table 1 travels as DATA_MDR (0x0C), table 2
// as DATA_MDR_NO2 (0x0E), and the two reuse numbers - table-1 0x48 is VPT_SET_PARAM while
// table-2 0x48 is VOICE_GUIDANCE_SET_PARAM. Replies are therefore dispatched on
// (table, command, selector), never on the command byte alone.
//
// Evidence level: static serializers/decoders from the APK. Only the NC/ASM, battery, optimizer
// and paired-device paths have been exercised against a WH-1000XM4; see PROTOCOL.md.
// ---------------------------------------------------------------------------------------
namespace v1
{
	// Table 1 (DATA_MDR) commands.
	namespace t1
	{
		inline constexpr unsigned char CONNECT_GET_PROTOCOL_INFO = 0x00;
		inline constexpr unsigned char CONNECT_GET_SUPPORT_FUNCTION = 0x06;
		inline constexpr unsigned char CONNECT_RET_SUPPORT_FUNCTION = 0x07;
		inline constexpr unsigned char COMMON_GET_BATTERY_LEVEL = 0x10;
		inline constexpr unsigned char COMMON_RET_BATTERY_LEVEL = 0x11;
		inline constexpr unsigned char COMMON_NTFY_BATTERY_LEVEL = 0x13;
		inline constexpr unsigned char COMMON_RET_UPSCALING_EFFECT = 0x15;
		inline constexpr unsigned char COMMON_NTFY_UPSCALING_EFFECT = 0x17;
		inline constexpr unsigned char VPT_GET_PARAM = 0x46;
		inline constexpr unsigned char VPT_RET_PARAM = 0x47;
		inline constexpr unsigned char VPT_SET_PARAM = 0x48;
		inline constexpr unsigned char VPT_NTFY_PARAM = 0x49;
		inline constexpr unsigned char EQEBB_GET_PARAM = 0x56;
		inline constexpr unsigned char EQEBB_RET_PARAM = 0x57;
		inline constexpr unsigned char EQEBB_SET_PARAM = 0x58;
		inline constexpr unsigned char EQEBB_NTFY_PARAM = 0x59;
		inline constexpr unsigned char NCASM_GET_PARAM = 0x66;
		inline constexpr unsigned char NCASM_RET_PARAM = 0x67;
		inline constexpr unsigned char NCASM_SET_PARAM = 0x68;
		inline constexpr unsigned char NCASM_NTFY_PARAM = 0x69;
		inline constexpr unsigned char OPT_NTFY_STATUS = 0x85;
		inline constexpr unsigned char ALERT_SET_STATUS = 0x94;
		inline constexpr unsigned char ALERT_SET_PARAM = 0x98;
		inline constexpr unsigned char ALERT_NTFY_PARAM = 0x99;
		inline constexpr unsigned char GENERAL_SETTING_GET_CAPABILITY = 0xD0;
		inline constexpr unsigned char GENERAL_SETTING_RET_CAPABILITY = 0xD1;
		inline constexpr unsigned char GENERAL_SETTING_GET_STATUS = 0xD2;
		inline constexpr unsigned char GENERAL_SETTING_RET_STATUS = 0xD3;
		inline constexpr unsigned char GENERAL_SETTING_NTFY_STATUS = 0xD5;
		inline constexpr unsigned char GENERAL_SETTING_GET_PARAM = 0xD6;
		inline constexpr unsigned char GENERAL_SETTING_RET_PARAM = 0xD7;
		inline constexpr unsigned char GENERAL_SETTING_SET_PARAM = 0xD8;
		inline constexpr unsigned char GENERAL_SETTING_NTFY_PARAM = 0xD9;
		inline constexpr unsigned char AUDIO_GET_PARAM = 0xE6;
		inline constexpr unsigned char AUDIO_RET_PARAM = 0xE7;
		inline constexpr unsigned char AUDIO_SET_PARAM = 0xE8;
		inline constexpr unsigned char AUDIO_NTFY_PARAM = 0xE9;
		inline constexpr unsigned char SYSTEM_GET_CAPABILITY = 0xF0;
		inline constexpr unsigned char SYSTEM_RET_CAPABILITY = 0xF1;
		inline constexpr unsigned char SYSTEM_GET_STATUS = 0xF2;
		inline constexpr unsigned char SYSTEM_RET_STATUS = 0xF3;
		inline constexpr unsigned char SYSTEM_NTFY_STATUS = 0xF5;
		inline constexpr unsigned char SYSTEM_GET_PARAM = 0xF6;
		inline constexpr unsigned char SYSTEM_RET_PARAM = 0xF7;
		inline constexpr unsigned char SYSTEM_SET_PARAM = 0xF8;
		inline constexpr unsigned char SYSTEM_NTFY_PARAM = 0xF9;
		inline constexpr unsigned char SYSTEM_GET_EXTENDED_PARAM = 0xFA;
		inline constexpr unsigned char SYSTEM_RET_EXTENDED_PARAM = 0xFB;
		inline constexpr unsigned char SYSTEM_SET_EXTENDED_PARAM = 0xFC;
		inline constexpr unsigned char SYSTEM_NTFY_EXTENDED_PARAM = 0xFD;
	}

	// Table 2 (DATA_MDR_NO2) commands.
	namespace t2
	{
		inline constexpr unsigned char PERIPHERAL_GET_PARAM = 0x36;
		inline constexpr unsigned char PERIPHERAL_RET_PARAM = 0x37;
		inline constexpr unsigned char PERIPHERAL_NTFY_PARAM = 0x39;
		inline constexpr unsigned char VOICE_GUIDANCE_GET_PARAM = 0x46;
		inline constexpr unsigned char VOICE_GUIDANCE_RET_PARAM = 0x47;
		inline constexpr unsigned char VOICE_GUIDANCE_SET_PARAM = 0x48;
		inline constexpr unsigned char VOICE_GUIDANCE_NTFY_PARAM = 0x49;
	}

	// Selectors: the inquired-type byte that follows a command.
	inline constexpr unsigned char SUPPORT_FUNCTION_INQUIRY = 0x01;   // 06 01 -> 07 01 count ids...
	inline constexpr unsigned char BATTERY_SINGLE = 0x00;
	inline constexpr unsigned char NC_OPTIMIZER = 0x01;
	inline constexpr unsigned char EQ_PRESET = 0x01;
	inline constexpr unsigned char AUDIO_CONNECTION_MODE = 0x01;
	inline constexpr unsigned char AUDIO_UPSCALING = 0x02;
	inline constexpr unsigned char SYSTEM_CONTROL_BY_WEARING = 0x03; // from the KMP repository, not the legacy enum
	inline constexpr unsigned char SYSTEM_AUTO_POWER_OFF = 0x04;
	inline constexpr unsigned char SYSTEM_SMART_TALKING = 0x05;
	inline constexpr unsigned char SYSTEM_ASSIGNABLE = 0x06;
	inline constexpr unsigned char ALERT_FIXED_MESSAGE = 0x01;
	inline constexpr unsigned char VOICE_GUIDANCE_SETTING = 0x01;
	inline constexpr unsigned char VOICE_GUIDANCE_DETAIL_ON_OFF = 0x01;
	inline constexpr unsigned char PERIPHERAL_CLASSIC_BT = 0x01;

	// General settings. D1/D2/D3 are slots negotiated per device: a feature is found by the
	// title its capability reports, never by assuming which slot it occupies.
	inline constexpr unsigned char GS_SLOT_1 = 0xD1;
	inline constexpr unsigned char GS_SLOT_2 = 0xD2;
	inline constexpr unsigned char GS_SLOT_3 = 0xD3;
	inline constexpr unsigned char GS_LANGUAGE_ENGLISH = 0x01;
	inline constexpr unsigned char GS_STRING_FORMAT_ENUM_NAME = 0x02;
	inline constexpr unsigned char GS_TYPE_BOOLEAN = 0x01;
	inline constexpr unsigned char GS_TYPE_LIST = 0x02;
	inline constexpr size_t GS_MAX_STRING_BYTES = 128;
	inline constexpr const char* GS_TITLE_TOUCH_PANEL = "TOUCH_PANEL_SETTING";
	inline constexpr const char* GS_TITLE_MULTIPOINT = "MULTIPOINT_SETTING";

	inline constexpr bool isGeneralSettingSlot(unsigned char slot)
	{
		return slot == GS_SLOT_1 || slot == GS_SLOT_2 || slot == GS_SLOT_3;
	}

	// Two unrelated "on/off type" bytes that happen to look alike - never reuse one for the other:
	// general settings say BOOLEAN_TYPE (01), wearing control says ProtocolSettingType.ON_OFF (00).
	inline constexpr unsigned char PROTOCOL_SETTING_TYPE_ON_OFF = 0x00;

	// Availability (CommonStatus). The polarity is the opposite of an on/off value: 00 = available.
	inline constexpr unsigned char STATUS_ENABLE = 0x00;
	inline constexpr unsigned char STATUS_DISABLE = 0x01;

	// Speak-to-Chat. The RET and the NTFY of the same toggle use different sub-type enums.
	inline constexpr unsigned char S2C_SETTING_TYPE_ON_OFF = 0x00;   // F7 05 00 v
	inline constexpr unsigned char S2C_PARAM_MODE_ON_OFF = 0x01;     // F8/F9 05 01 v
	inline constexpr unsigned char S2C_PARAM_PREVIEW_ON_OFF = 0x02;  // F9 05 02 v - a preview, not the setting
	inline constexpr unsigned char S2C_DETAIL_TYPE_1 = 0x00;         // FB/FC/FD 05 00 ...

	inline constexpr unsigned char UPSCALING_SETTING_AUTO_OFF = 0x00; // E7/E8/E9 02 00 v

	// Auto power-off: one parameter type carrying two element IDs.
	inline constexpr unsigned char APO_ACTIVE_AND_SELECT_TIME = 0x01;
	inline constexpr unsigned char APO_ELEMENT_5_MIN = 0x00;
	inline constexpr unsigned char APO_ELEMENT_30_MIN = 0x01;
	inline constexpr unsigned char APO_ELEMENT_60_MIN = 0x02;
	inline constexpr unsigned char APO_ELEMENT_180_MIN = 0x03;
	inline constexpr unsigned char APO_ELEMENT_WHEN_REMOVED = 0x10;
	inline constexpr unsigned char APO_ELEMENT_DISABLED = 0x11;

	inline constexpr bool isAutoPowerOffElement(unsigned char e)
	{
		return e <= APO_ELEMENT_180_MIN || e == APO_ELEMENT_WHEN_REMOVED || e == APO_ELEMENT_DISABLED;
	}
	inline constexpr bool isTimedAutoPowerOffElement(unsigned char e)
	{
		return e <= APO_ELEMENT_180_MIN;
	}

	// Fixed-message alerts (94/98/99 01 ...).
	inline constexpr unsigned char ALERT_MSG_MULTIPOINT_TO_ON = 0x06;   // Sony shows its LDAC-disable caution
	inline constexpr unsigned char ALERT_MSG_MULTIPOINT_TO_OFF = 0x07;
	inline constexpr unsigned char ALERT_ACTION_CONFIRMATION_ONLY = 0x00;
	inline constexpr unsigned char ALERT_ACTION_POSITIVE_NEGATIVE = 0x01;
	inline constexpr unsigned char ALERT_RESPONSE_NEGATIVE = 0x00;
	inline constexpr unsigned char ALERT_RESPONSE_POSITIVE = 0x01;
}
