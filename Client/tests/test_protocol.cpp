// Payload builders and bounded decoders, driven by the packet vectors of the analyst-written
// Python reference (protocol-reference/test_xm4_remaining.py) plus the malformed cases it
// exercises. Everything here is static protocol evidence from Sound Connect 13.3.0 - these tests
// prove the C++ matches that evidence, not that a headset accepts it.

#include "TestSupport.h"

#include <set>

using namespace CommandSerializer;
using replies::hex;

namespace {

Message msg(const std::string& payloadHex, DATA_TYPE table = DATA_TYPE::DATA_MDR)
{
	return Message{ table, 0, hex(payloadHex) };
}

Message msg(const Buffer& payload, DATA_TYPE table = DATA_TYPE::DATA_MDR)
{
	return Message{ table, 0, payload };
}

std::set<unsigned char> asSet(const std::vector<unsigned char>& ids)
{
	return std::set<unsigned char>(ids.begin(), ids.end());
}

const unsigned char SLOTS[] = { v1::GS_SLOT_1, v1::GS_SLOT_2, v1::GS_SLOT_3 };
const char* TITLES[] = { v1::GS_TITLE_TOUCH_PANEL, v1::GS_TITLE_MULTIPOINT };

} // namespace

// ---- supported functions (07) ------------------------------------------------------------------

TEST(protocol_supported_functions_honour_the_declared_count)
{
	auto ids = parseSupportedFunctions(msg("07 01 03 D1 D3 F3"));
	CHECK(ids.has_value());
	CHECK(asSet(*ids) == (std::set<unsigned char>{ 0xD1, 0xD3, 0xF3 }));

	// The count says three, two follow.
	CHECK(!parseSupportedFunctions(msg("07 01 03 D1 D3")).has_value());
	// The count says two, three follow.
	CHECK(!parseSupportedFunctions(msg("07 01 02 D1 D3 F3")).has_value());
	// Nothing after the header.
	CHECK(!parseSupportedFunctions(msg("07 01")).has_value());
}

TEST(protocol_supported_function_headers_are_not_function_ids)
{
	// The old parser scanned every byte, so the command, inquiry and count all "advertised"
	// themselves. A count of 0x13 used to switch on CODEC_INDICATOR (0x13).
	auto empty = parseSupportedFunctions(msg("07 01 00"));
	CHECK(empty.has_value());
	CHECK(empty->empty());

	Buffer nineteen = hex("07 01 13");
	for (int i = 0; i < 19; ++i)
		nineteen.push_back(static_cast<char>(0x20 + i));   // none of them is 0x07, 0x01 or 0x13
	auto ids = parseSupportedFunctions(msg(nineteen));
	CHECK(ids.has_value());
	CHECK_EQ(ids->size(), size_t(19));
	const auto set = asSet(*ids);
	CHECK(set.count(0x07) == 0 && set.count(0x01) == 0 && set.count(0x13) == 0);
}

TEST(protocol_supported_functions_need_table_1_and_the_inquiry_we_sent)
{
	CHECK(!parseSupportedFunctions(msg("07 01 01 62", DATA_TYPE::DATA_MDR_NO2)).has_value());
	CHECK(!parseSupportedFunctions(msg("07 02 01 62")).has_value());
	CHECK(!parseSupportedFunctions(msg("06 01 01 62")).has_value());
	CHECK_EQ(toHex(serializeSupportedFunctionsQuery()), std::string("06 01"));
}

// ---- general-setting capability (D1) -----------------------------------------------------------

TEST(protocol_general_setting_slots_are_discovered_not_fixed)
{
	for (unsigned char slot : SLOTS)
	{
		for (const char* title : TITLES)
		{
			const Buffer p = replies::generalSettingCapability(slot, title, "MULTIPOINT_SETTING_SUMMARY");
			auto cap = parseGeneralSettingCapability(msg(p));
			CHECK(cap.has_value());
			CHECK_EQ(int(cap->slot), int(slot));
			CHECK(namesGeneralSetting(*cap, title));
			CHECK(isBooleanGeneralSetting(*cap));
			CHECK_EQ(cap->summary, std::string("MULTIPOINT_SETTING_SUMMARY"));

			// Every strict prefix is a truncated record and decodes to nothing.
			for (size_t cut = 0; cut < p.size(); ++cut)
			{
				Buffer truncated(p.begin(), p.begin() + cut);
				CHECK_MSG(!parseGeneralSettingCapability(msg(truncated)).has_value(),
					"a capability record truncated to " + std::to_string(cut) + " bytes must not decode");
			}
		}
	}
}

TEST(protocol_general_setting_capability_validation)
{
	const Buffer valid = replies::generalSettingCapability(0xD2, v1::GS_TITLE_TOUCH_PANEL);

	// Trailing data after a boolean type: not a plain boolean.
	Buffer trailing = valid;
	trailing.push_back(0x00);
	auto withTrailing = parseGeneralSettingCapability(msg(trailing));
	CHECK(withTrailing.has_value());
	CHECK(!isBooleanGeneralSetting(*withTrailing));

	// LIST_TYPE: an index, never coerced to a bool.
	Buffer list = valid;
	list.back() = static_cast<char>(v1::GS_TYPE_LIST);
	auto asList = parseGeneralSettingCapability(msg(list));
	CHECK(asList.has_value());
	CHECK(!isBooleanGeneralSetting(*asList));

	// A title nobody maps to a feature.
	auto unknown = parseGeneralSettingCapability(msg(replies::generalSettingCapability(0xD2, "UNKNOWN")));
	CHECK(unknown.has_value());
	CHECK(!namesGeneralSetting(*unknown, v1::GS_TITLE_TOUCH_PANEL));
	CHECK(!namesGeneralSetting(*unknown, v1::GS_TITLE_MULTIPOINT));

	// A title length beyond the 128-byte limit.
	CHECK(!parseGeneralSettingCapability(msg("D1 D2 02 FF 00 01")).has_value());

	// RAW_NAME format: the bytes may spell the title, but Sony only matches ENUM_NAME.
	Buffer raw = valid;
	raw[2] = 0x01;
	auto rawName = parseGeneralSettingCapability(msg(raw));
	CHECK(rawName.has_value());
	CHECK(!namesGeneralSetting(*rawName, v1::GS_TITLE_TOUCH_PANEL));

	// Not a general-setting slot, the wrong table, invalid UTF-8.
	CHECK(!parseGeneralSettingCapability(msg(replies::generalSettingCapability(0xD4, v1::GS_TITLE_TOUCH_PANEL))).has_value());
	CHECK(!parseGeneralSettingCapability(msg(valid, DATA_TYPE::DATA_MDR_NO2)).has_value());
	CHECK(!parseGeneralSettingCapability(msg("D1 D1 02 02 C3 28 00 01")).has_value());

	// A zero-length summary is valid.
	auto noSummary = parseGeneralSettingCapability(msg(valid));
	CHECK(noSummary.has_value());
	CHECK(noSummary->summary.empty());
}

TEST(protocol_general_setting_query_builders)
{
	CHECK_EQ(toHex(serializeGeneralSettingCapabilityQuery(0xD3)), std::string("D0 D3 01"));
	CHECK_EQ(toHex(serializeGeneralSettingStatusQuery(0xD2)), std::string("D2 D2"));
	CHECK_EQ(toHex(serializeGeneralSettingQuery(0xD1)), std::string("D6 D1"));
}

// ---- availability and value ---------------------------------------------------------------------

TEST(protocol_availability_is_not_enabled)
{
	// Availability: 00 means available - the opposite polarity of an on/off value.
	struct Case { const char* prefix; unsigned char selector; };
	for (const Case& c : { Case{ "F3 03", 0x03 }, Case{ "F5 03", 0x03 } })
	{
		auto available = parseWearingAvailability(msg(std::string(c.prefix) + " 00"));
		auto unavailable = parseWearingAvailability(msg(std::string(c.prefix) + " 01"));
		CHECK(available.has_value() && available->available);
		CHECK(unavailable.has_value() && !unavailable->available);
	}
	for (const char* prefix : { "D3 D1", "D5 D1" })
	{
		auto available = parseGeneralSettingAvailability(msg(std::string(prefix) + " 00"));
		auto unavailable = parseGeneralSettingAvailability(msg(std::string(prefix) + " 01"));
		CHECK(available.has_value() && available->available && available->selector == 0xD1);
		CHECK(unavailable.has_value() && !unavailable->available);
	}
	for (const char* text : { "D7 D1 01 00", "D9 D1 01 00" })
	{
		auto value = parseGeneralSettingBool(msg(text));
		CHECK(value.has_value() && !value->enabled && value->selector == 0xD1);
	}
	auto on = parseGeneralSettingBool(msg("D9 D3 01 01"));
	CHECK(on.has_value() && on->enabled);

	// Too short, out-of-range values, someone else's selector, a list-type value, a long frame.
	CHECK(!parseWearingSetting(msg("F7 03 00")).has_value());
	CHECK(!parseWearingSetting(msg("F7 03 00 02")).has_value());
	CHECK(!parseWearingSetting(msg("F7 04 00 01")).has_value());
	CHECK(!parseGeneralSettingBool(msg("D7 D1 02 01")).has_value());
	CHECK(!parseGeneralSettingAvailability(msg("D3 D1")).has_value());
	CHECK(!parseGeneralSettingAvailability(msg("D5 D1 00 00")).has_value());
	CHECK(!parseGeneralSettingAvailability(msg("D3 D1 02")).has_value());
	CHECK(!parseGeneralSettingAvailability(msg("D3 D4 00")).has_value());

	// Right bytes, wrong table.
	CHECK(!parseWearingSetting(msg("F7 03 00 01", DATA_TYPE::DATA_MDR_NO2)).has_value());
	CHECK(!parseGeneralSettingBool(msg("D7 D1 01 01", DATA_TYPE::DATA_MDR_NO2)).has_value());
}

TEST(protocol_wearing_vectors)
{
	for (const char* cmd : { "F7", "F9" })
	{
		auto on = parseWearingSetting(msg(std::string(cmd) + " 03 00 01"));
		CHECK(on.has_value() && on->enabled && on->selector == 0x03);
		// The wearing type byte is ProtocolSettingType.ON_OFF (00), not BOOLEAN_TYPE (01).
		CHECK(!parseWearingSetting(msg(std::string(cmd) + " 03 01 01")).has_value());
	}
	auto onOffType = parseWearingCapability(msg("F1 03 00"));
	CHECK(onOffType.has_value() && *onOffType == v1::PROTOCOL_SETTING_TYPE_ON_OFF);
	CHECK(!parseWearingCapability(msg("F1 04 00")).has_value());
	CHECK(!parseWearingCapability(msg("F1 03")).has_value());
	CHECK(!parseWearingCapability(msg("F1 03 00 00")).has_value());

	CHECK_EQ(toHex(serializeSystemCapabilityQuery(0x03)), std::string("F0 03"));
	CHECK_EQ(toHex(serializeSystemStatusQuery(0x03)), std::string("F2 03"));
	CHECK_EQ(toHex(serializeSystemQuery(0x03)), std::string("F6 03"));
}

// ---- existing replies, now bounded ------------------------------------------------------------

TEST(protocol_nc_asm_report_keeps_every_field)
{
	auto r = parseNcAsm(msg("69 02 01 02 01 01 01 0A"));
	CHECK(r.has_value());
	CHECK_EQ(int(r->effect), 0x01);
	CHECK_EQ(int(r->ncSettingType), 0x02);
	CHECK_EQ(int(r->ncValue), 0x01);
	CHECK_EQ(int(r->asmSettingType), 0x01);
	CHECK_EQ(int(r->asmId), 0x01);
	CHECK_EQ(int(r->asmLevel), 10);

	CHECK(!parseNcAsm(msg("67 02 01 02 01 01 01")).has_value());       // short
	CHECK(!parseNcAsm(msg("67 02 01 02 01 01 01 0A 00")).has_value()); // long
	CHECK(!parseNcAsm(msg("67 01 01 02 01 01 01 0A")).has_value());    // NC-only inquiry
	CHECK(!parseNcAsm(msg("67 02 05 02 01 01 01 0A")).has_value());    // unknown effect
	CHECK(!parseNcAsm(msg("67 02 01 03 01 01 01 0A")).has_value());    // unknown NC type
	CHECK(!parseNcAsm(msg("67 02 01 00 02 01 01 0A")).has_value());    // on/off type, value 02
	CHECK(!parseNcAsm(msg("67 02 01 02 01 01 02 0A")).has_value());    // unknown ASM id
	CHECK(!parseNcAsm(msg("67 02 01 02 01 01 01 0A", DATA_TYPE::DATA_MDR_NO2)).has_value());
}

TEST(protocol_battery_decodes_the_single_battery_only)
{
	auto b = parseBattery(msg("11 00 4B 01"));
	CHECK(b.has_value() && b->level == 75 && b->charging);
	auto unknownCharge = parseBattery(msg("13 00 4B F0"));     // F0 is "unknown", not charging
	CHECK(unknownCharge.has_value() && !unknownCharge->charging);
	CHECK(!parseBattery(msg("11 01 4B 00 50 00")).has_value()); // left/right layout
	CHECK(!parseBattery(msg("11 00 65 00")).has_value());       // 101%
	CHECK(!parseBattery(msg("11 00 4B")).has_value());
	CHECK(!parseBattery(msg("11 00 4B 00", DATA_TYPE::DATA_MDR_NO2)).has_value());
}

TEST(protocol_paired_devices_are_table_2_only)
{
	Buffer reply = hex("37 01 01");
	const std::string mac = "AA:BB:CC:DD:EE:FF";
	reply.insert(reply.end(), mac.begin(), mac.end());
	reply.push_back(0x01);
	reply.push_back(0x05);
	for (char c : std::string("Phone"))
		reply.push_back(c);

	auto devices = parsePairedDevices(msg(reply, DATA_TYPE::DATA_MDR_NO2));
	CHECK(devices.has_value());
	CHECK_EQ(devices->size(), size_t(1));
	CHECK_EQ(devices->at(0).name, std::string("Phone"));
	CHECK_EQ(int(devices->at(0).slot), 1);

	// Table-1 0x37 is UPDT_RET_PARAM (firmware update), not a device list.
	CHECK(!parsePairedDevices(msg(reply, DATA_TYPE::DATA_MDR)).has_value());
	// A name running past the end of the frame.
	Buffer truncated(reply.begin(), reply.end() - 1);
	CHECK(!parsePairedDevices(msg(truncated, DATA_TYPE::DATA_MDR_NO2)).has_value());
}

// ---- framing ------------------------------------------------------------------------------------

TEST(protocol_frame_size_must_match_the_declared_size)
{
	const Buffer framed = packageDataForBt(hex("11 00 4B 00"), DATA_TYPE::DATA_MDR, 0);
	// Strip the markers to get what unpackBtMessage sees.
	const Buffer inner(framed.begin() + 1, framed.end() - 1);
	auto ok = unpackBtMessage(inner);
	CHECK_EQ(toHex(ok.messageBytes), std::string("11 00 4B 00"));

	// Same bytes, but the size field claims one byte more (or less) than the frame holds. The
	// checksum is recomputed so only the size is wrong.
	for (int delta : { +1, -1, +256 })
	{
		Buffer raw = { static_cast<char>(DATA_TYPE::DATA_MDR), 0 };
		const unsigned int size = 4 + delta;
		std::vector<unsigned char> sizeBytes{static_cast<unsigned char>(size >> 24), static_cast<unsigned char>(size >> 16), static_cast<unsigned char>(size >> 8), static_cast<unsigned char>(size)};
		raw.insert(raw.end(), sizeBytes.begin(), sizeBytes.end());
		const Buffer payload = hex("11 00 4B 00");
		raw.insert(raw.end(), payload.begin(), payload.end());
		raw.push_back(static_cast<char>(_sumChecksum(raw)));
		CHECK_THROWS(unpackBtMessage(_escapeSpecials(raw)), RecoverableException);
	}
}
