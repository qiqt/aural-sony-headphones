#pragma once

#include <vector>
#include <string>

inline constexpr auto MAX_BLUETOOTH_MESSAGE_SIZE = 2048;
inline constexpr char START_MARKER{ 62 };
inline constexpr char END_MARKER{ 60 };

inline constexpr auto MAC_ADDR_STR_SIZE = 17;

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
inline unsigned char SERVICE_UUID_IN_BYTES[] = { // this is the SERVICE_UUID but in bytes
	0x96, 0xcc, 0x20, 0x3e, 0x50, 0x68, 0x46, 0xad,
	0xb3, 0x2d, 0xe3, 0x16, 0xf5, 0xe0, 0x69, 0xba
};

// V2 service UUID (WH-1000XM5 and newer)
inline constexpr auto SERVICE_UUID_V2 = "956C7B26-D49A-4BA8-B03F-B17D393CB6E2";
inline unsigned char SERVICE_UUID_V2_IN_BYTES[] = {
	0x95, 0x6C, 0x7B, 0x26, 0xD4, 0x9A, 0x4B, 0xA8,
	0xB0, 0x3F, 0xB1, 0x7D, 0x39, 0x3C, 0xB6, 0xE2
};

inline const char* serviceUuidString(SonyProtocol protocol)
{
	return protocol == SonyProtocol::V2 ? SERVICE_UUID_V2 : SERVICE_UUID;
}

inline unsigned char* serviceUuidBytes(SonyProtocol protocol)
{
	return protocol == SonyProtocol::V2 ? SERVICE_UUID_V2_IN_BYTES : SERVICE_UUID_IN_BYTES;
}

// Decide which protocol a device speaks from its Bluetooth name. XM5/XM6 are V2;
// everything else defaults to V1 (WH-1000XM3/XM4, MDR-*, ...). Note: the WF-1000XM4/
// XM5 earbuds are actually V2, but this client targets the over-ear WH series.
inline SonyProtocol protocolForDeviceName(const std::string& name)
{
	if (name.find("XM5") != std::string::npos || name.find("XM6") != std::string::npos)
		return SonyProtocol::V2;
	return SonyProtocol::V1;
}

#define APP_NAME "Sony Headphones App v" __HEADPHONES_APP_VERSION__
#define APP_NAME_W (L"" APP_NAME)

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
	NCASM_SET_PARAM = 104,

	// v1 feature command IDs (proven on XM4, from PR #103)
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

//Needed for converting the ImGui Combo index into the VPT index.
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
enum DEVICE_CAPABILITIES
{
	NC_ASM = 0x01 << 0,
	VPT = 0x01 << 1,
	MULTI_POINT = 0x01 << 2,
	OPTIMIZER = 0x01 << 3,
	SPEAK_TO_CHAT = 0x01 << 4
};

// Function-type bytes as reported inside the capability query reply.
enum class FUNCTION_TYPE : signed char
{
	DEVICE_MANAGEMENT = 56,
	VPT = 65,
	OPTIMIZER = -127,
	NC_ASM = 98,
	SMART_TALKING_MODE = -11
};
