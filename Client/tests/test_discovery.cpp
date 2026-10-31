// Capability discovery and reply dispatch, end to end through the real transport: the reply
// arrives on the reader thread, the follow-up requests it causes are queued, and only the
// command worker (sendPendingQueries) puts them on the wire.

#include "TestSupport.h"

using replies::hex;

namespace {

ToggleSnapshot touch(EngineFixture& f) { return f.hp.getTouchPanel(); }
ToggleSnapshot multipoint(EngineFixture& f) { return f.hp.getMultipointMode(); }
ToggleSnapshot wearing(EngineFixture& f) { return f.hp.getWearingControl(); }

bool sameSnapshot(const ToggleSnapshot& a, const ToggleSnapshot& b)
{
	return a.support == b.support && a.selector == b.selector && a.availability == b.availability &&
		a.valueKnown == b.valueKnown && a.value == b.value && a.requested == b.requested &&
		a.status == b.status;
}

// Advertise the given function IDs and run every follow-up query the reply caused.
void discover(EngineFixture& f, const std::string& functionIdsHex)
{
	const Buffer ids = hex(functionIdsHex);
	Buffer reply = hex("07 01");
	reply.push_back(static_cast<char>(ids.size()));
	reply.insert(reply.end(), ids.begin(), ids.end());
	CHECK(deliver(f, reply));
	f.hp.sendPendingQueries();
}

} // namespace

TEST(discovery_counted_reply_is_known_and_queues_detail_queries)
{
	EngineFixture f;
	f.start();

	CHECK(deliver(f, hex("07 01 03 D1 D3 F3")));
	CHECK(f.hp.capabilitiesKnown());
	CHECK(f.hp.functionState(FUNCTION_TYPE::GENERAL_SETTING1) == CapabilityState::Supported);
	CHECK(f.hp.functionState(FUNCTION_TYPE::GENERAL_SETTING2) == CapabilityState::Unsupported);
	CHECK(f.hp.functionState(FUNCTION_TYPE::CONTROL_BY_WEARING) == CapabilityState::Supported);

	// The reader only queued the follow-ups; nothing has been sent yet.
	CHECK(f.hp.hasPendingQueries());
	CHECK(f.fake().sentCommands().empty());

	f.hp.sendPendingQueries();
	CHECK_EQ(f.hp.hasPendingQueries(), false);
	CHECK(wasSent(f.fake(), "D0 D1 01"));
	CHECK(wasSent(f.fake(), "D0 D3 01"));
	CHECK(wasSent(f.fake(), "F0 03"));
	CHECK_MSG(!wasSent(f.fake(), "D0 D2 01"), "an unadvertised slot must not be asked about");
}

TEST(discovery_header_bytes_no_longer_advertise_features)
{
	EngineFixture f;
	f.start();

	// Nineteen functions: the count byte is 0x13, CODEC_INDICATOR. The old byte scan reported it
	// (and 0x07, 0x01) as supported although the device never listed it.
	Buffer reply = hex("07 01 13");
	for (int i = 0; i < 19; ++i)
		reply.push_back(static_cast<char>(0x20 + i));
	CHECK(deliver(f, reply));

	CHECK(f.hp.capabilitiesKnown());
	CHECK(f.hp.functionState(FUNCTION_TYPE::CODEC_INDICATOR) == CapabilityState::Unsupported);
	CHECK(f.hp.featureState(DEVICE_CAPABILITIES::NC_ASM) == CapabilityState::Unsupported);
}

TEST(discovery_wrong_count_or_wrong_table_changes_nothing)
{
	EngineFixture f;
	f.start();

	CHECK(deliver(f, hex("07 01 03 D1 D3")));                              // count too large
	CHECK(deliver(f, hex("07 01 01 62"), DATA_TYPE::DATA_MDR_NO2));       // table 2
	CHECK(deliver(f, hex("07 02 01 62")));                                 // another inquiry

	CHECK_EQ(f.hp.capabilitiesKnown(), false);
	CHECK(f.hp.featureState(DEVICE_CAPABILITIES::NC_ASM) == CapabilityState::Unknown);
	CHECK_EQ(f.hp.hasPendingQueries(), false);
	CHECK(touch(f).support == CapabilityState::Unknown);
}

TEST(discovery_touch_and_multipoint_slots_follow_the_capability_titles)
{
	// Every arrangement: neither feature owns a slot.
	struct Layout { unsigned char touch, multipoint; };
	for (const Layout& layout : { Layout{ 0xD1, 0xD2 }, Layout{ 0xD2, 0xD3 }, Layout{ 0xD3, 0xD1 },
		Layout{ 0xD2, 0xD1 }, Layout{ 0xD1, 0xD3 }, Layout{ 0xD3, 0xD2 } })
	{
		EngineFixture f;
		f.start();

		discover(f, "D1 D2 D3");
		const unsigned char other = static_cast<unsigned char>(0xD1 + 0xD2 + 0xD3 - layout.touch - layout.multipoint);
		CHECK(deliver(f, replies::generalSettingCapability(layout.touch, v1::GS_TITLE_TOUCH_PANEL)));
		CHECK(deliver(f, replies::generalSettingCapability(layout.multipoint, v1::GS_TITLE_MULTIPOINT,
			"Connect to two devices")));
		CHECK(deliver(f, replies::generalSettingCapability(other, "SOME_OTHER_SETTING")));

		CHECK(touch(f).support == CapabilityState::Supported);
		CHECK_EQ(int(touch(f).selector), int(layout.touch));
		CHECK(multipoint(f).support == CapabilityState::Supported);
		CHECK_EQ(int(multipoint(f).selector), int(layout.multipoint));

		f.fake().clearSent();
		f.hp.sendPendingQueries();
		const auto sent = sentPayloads(f.fake());
		CHECK(wasSent(f.fake(), "D2 " + toHex(Buffer{ static_cast<char>(layout.touch) })));
		CHECK(wasSent(f.fake(), "D6 " + toHex(Buffer{ static_cast<char>(layout.touch) })));
		CHECK(wasSent(f.fake(), "D2 " + toHex(Buffer{ static_cast<char>(layout.multipoint) })));
		CHECK(wasSent(f.fake(), "D6 " + toHex(Buffer{ static_cast<char>(layout.multipoint) })));
		CHECK_MSG(!wasSent(f.fake(), "D6 " + toHex(Buffer{ static_cast<char>(other) })),
			"a slot holding some other setting is not ours to read");
		CHECK_EQ(sent.size(), size_t(4));
	}
}

TEST(discovery_a_list_type_setting_is_not_coerced_into_a_toggle)
{
	EngineFixture f;
	f.start();

	discover(f, "D1 D2");
	CHECK(deliver(f, replies::generalSettingCapability(0xD1, v1::GS_TITLE_TOUCH_PANEL)));
	CHECK(deliver(f, replies::generalSettingCapability(0xD2, v1::GS_TITLE_MULTIPOINT, "", v1::GS_TYPE_LIST)));

	CHECK(touch(f).support == CapabilityState::Supported);
	CHECK(multipoint(f).support == CapabilityState::Unsupported);

	f.fake().clearSent();
	f.hp.sendPendingQueries();
	CHECK(!wasSent(f.fake(), "D6 D2"));

	// And a value report for that slot is ignored: it is not a boolean we track.
	CHECK(deliver(f, hex("D7 D2 01 01")));
	CHECK_EQ(multipoint(f).valueKnown, false);
}

TEST(discovery_raw_name_and_unadvertised_slots_do_not_match)
{
	EngineFixture f;
	f.start();

	discover(f, "D1");
	// D2 was not advertised in this session's 07 reply.
	CHECK(deliver(f, replies::generalSettingCapability(0xD2, v1::GS_TITLE_TOUCH_PANEL)));
	CHECK(touch(f).support == CapabilityState::Unknown);

	// RAW_NAME (01) spelling the right title: Sony only matches ENUM_NAME.
	Buffer raw = replies::generalSettingCapability(0xD1, v1::GS_TITLE_TOUCH_PANEL);
	raw[2] = 0x01;
	CHECK(deliver(f, raw));
	// Every advertised slot has now answered without naming either feature.
	CHECK(touch(f).support == CapabilityState::Unsupported);
	CHECK(multipoint(f).support == CapabilityState::Unsupported);
}

TEST(discovery_features_stay_unknown_until_every_slot_has_answered)
{
	EngineFixture f;
	f.start();

	discover(f, "D1 D3");
	CHECK(deliver(f, replies::generalSettingCapability(0xD1, v1::GS_TITLE_TOUCH_PANEL)));
	CHECK(touch(f).support == CapabilityState::Supported);
	// D3 has not described itself: multipoint might still be there.
	CHECK(multipoint(f).support == CapabilityState::Unknown);

	CHECK(deliver(f, replies::generalSettingCapability(0xD3, "SOMETHING_ELSE")));
	CHECK(multipoint(f).support == CapabilityState::Unsupported);
}

TEST(discovery_no_general_slots_means_no_touch_or_multipoint)
{
	EngineFixture f;
	f.start();

	discover(f, "62 F5");
	CHECK(touch(f).support == CapabilityState::Unsupported);
	CHECK(multipoint(f).support == CapabilityState::Unsupported);
	CHECK(wearing(f).support == CapabilityState::Unsupported);
	CHECK(!wasSent(f.fake(), "F0 03"));
}

TEST(discovery_availability_and_value_are_separate_facts)
{
	EngineFixture f;
	f.start();

	discover(f, "D2");
	CHECK(deliver(f, replies::generalSettingCapability(0xD2, v1::GS_TITLE_TOUCH_PANEL)));
	CHECK(touch(f).availability == Availability::Unknown);
	CHECK_EQ(touch(f).valueKnown, false);

	// 00 = available (CommonStatus.ENABLE) - not "off".
	CHECK(deliver(f, hex("D3 D2 00")));
	CHECK(touch(f).availability == Availability::Available);
	CHECK_EQ(touch(f).valueKnown, false);

	CHECK(deliver(f, hex("D7 D2 01 01")));
	CHECK_EQ(touch(f).valueKnown, true);
	CHECK_EQ(touch(f).value, true);
	CHECK_EQ(touch(f).status, SettingStatus::Confirmed);

	// Unavailable does not mean off, and off does not mean unavailable.
	CHECK(deliver(f, hex("D5 D2 01")));
	CHECK(touch(f).availability == Availability::Unavailable);
	CHECK_EQ(touch(f).value, true);
	CHECK(deliver(f, hex("D9 D2 01 00")));
	CHECK_EQ(touch(f).value, false);
	CHECK(touch(f).availability == Availability::Unavailable);
}

TEST(discovery_state_for_an_unmapped_slot_is_ignored)
{
	EngineFixture f;
	f.start();

	discover(f, "D1 D2");
	CHECK(deliver(f, replies::generalSettingCapability(0xD2, v1::GS_TITLE_MULTIPOINT)));

	// D1 has not been resolved to anything; D7 for it says nothing about either feature.
	CHECK(deliver(f, hex("D7 D1 01 01")));
	CHECK(deliver(f, hex("D3 D1 00")));
	CHECK_EQ(touch(f).valueKnown, false);
	CHECK_EQ(multipoint(f).valueKnown, false);
	CHECK(multipoint(f).availability == Availability::Unknown);
}

TEST(discovery_wearing_control_needs_F3_and_an_on_off_capability)
{
	{
		EngineFixture f;
		f.start();
		discover(f, "62");
		// Without F3 a capability reply is not ours to believe.
		CHECK(deliver(f, hex("F1 03 00")));
		CHECK(wearing(f).support == CapabilityState::Unsupported);
	}
	{
		EngineFixture f;
		f.start();
		discover(f, "F3");
		CHECK(wasSent(f.fake(), "F0 03"));
		CHECK(wearing(f).support == CapabilityState::Unknown);

		// An unknown setting type: no known write encoding, so not offered.
		CHECK(deliver(f, hex("F1 03 01")));
		CHECK(wearing(f).support == CapabilityState::Unsupported);
	}
	{
		EngineFixture f;
		f.start();
		discover(f, "F3");
		CHECK(deliver(f, hex("F1 03 00")));
		CHECK(wearing(f).support == CapabilityState::Supported);
		CHECK_EQ(int(wearing(f).selector), 0x03);

		f.fake().clearSent();
		f.hp.sendPendingQueries();
		CHECK(wasSent(f.fake(), "F2 03"));
		CHECK(wasSent(f.fake(), "F6 03"));

		CHECK(deliver(f, hex("F3 03 01")));
		CHECK(wearing(f).availability == Availability::Unavailable);
		CHECK_EQ(wearing(f).valueKnown, false);
		CHECK(deliver(f, hex("F9 03 00 01")));
		CHECK_EQ(wearing(f).valueKnown, true);
		CHECK_EQ(wearing(f).value, true);
	}
}

TEST(discovery_system_replies_for_other_selectors_do_not_touch_wearing_control)
{
	EngineFixture f;
	f.start();
	discover(f, "F3 F4 F5");
	CHECK(deliver(f, hex("F1 03 00")));
	CHECK(deliver(f, hex("F7 03 00 00")));
	CHECK_EQ(wearing(f).value, false);

	// F7/F9 are shared: auto power-off (04) and Speak-to-Chat (05) must not land here.
	CHECK(deliver(f, hex("F9 05 01 01")));
	CHECK(deliver(f, hex("F7 04 01 10 00")));
	CHECK(deliver(f, hex("F5 04 00")));
	CHECK_EQ(wearing(f).value, false);
	CHECK(wearing(f).availability == Availability::Unknown);
}

TEST(discovery_paired_device_reply_is_only_read_from_table_2)
{
	EngineFixture f;
	f.start();

	Buffer reply = hex("37 01 01");
	const std::string mac = "AA:BB:CC:DD:EE:FF";
	reply.insert(reply.end(), mac.begin(), mac.end());
	reply.push_back(0x01);
	reply.push_back(0x05);
	for (char c : std::string("Phone"))
		reply.push_back(c);

	// Table-1 0x37 is UPDT_RET_PARAM. The old command-only switch fed it to the device list.
	CHECK(deliver(f, reply, DATA_TYPE::DATA_MDR));
	CHECK_EQ(f.hp.getDevices().size(), size_t(1));

	CHECK(deliver(f, reply, DATA_TYPE::DATA_MDR_NO2));
	CHECK_EQ(f.hp.getDevices().size(), size_t(2));
	CHECK_EQ(f.hp.getConnectedDevices().first, 1);
}

TEST(discovery_truncated_and_malformed_frames_leave_state_unchanged)
{
	EngineFixture f;
	f.start();

	discover(f, "D1 D2 F3");
	CHECK(deliver(f, replies::generalSettingCapability(0xD1, v1::GS_TITLE_TOUCH_PANEL)));
	CHECK(deliver(f, replies::generalSettingCapability(0xD2, v1::GS_TITLE_MULTIPOINT)));
	CHECK(deliver(f, hex("F1 03 00")));
	CHECK(deliver(f, hex("D3 D1 00")));
	CHECK(deliver(f, hex("D7 D1 01 01")));
	CHECK(deliver(f, hex("F3 03 00")));
	CHECK(deliver(f, hex("F7 03 00 01")));
	f.hp.sendPendingQueries();

	const ToggleSnapshot touchBefore = touch(f);
	const ToggleSnapshot multipointBefore = multipoint(f);
	const ToggleSnapshot wearingBefore = wearing(f);

	// Every strict prefix of a reply that would otherwise change something, and a few frames
	// that are complete but wrong.
	const char* replies[] = { "D5 D1 01", "D9 D1 01 00", "F5 03 01", "F9 03 00 00", "D3 D2 01",
		"D7 D2 01 01", "07 01 02 D1 D2" };
	for (const char* text : replies)
	{
		const Buffer full = hex(text);
		for (size_t cut = 1; cut < full.size(); ++cut)
			CHECK(deliver(f, Buffer(full.begin(), full.begin() + cut)));
	}
	for (const char* text : { "D9 D1 01 02", "D9 D1 02 00", "F9 03 01 00", "F5 03 02", "D5 D4 01",
		"D9 D1 01 00 00", "F9 03 00 00 00" })
		CHECK(deliver(f, hex(text)));
	// The right bytes on the wrong table.
	CHECK(deliver(f, hex("D9 D1 01 00"), DATA_TYPE::DATA_MDR_NO2));
	CHECK(deliver(f, hex("F9 03 00 00"), DATA_TYPE::DATA_MDR_NO2));

	CHECK(sameSnapshot(touch(f), touchBefore));
	CHECK(sameSnapshot(multipoint(f), multipointBefore));
	CHECK(sameSnapshot(wearing(f), wearingBefore));
	CHECK_EQ(f.hp.hasPendingQueries(), false);
	CHECK_EQ(f.disconnectCallbacks.load(), 0);
}

TEST(discovery_runs_once_per_session)
{
	EngineFixture f;
	f.start();

	discover(f, "D1");
	CHECK_EQ(countSent(f.fake(), "D0 D1 01"), size_t(1));

	// The device repeats itself: the function set is refreshed, discovery is not restarted.
	CHECK(deliver(f, hex("07 01 02 D1 62")));
	f.hp.sendPendingQueries();
	CHECK_EQ(countSent(f.fake(), "D0 D1 01"), size_t(1));
	CHECK(f.hp.featureState(DEVICE_CAPABILITIES::NC_ASM) == CapabilityState::Supported);
}

TEST(discovery_reset_forgets_slots_and_queued_queries)
{
	EngineFixture f;
	f.start();

	discover(f, "D1 D2 F3");
	CHECK(deliver(f, replies::generalSettingCapability(0xD1, v1::GS_TITLE_TOUCH_PANEL)));
	CHECK(deliver(f, hex("D7 D1 01 01")));
	CHECK(f.hp.hasPendingQueries());   // D2/D6 D1 are still queued

	f.hp.resetState();

	CHECK_EQ(f.hp.capabilitiesKnown(), false);
	CHECK_EQ(f.hp.hasPendingQueries(), false);
	CHECK(touch(f).support == CapabilityState::Unknown);
	CHECK_EQ(int(touch(f).selector), 0);
	CHECK_EQ(touch(f).valueKnown, false);
	CHECK(wearing(f).support == CapabilityState::Unknown);
}

TEST(discovery_reconnect_rediscovers_instead_of_replaying)
{
	EngineFixture f;
	f.start();

	discover(f, "D1");
	CHECK(deliver(f, replies::generalSettingCapability(0xD1, v1::GS_TITLE_TOUCH_PANEL)));
	// The D2/D6 follow-ups are still queued when the session ends.
	CHECK(f.hp.hasPendingQueries());

	f.hp.resetState();
	f.reconnect();
	f.fake().clearSent();

	// Nothing from the old session goes out on the new link.
	f.hp.sendPendingQueries();
	CHECK(f.fake().sentCommands().empty());

	// This device keeps touch in D3. The new session finds it there.
	discover(f, "D3");
	CHECK(deliver(f, replies::generalSettingCapability(0xD3, v1::GS_TITLE_TOUCH_PANEL)));
	CHECK_EQ(int(touch(f).selector), 0xD3);
	CHECK(!wasSent(f.fake(), "D6 D1"));
}

TEST(discovery_upscaling_indicator_is_not_the_dsee_setting)
{
	EngineFixture f;
	f.start();

	const auto before = f.hp.getStatuses().dsee;
	const bool dseeBefore = f.hp.getAudioUpsampling();
	CHECK(deliver(f, hex("15 02 01")));
	CHECK(deliver(f, hex("17 02 01")));
	CHECK_EQ(f.hp.getStatuses().dsee, before);
	CHECK_EQ(f.hp.getAudioUpsampling(), dseeBefore);
}
