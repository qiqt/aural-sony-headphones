// Read-back for the settings Aural already exposed: equalizer, Speak-to-Chat, DSEE, voice
// guidance, auto power-off, NC/ASM and spatial. Layouts are APK/Gadgetbridge evidence; these
// tests prove the engine follows it, not that a headset does.

#include "TestSupport.h"

#include <atomic>

using replies::hex;

namespace {

void advertise(EngineFixture& f, const std::string& functionIdsHex)
{
	const Buffer ids = hex(functionIdsHex);
	Buffer reply = hex("07 01");
	reply.push_back(static_cast<char>(ids.size()));
	reply.insert(reply.end(), ids.begin(), ids.end());
	CHECK(deliver(f, reply));
}

CommandSerializer::NcAsmReport ncAsm(const std::string& text)
{
	auto report = CommandSerializer::parseNcAsm(CommandSerializer::Message{ DATA_TYPE::DATA_MDR, 0, hex(text) });
	CHECK_MSG(report.has_value(), "test vector should decode: " + text);
	return *report;
}

// A headset that remembers its DSEE setting, applies writes (unless told not to) and answers
// reads with what it holds.
struct DseeDevice
{
	std::shared_ptr<std::atomic<int>> value = std::make_shared<std::atomic<int>>(0);
	std::shared_ptr<std::atomic<bool>> applyWrites = std::make_shared<std::atomic<bool>>(true);
	DeviceScript script;

	DseeDevice()
	{
		auto v = value;
		auto apply = applyWrites;
		script.onRequest("E8 02 00 01", [v, apply](FakeBluetoothConnector&, const CommandSerializer::Message&) {
			if (*apply) *v = 1;
		});
		script.onRequest("E8 02 00 00", [v, apply](FakeBluetoothConnector&, const CommandSerializer::Message&) {
			if (*apply) *v = 0;
		});
		script.onRequest("E6 02", [v](FakeBluetoothConnector& device, const CommandSerializer::Message&) {
			device.enqueueData(*v ? hex("E7 02 00 01") : hex("E7 02 00 00"));
		});
	}
};

} // namespace

// ---- what is read, and when ---------------------------------------------------------------------

TEST(readback_connect_reads_every_advertised_setting)
{
	EngineFixture f;
	f.start();

	advertise(f, "41 42 51 E2 F4 F5 39");
	f.hp.sendPendingQueries();

	for (const char* query : { "46 01", "46 02", "56 01", "E6 02", "F6 04", "F6 05", "FA 05" })
		CHECK_MSG(wasSent(f.fake(), query), std::string("expected a read: ") + query);
	CHECK(wasSent(f.fake(), "46 01 01", DATA_TYPE::DATA_MDR_NO2));
	// The voice-guidance read never goes out on table 1, where 0x46 is VPT.
	CHECK(!wasSent(f.fake(), "46 01 01", DATA_TYPE::DATA_MDR));
}

TEST(readback_unadvertised_settings_are_not_queried)
{
	EngineFixture f;
	f.start();

	advertise(f, "62");
	f.hp.sendPendingQueries();
    CHECK(wasSent(f.fake(), "60 02")); // only the advertised ANC capability
    for (const auto& message : f.fake().sentCommands()) CHECK(message.messageBytes == replies::hex("60 02"));

	// Without a capability reply at all, a write is not followed by a read either.
	EngineFixture g;
	g.start();
	g.hp.setAudioUpsampling(true);
	g.hp.setChanges();
	CHECK_EQ(g.hp.hasPendingQueries(), false);
	CHECK_EQ(g.hp.getStatuses().dsee, SettingStatus::Acknowledged);
}

// ---- table collisions -----------------------------------------------------------------------------

TEST(readback_vpt_and_voice_guidance_share_numbers_not_tables)
{
	EngineFixture f;
	f.start();

	// Table 2, 47 01 01 01: voice guidance on.
	CHECK(deliver(f, hex("47 01 01 01"), DATA_TYPE::DATA_MDR_NO2));
	CHECK_EQ(f.hp.getVoiceGuidance(), true);
	CHECK_EQ(f.hp.getVptType(), 0);
	CHECK_EQ(f.hp.getStatuses().spatial, SettingStatus::Unknown);

	// Table 1, 47 01 02: VPT preset Arena. Voice guidance is not touched.
	CHECK(deliver(f, hex("47 01 02")));
	CHECK_EQ(f.hp.getVptType(), 2);
	CHECK_EQ(f.hp.getVoiceGuidance(), true);

	// Each table's layout on the other table decodes to nothing.
	CHECK(deliver(f, hex("49 01 01 00"), DATA_TYPE::DATA_MDR));
	CHECK(deliver(f, hex("49 01 03"), DATA_TYPE::DATA_MDR_NO2));
	CHECK_EQ(f.hp.getVoiceGuidance(), true);
	CHECK_EQ(f.hp.getVptType(), 2);
}

// ---- write, then read ---------------------------------------------------------------------------

TEST(readback_write_is_confirmed_by_the_read_that_follows)
{
	EngineFixture f;
	DseeDevice device;
	device.script.installOn(f.fake());
	f.start();

	advertise(f, "E2");
	f.hp.sendPendingQueries();
	CHECK(testing::waitFor([&] { return f.hp.getStatuses().dsee == SettingStatus::Confirmed; }));
	CHECK_EQ(f.hp.getAudioUpsampling(), false);

	f.hp.setAudioUpsampling(true);
	f.hp.setChanges();
	// The ACK only says the frame arrived.
	CHECK_EQ(f.hp.getStatuses().dsee, SettingStatus::Acknowledged);
	CHECK(f.hp.hasPendingQueries());

	f.hp.sendPendingQueries();
	CHECK(testing::waitFor([&] { return f.hp.getStatuses().dsee == SettingStatus::Confirmed; }));
	CHECK_EQ(f.hp.getAudioUpsampling(), true);
}

TEST(readback_a_different_reported_value_is_shown_as_the_device_state)
{
	EngineFixture f;
	DseeDevice device;
	*device.applyWrites = false;   // the headset ACKs the write but keeps its setting
	device.script.installOn(f.fake());
	f.start();

	advertise(f, "E2");
	f.hp.setAudioUpsampling(true);
	f.hp.setChanges();
	f.hp.sendPendingQueries();

	CHECK(testing::waitFor([&] { return !f.hp.getAudioUpsampling(); }));
	CHECK_MSG(f.hp.getStatuses().dsee == SettingStatus::Confirmed,
		"the device's own report is what the app shows, not the request it ignored");
	CHECK_EQ(f.hp.isChanged(), false);
}

TEST(readback_reply_arriving_before_the_ack_confirms_the_write)
{
	EngineFixture f;
	f.fake().setScript([&](FakeBluetoothConnector& fake, const CommandSerializer::Message& msg) {
		if (msg.dataType == DATA_TYPE::ACK)
			return;
		if (toHex(msg.messageBytes) == "E8 02 00 01")
		{
			fake.enqueueData(hex("E9 02 00 01"));
			CHECK(testing::waitFor([&] { return f.hp.getStatuses().dsee == SettingStatus::Confirmed; }));
		}
		fake.enqueueAck(msg.seqNumber);
	});
	f.start();

	f.hp.setAudioUpsampling(true);
	f.hp.setChanges();
	CHECK_EQ(f.hp.getStatuses().dsee, SettingStatus::Confirmed);
	CHECK_EQ(f.hp.getAudioUpsampling(), true);
}

TEST(readback_disconnect_during_a_write_leaves_it_failed_not_confirmed)
{
	EngineFixture f;
	f.fake().setScript([](FakeBluetoothConnector& fake, const CommandSerializer::Message& msg) {
		if (msg.dataType == DATA_TYPE::ACK)
			return;
		if (toHex(msg.messageBytes) == "E8 02 00 01")
		{
			fake.closeRemote();   // the link drops with the write in flight
			return;
		}
		fake.enqueueAck(msg.seqNumber);
	});
	f.start();

	advertise(f, "E2");
	f.hp.sendPendingQueries();
	CHECK(deliver(f, hex("E7 02 00 00")));
	f.hp.setAudioUpsampling(true);
	CHECK_THROWS(f.hp.setChanges(), RecoverableException);

	CHECK_EQ(f.hp.getStatuses().dsee, SettingStatus::Failed);
	CHECK_EQ(f.hp.getAudioUpsampling(), false);
	CHECK_EQ(f.hp.hasPendingQueries(), false);
}

// ---- per-setting decoding -------------------------------------------------------------------------

TEST(readback_speak_to_chat_ret_and_ntfy_use_different_subtypes)
{
	EngineFixture f;
	f.start();

	CHECK(deliver(f, hex("F7 05 00 01")));   // RET: SmartTalkingModeSettingType.ON_OFF
	CHECK(f.hp.getS2CToggle() == S2C_TOGGLE::ACTIVE);
	CHECK(deliver(f, hex("F9 05 01 00")));   // NTFY: SmartTalkingModeParameterType.MODE_ON_OFF
	CHECK(f.hp.getS2CToggle() == S2C_TOGGLE::INACTIVE);

	// A preview turns Speak-to-Chat on to demonstrate it; the setting stays off.
	CHECK(deliver(f, hex("F9 05 02 01")));
	CHECK(f.hp.getS2CToggle() == S2C_TOGGLE::INACTIVE);

	// Swapped sub-types are not the layouts Sony uses for these commands.
	CHECK(deliver(f, hex("F7 05 01 01")));
	CHECK(deliver(f, hex("F9 05 00 01")));
	CHECK(f.hp.getS2CToggle() == S2C_TOGGLE::INACTIVE);
}

TEST(readback_speak_to_chat_details)
{
	EngineFixture f;
	f.start();

	CHECK(deliver(f, hex("FB 05 00 02 01 03")));
	CHECK_EQ(f.hp.getS2COptions(), (2u << 16) | (1u << 8) | 3u);
	CHECK_EQ(f.hp.getStatuses().speakToChat == SettingStatus::Unknown, true); // the toggle is still unread

	for (const char* bad : { "FD 05 00 03 00 00", "FD 05 00 00 02 00", "FD 05 00 00 00 04", "FB 05 01 00 00 00",
		"FB 04 00 00 00 00", "FB 05 00 00 00" })
		CHECK(deliver(f, hex(bad)));
	CHECK_EQ(f.hp.getS2COptions(), (2u << 16) | (1u << 8) | 3u);

	CHECK(deliver(f, hex("FD 05 00 01 00 00")));
	CHECK_EQ(f.hp.getS2COptions(), 1u << 16);
}

TEST(readback_dsee_setting_is_separate_from_priority_and_the_indicator)
{
	EngineFixture f;
	f.start();

	CHECK(deliver(f, hex("E7 02 00 01")));
	CHECK_EQ(f.hp.getAudioUpsampling(), true);

	CHECK(deliver(f, hex("E9 01 00 00")));   // connection priority, not DSEE
	CHECK(deliver(f, hex("17 02 00")));      // "processing right now" indicator
	CHECK(deliver(f, hex("E9 02 00 02")));   // out of range
	CHECK_EQ(f.hp.getAudioUpsampling(), true);

	CHECK(deliver(f, hex("E9 02 00 00")));
	CHECK_EQ(f.hp.getAudioUpsampling(), false);
}

TEST(readback_voice_guidance_validates_inquiry_and_detail)
{
	EngineFixture f;
	f.start();

	CHECK(deliver(f, hex("49 01 01 01"), DATA_TYPE::DATA_MDR_NO2));
	CHECK_EQ(f.hp.getVoiceGuidance(), true);
	for (const char* bad : { "49 02 01 00", "49 01 02 00", "49 01 01 02", "49 01 01", "49 01 01 00 00" })
		CHECK(deliver(f, hex(bad), DATA_TYPE::DATA_MDR_NO2));
	CHECK_EQ(f.hp.getVoiceGuidance(), true);
}

TEST(readback_auto_power_off_preserves_the_selected_timer)
{
	EngineFixture f;
	f.start();

	// Disabled, with the 180-minute timer still selected.
	CHECK(deliver(f, hex("F7 04 01 11 03")));
	CHECK(f.hp.getAutoPowerOff() == AUTO_POWER_OFF::OFF);
	CHECK_EQ(int(f.hp.getAutoPowerOffConfig().selectedTime), 0x03);

	f.hp.setAutoPowerOff(AUTO_POWER_OFF::WHEN_TAKEN_OFF);
	f.hp.setChanges();
	CHECK_MSG(wasSent(f.fake(), "F8 04 01 10 03"), "switching mode must not overwrite the selected timer");

	f.hp.setAutoPowerOff(AUTO_POWER_OFF::AFTER_30_MIN);
	f.hp.setChanges();
	CHECK(wasSent(f.fake(), "F8 04 01 01 01"));

	// The newest timer choice is the one kept.
	f.hp.setAutoPowerOff(AUTO_POWER_OFF::OFF);
	f.hp.setChanges();
	CHECK(wasSent(f.fake(), "F8 04 01 11 01"));
}

TEST(readback_auto_power_off_without_a_report_sends_the_legacy_bytes)
{
	EngineFixture f;
	f.start();

	f.hp.setAutoPowerOff(AUTO_POWER_OFF::WHEN_TAKEN_OFF);
	f.hp.setChanges();
	CHECK(wasSent(f.fake(), "F8 04 01 10 00"));
	f.hp.setAutoPowerOff(AUTO_POWER_OFF::AFTER_1_HOUR);
	f.hp.setChanges();
	CHECK(wasSent(f.fake(), "F8 04 01 02 02"));

	// The old serializer still produces the bytes it always did.
	CHECK_EQ(toHex(CommandSerializer::serializeAutoPowerOff(AUTO_POWER_OFF::OFF)), std::string("F8 04 01 11 00"));
	CHECK_EQ(toHex(CommandSerializer::serializeAutoPowerOff(AUTO_POWER_OFF::AFTER_3_HOUR)), std::string("F8 04 01 03 03"));
}

TEST(readback_auto_power_off_rejects_unknown_layouts)
{
	EngineFixture f;
	f.start();

	CHECK(deliver(f, hex("F9 04 01 10 00")));
	for (const char* bad : { "F9 04 01 05 00", "F9 04 01 10 12", "F9 04 02 10 00", "F9 04 01 10", "F9 04 01 10 00 00" })
		CHECK(deliver(f, hex(bad)));
	CHECK(f.hp.getAutoPowerOff() == AUTO_POWER_OFF::WHEN_TAKEN_OFF);
	CHECK_EQ(int(f.hp.getAutoPowerOffConfig().selectedTime), 0x00);
}

TEST(readback_ambient_mode_is_derived_from_the_complete_state)
{
	struct Case { const char* report; AmbientMode mode; };
	const Case cases[] = {
		{ "69 02 00 01 02 01 00 FF", AmbientMode::Off },
		// Level adjustment (what this client writes): DUAL, or level 0, is noise cancelling.
		{ "69 02 11 01 02 01 00 00", AmbientMode::NoiseCancelling },
		{ "69 02 01 01 02 01 00 0A", AmbientMode::NoiseCancelling },   // NC that kept the last level
		{ "69 02 11 01 00 01 00 0A", AmbientMode::AmbientSound },
		{ "69 02 11 01 01 01 00 01", AmbientMode::AmbientSound },      // the client's own ambient level 1
		// On/off type: the value decides, whatever the level byte holds.
		{ "69 02 01 00 01 01 00 07", AmbientMode::NoiseCancelling },
		{ "69 02 01 00 00 01 00 07", AmbientMode::AmbientSound },
		// Dual/single/off: single and dual are both noise cancelling.
		{ "69 02 01 02 01 01 00 05", AmbientMode::NoiseCancelling },
		{ "69 02 01 02 00 01 00 05", AmbientMode::AmbientSound },
	};
	for (const Case& c : cases)
		CHECK_MSG(ambientModeOf(ncAsm(c.report)) == c.mode, std::string("wrong mode for ") + c.report);
}

TEST(readback_nc_asm_keeps_every_field_and_normalises_noise_cancelling)
{
	EngineFixture f;
	f.start();

	// Noise cancelling, reported with the last ambient level still in the frame. The old reader
	// took level 10 and showed "Ambient Sound, level 10".
	CHECK(deliver(f, hex("69 02 01 01 02 01 01 0A")));
	CHECK_EQ(f.hp.getAmbientSoundControl(), true);
	CHECK_EQ(f.hp.getAsmLevel(), 0);
	CHECK_EQ(f.hp.getFocusOnVoice(), true);

	auto report = f.hp.getNcAsmReport();
	CHECK(report.has_value());
	CHECK_EQ(int(report->ncSettingType), 0x01);
	CHECK_EQ(int(report->ncValue), 0x02);
	CHECK_EQ(int(report->asmLevel), 10);
}

TEST(readback_nc_request_is_confirmed_by_another_nc_encoding)
{
	EngineFixture f;
	f.start();

	f.hp.setAmbientSoundControl(true);
	f.hp.setAsmLevel(0);
	f.hp.setChanges();
	CHECK_EQ(f.hp.getStatuses().ambient, SettingStatus::Acknowledged);

	// The device answers in on/off form with a stale level: still the noise cancelling we asked for.
	CHECK(deliver(f, hex("69 02 01 00 01 01 00 07")));
	CHECK_EQ(f.hp.getStatuses().ambient, SettingStatus::Confirmed);
	CHECK_EQ(f.hp.getAsmLevel(), 0);
}

TEST(readback_equalizer_report)
{
	EngineFixture f;
	f.start();

	CHECK_EQ(f.hp.getEqualizerLevelCount(), -1);
	CHECK(deliver(f, hex("57 01 10 06 0A 0C 08 0A 0E 14")));
	auto eq = f.hp.getEqualizer();
	CHECK_EQ(eq.preset, 0x10);
	CHECK_EQ(eq.clearBass, 0);
	CHECK(eq.bands == (std::vector<int>{ 2, -2, 0, 4, 10 }));
	CHECK_EQ(f.hp.getEqualizerLevelCount(), 6);
	CHECK_EQ(f.hp.getStatuses().equalizer, SettingStatus::Confirmed);

	// A preset reported without levels keeps the bands we know and records the shape.
	CHECK(deliver(f, hex("59 01 A0 00")));
	eq = f.hp.getEqualizer();
	CHECK_EQ(eq.preset, 0xA0);
	CHECK(eq.bands == (std::vector<int>{ 2, -2, 0, 4, 10 }));
	CHECK_EQ(f.hp.getEqualizerLevelCount(), 0);

	// Count mismatch, a preset outside EqPresetId, EBB (not decoded), the wrong table.
	for (const char* bad : { "57 01 10 06 0A 0C", "57 01 30 00", "57 02 05", "57 01 10" })
		CHECK(deliver(f, hex(bad)));
	CHECK(deliver(f, hex("57 01 11 00"), DATA_TYPE::DATA_MDR_NO2));
	CHECK_EQ(f.hp.getEqualizer().preset, 0xA0);
}

TEST(readback_queued_manual_curve_is_not_sent_after_an_unsupported_layout_arrives)
{
	EngineFixture f;
	f.start();
	EqualizerConfig cfg;
	cfg.preset = static_cast<unsigned char>(EQ_PRESET::MANUAL);
	f.hp.setEqualizer(cfg);
	CHECK(deliver(f, hex("57 01 10 03 0A 0A 0A")));
	bool refused = false;
	try { f.hp.setChanges(); }
	catch (const RecoverableException& exc) { refused = true; CHECK(!exc.shouldDisconnect); }
	CHECK(refused);
	CHECK_EQ(f.hp.getStatuses().equalizer, SettingStatus::Failed);
	CHECK(!f.hp.isChanged());
	for (const auto& msg : f.fake().sentCommands())
		CHECK(msg.messageBytes.empty() || static_cast<unsigned char>(msg.messageBytes[0]) != 0x58);
}

TEST(readback_equalizer_echo_of_the_custom_marker_is_manual)
{
	EngineFixture f;
	f.start();

	// 0xFF is what this client writes for custom bands; the device echoing it is the client's
	// own Manual curve, recorded as MANUAL so a Manual request can be confirmed by it.
	CHECK(deliver(f, hex("57 01 FF 06 0A 0B 0C 0D 0E 0F")));
	CHECK_EQ(f.hp.getEqualizer().preset, 0xA0);
	CHECK_EQ(f.hp.getEqualizer().clearBass, 0);
}

TEST(readback_reselecting_a_reported_preset_sends_nothing)
{
	EngineFixture f;
	f.start();

	// The device reports Bright together with its curve.
	CHECK(deliver(f, hex("57 01 10 06 0C 0C 0C 0C 0C 0C")));

	// A caller selecting Bright passes its own (irrelevant) band values and the signed enum
	// value. Neither may turn the same preset into a new request.
	EqualizerConfig bright;
	bright.preset = static_cast<int>(EQ_PRESET::BRIGHT);
	bright.bands = { -3, -3, -3, -3, -3 };
	f.hp.setEqualizer(bright);
	CHECK_EQ(f.hp.isChanged(), false);

	CHECK(deliver(f, hex("59 01 A0 06 0A 0A 0A 0A 0A 0A")));
	EqualizerConfig manual;
	manual.preset = static_cast<int>(EQ_PRESET::MANUAL);   // -96 as an int
	f.hp.setEqualizer(manual);
	CHECK_EQ(f.hp.isChanged(), false);

	// Manual with different bands is a real change.
	manual.bands = { 1, 0, 0, 0, 0 };
	f.hp.setEqualizer(manual);
	CHECK_EQ(f.hp.isChanged(), true);
}

TEST(readback_equalizer_write_is_followed_by_a_read)
{
	EngineFixture f;
	DeviceScript device;
	device.on("56 01", hex("57 01 10 06 0A 0A 0A 0A 0A 0A"));
	device.installOn(f.fake());
	f.start();

	advertise(f, "51");
	EqualizerConfig bright;
	bright.preset = static_cast<int>(EQ_PRESET::BRIGHT);
	f.hp.setEqualizer(bright);
	f.hp.setChanges();
	CHECK(wasSent(f.fake(), "58 01 10 00"));
	CHECK_EQ(f.hp.getStatuses().equalizer, SettingStatus::Acknowledged);

	f.hp.sendPendingQueries();
	CHECK(testing::waitFor([&] { return f.hp.getStatuses().equalizer == SettingStatus::Confirmed; }));
	CHECK_EQ(f.hp.getEqualizer().preset, 0x10);
}

TEST(readback_spatial_reports)
{
	EngineFixture f;
	f.start();

	CHECK(deliver(f, hex("47 01 03")));
	CHECK_EQ(f.hp.getVptType(), 3);
	CHECK(deliver(f, hex("49 02 11")));
	CHECK(f.hp.getSurroundPosition() == SOUND_POSITION_PRESET::REAR_LEFT);

	for (const char* bad : { "49 02 05", "47 01 10", "47 03 01", "47 01", "47 01 01 00" })
		CHECK(deliver(f, hex(bad)));
	CHECK_EQ(f.hp.getVptType(), 3);
	CHECK(f.hp.getSurroundPosition() == SOUND_POSITION_PRESET::REAR_LEFT);
}

TEST(readback_spatial_write_reads_both_effects)
{
	EngineFixture f;
	f.start();

	advertise(f, "41 42");
	f.hp.sendPendingQueries();
	f.fake().clearSent();

	f.hp.setVptType(2);
	f.hp.setChanges();
	f.hp.sendPendingQueries();
	CHECK(wasSent(f.fake(), "48 01 02"));
	CHECK(wasSent(f.fake(), "46 01"));
	CHECK(wasSent(f.fake(), "46 02"));
}
