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

	// A byte after the boolean type is still a boolean. The Python reference rejects it, but a
	// WH-1000XM4 sends exactly that (01 00) for both of its boolean settings; see the capture test.
	Buffer trailing = valid;
	trailing.push_back(0x00);
	auto withTrailing = parseGeneralSettingCapability(msg(trailing));
	CHECK(withTrailing.has_value());
	CHECK(isBooleanGeneralSetting(*withTrailing));
	CHECK_EQ(withTrailing->typeSpecificBytes, size_t(1));

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

TEST(protocol_general_setting_write_goes_to_the_discovered_slot)
{
	// set_general: D8 slot 01(BOOLEAN_TYPE) value, for whichever slot the capability named.
	for (unsigned char slot : SLOTS)
	{
		const std::string s = toHex(Buffer{ static_cast<char>(slot) });
		CHECK_EQ(toHex(serializeGeneralSettingBool(slot, true)), "D8 " + s + " 01 01");
		CHECK_EQ(toHex(serializeGeneralSettingBool(slot, false)), "D8 " + s + " 01 00");
	}
	// Without a discovered slot there is nothing to write to; the builder refuses to guess.
	const unsigned char notSlots[] = { 0x00, 0xD0, 0xD4, 0xFF };
	for (unsigned char notASlot : notSlots)
		CHECK_THROWS(serializeGeneralSettingBool(notASlot, true), std::invalid_argument);
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

	// set_wearing: F8 03 00(ON_OFF) value.
	CHECK_EQ(toHex(serializeWearingControl(true)), std::string("F8 03 00 01"));
	CHECK_EQ(toHex(serializeWearingControl(false)), std::string("F8 03 00 00"));
}

// ---- replies captured from hardware -------------------------------------------------------------

// Every reply below was captured from a WH-1000XM4 by the Aural protocol trace (AURAL_TRACE) on
// 2026-10-31; the firmware version was not recorded. Unlike the rest of this file, this is
// hardware evidence: each reply must decode exactly as shown.
TEST(protocol_wh1000xm4_captured_replies_decode)
{
	auto ids = parseSupportedFunctions(msg("07 01 17 71 38 62 F5 81 51 A1 E1 E2 D2 F6 D1 F4 F3 39 12 13 11 30 C1 14 22 21"));
	CHECK(ids.has_value());
	CHECK_EQ(ids->size(), size_t(23));
	const std::set<unsigned char> advertised = asSet(*ids);
	CHECK(advertised.count(0x62) && advertised.count(0xD1) && advertised.count(0xD2) && advertised.count(0xF3));
	CHECK_MSG(!advertised.count(0x41) && !advertised.count(0x42), "the XM4 has neither VPT nor sound position");
	CHECK(!advertised.count(0xD3));

	// Both boolean records carry one byte after the type.
	auto touch = parseGeneralSettingCapability(msg(
		"D1 D1 02 13 54 4F 55 43 48 5F 50 41 4E 45 4C 5F 53 45 54 54 49 4E 47 00 01 00"));
	CHECK(touch.has_value());
	CHECK(namesGeneralSetting(*touch, v1::GS_TITLE_TOUCH_PANEL));
	CHECK(isBooleanGeneralSetting(*touch));
	CHECK(touch->summary.empty());
	auto multipoint = parseGeneralSettingCapability(msg(
		"D1 D2 02 12 4D 55 4C 54 49 50 4F 49 4E 54 5F 53 45 54 54 49 4E 47 1A 4D 55 4C 54 49 50 4F 49 4E 54 5F "
		"53 45 54 54 49 4E 47 5F 53 55 4D 4D 41 52 59 01 00"));
	CHECK(multipoint.has_value());
	CHECK(namesGeneralSetting(*multipoint, v1::GS_TITLE_MULTIPOINT));
	CHECK(isBooleanGeneralSetting(*multipoint));
	CHECK_EQ(multipoint->summary, std::string("MULTIPOINT_SETTING_SUMMARY"));

	auto battery = parseBattery(msg("11 00 28 00"));
	CHECK(battery.has_value() && battery->level == 40 && !battery->charging);

	// NC/ASM: the client writes the level-adjustment type; the XM4 reports dual/single/off.
	auto nc = parseNcAsm(msg("67 02 01 02 02 01 00 00"));
	CHECK(nc.has_value() && ambientModeOf(*nc) == AmbientMode::NoiseCancelling);
	auto ambient = parseNcAsm(msg("69 02 01 02 00 01 00 0A"));
	CHECK(ambient.has_value() && ambientModeOf(*ambient) == AmbientMode::AmbientSound && ambient->asmLevel == 10);
	auto off = parseNcAsm(msg("69 02 00 02 02 01 00 00"));
	CHECK(off.has_value() && ambientModeOf(*off) == AmbientMode::Off);

	auto eq = parseEqualizer(msg("57 01 11 06 12 09 0B 0A 0D 0F"));
	CHECK(eq.has_value() && eq->preset == 0x11 && eq->levels.size() == 6);

	auto s2cOff = parseSpeakToChatToggle(msg("F7 05 00 00"));
	CHECK(s2cOff.has_value() && !s2cOff->enabled && !s2cOff->preview);
	auto s2cOn = parseSpeakToChatToggle(msg("F9 05 01 01"));
	CHECK(s2cOn.has_value() && s2cOn->enabled && !s2cOn->preview);
	auto preview = parseSpeakToChatToggle(msg("F9 05 02 00"));
	CHECK(preview.has_value() && preview->preview);
	CHECK(parseSpeakToChatDetails(msg("FB 05 00 00 00 00")).has_value());

	auto dseeOff = parseUpscaling(msg("E7 02 00 00"));
	CHECK(dseeOff.has_value() && !*dseeOff);
	auto dseeOn = parseUpscaling(msg("E9 02 00 01"));
	CHECK(dseeOn.has_value() && *dseeOn);

	auto guidance = parseVoiceGuidance(msg("47 01 01 01", DATA_TYPE::DATA_MDR_NO2));
	CHECK(guidance.has_value() && *guidance);

	auto powerOff = parseAutoPowerOff(msg("F7 04 01 10 00"));
	CHECK(powerOff.has_value() && powerOff->activeElement == v1::APO_ELEMENT_WHEN_REMOVED);

	auto wearingType = parseWearingCapability(msg("F1 03 00"));
	CHECK(wearingType.has_value() && *wearingType == v1::PROTOCOL_SETTING_TYPE_ON_OFF);
	auto wearingAvailable = parseWearingAvailability(msg("F3 03 00"));
	CHECK(wearingAvailable.has_value() && wearingAvailable->available);
	auto wearingOn = parseWearingSetting(msg("F7 03 00 01"));
	CHECK(wearingOn.has_value() && wearingOn->enabled);
}

// The second WH-1000XM4 capture: the touch panel, wearing control and multipoint writes.
TEST(protocol_wh1000xm4_captured_control_writes)
{
	// What the client sent, byte for byte.
	CHECK_EQ(toHex(serializeGeneralSettingBool(0xD1, false)), std::string("D8 D1 01 00"));
	CHECK_EQ(toHex(serializeGeneralSettingBool(0xD1, true)), std::string("D8 D1 01 01"));
	CHECK_EQ(toHex(serializeWearingControl(false)), std::string("F8 03 00 00"));
	CHECK_EQ(toHex(serializeWearingControl(true)), std::string("F8 03 00 01"));
	CHECK_EQ(toHex(serializeAlertEnable()), std::string("94 01 00"));
	CHECK_EQ(toHex(serializeGeneralSettingBool(0xD2, false)), std::string("D8 D2 01 00"));
	CHECK_EQ(toHex(serializeMultipointAlertResponse(0x07, false)), std::string("98 01 07 00"));

	// What the headphones answered.
	for (const char* text : { "D3 D1 00", "D3 D2 00" })
	{
		auto available = parseGeneralSettingAvailability(msg(text));
		CHECK_MSG(available.has_value() && available->available, std::string("available: ") + text);
	}
	auto touchOn = parseGeneralSettingBool(msg("D7 D1 01 01"));
	CHECK(touchOn.has_value() && touchOn->selector == 0xD1 && touchOn->enabled);
	auto touchOff = parseGeneralSettingBool(msg("D9 D1 01 00"));
	CHECK(touchOff.has_value() && touchOff->selector == 0xD1 && !touchOff->enabled);
	auto multipointOn = parseGeneralSettingBool(msg("D9 D2 01 01"));
	CHECK(multipointOn.has_value() && multipointOn->selector == 0xD2 && multipointOn->enabled);
	auto wearingOff = parseWearingSetting(msg("F9 03 00 00"));
	CHECK(wearingOff.has_value() && !wearingOff->enabled);

	// Turning multipoint off asks for accept or cancel.
	auto alert = parseMultipointAlert(msg("99 01 07 01"));
	CHECK(alert.has_value() && alert->messageId == v1::ALERT_MSG_MULTIPOINT_TO_OFF && alert->actionType == 0x01);
	CHECK_EQ(toHex(serializeMultipointAlertResponse(0x07, true)), std::string("98 01 07 01"));

	// After accepting, the XM4 reconnects with multipoint off and lists 22 functions:
	// paired-device management (38) is gone.
	auto afterwards = parseSupportedFunctions(msg("07 01 16 71 62 F5 81 51 A1 E1 E2 D2 F6 D1 F4 F3 39 12 13 11 30 C1 14 22 21"));
	CHECK(afterwards.has_value() && afterwards->size() == 22);
	CHECK(!asSet(*afterwards).count(0x38));
	auto multipointOff = parseGeneralSettingBool(msg("D7 D2 01 00"));
	CHECK(multipointOff.has_value() && multipointOff->selector == 0xD2 && !multipointOff->enabled);
}

// ---- fixed-message alerts ------------------------------------------------------------------------

TEST(protocol_multipoint_alert_vectors)
{
	// multipoint_alert / answer_multipoint_alert in the reference.
	auto on = parseMultipointAlert(msg("99 01 06 01"));
	CHECK(on.has_value() && on->messageId == 0x06 && on->actionType == 0x01);
	auto off = parseMultipointAlert(msg("99 01 07 00"));
	CHECK(off.has_value() && off->messageId == 0x07 && off->actionType == 0x00);

	CHECK_EQ(toHex(serializeMultipointAlertResponse(0x06, true)), std::string("98 01 06 01"));
	CHECK_EQ(toHex(serializeMultipointAlertResponse(0x07, false)), std::string("98 01 07 00"));
	CHECK_EQ(toHex(serializeAlertEnable()), std::string("94 01 00"));

	// Another message, another inquiry, short, an unknown action, long, our own answer echoed.
	for (const char* text : { "99 01 08 01", "99 02 06 01", "99 01 06", "99 01 06 02", "99 01 06 01 00", "98 01 06 01" })
		CHECK_MSG(!parseMultipointAlert(msg(text)).has_value(), std::string("must not decode: ") + text);
	CHECK(!parseMultipointAlert(msg("99 01 06 01", DATA_TYPE::DATA_MDR_NO2)).has_value());

	// There is no answer to compose for a message that is not a multipoint alert.
	const unsigned char notMultipoint[] = { 0x00, 0x05, 0x08, 0xFF };
	for (unsigned char id : notMultipoint)
		CHECK_THROWS(serializeMultipointAlertResponse(id, true), std::invalid_argument);
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
		for (int shift : {24, 16, 8, 0}) raw.push_back(static_cast<char>(size >> shift));
		const Buffer payload = hex("11 00 4B 00");
		raw.insert(raw.end(), payload.begin(), payload.end());
		raw.push_back(static_cast<char>(_sumChecksum(raw)));
		CHECK_THROWS(unpackBtMessage(_escapeSpecials(raw)), RecoverableException);
	}
}
