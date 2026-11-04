#include "TestSupport.h"
#include <future>

namespace {
void setupV2(EngineFixture& f, DeviceScript& script, bool twoTables = true) {
    f.fake().setSonyServices(false, true);
    script.on("00 00", replies::hex(twoTables ? "01 00 03 00 30 37 00 00" : "01 00 03 00 30 37 00 01"))
        .on("02 00", replies::hex("03 00 01 03 61 62 63"))
        .on("04 01", replies::hex("05 01 0A 57 48 2D 31 30 30 30 58 4D 35"))
        .on("04 02", replies::hex("05 02 03 31 2E 30"))
        .on("06 00", replies::hex("07 00 02 20 01 50 02"))
        .on("06 00", replies::hex("07 00 01 50 09"), DATA_TYPE::DATA_MDR_NO2, DATA_TYPE::DATA_MDR_NO2)
        .on("22 00", replies::hex("23 00 55 00"))
        .on("50 00", replies::hex("51 00 06 15 02 00 00 10 00"))
        .on("52 00", replies::hex("53 00 00"))
        .on("56 00", replies::hex("57 00 00 06 0A 0A 0A 0A 0A 0A"));
    script.installOn(f.fake());
    f.start();
    f.hp.queryState();
    for (int i = 0; i < 4; ++i) { f.hp.sendPendingQueries(); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
}
}

TEST(v2session_discovery_preserves_tables_and_never_runs_v1_initialization)
{
    EngineFixture f; DeviceScript script; setupV2(f, script);
    const auto snapshot = f.hp.getV2Snapshot();
    CHECK_EQ(snapshot.phase, std::string("ready"));
    CHECK(snapshot.functions[0]->supports(0x50));
    CHECK(snapshot.functions[1]->supports(0x50)); // distinct table; not another EQ
    CHECK_EQ(snapshot.equalizers.size(), size_t(1));
    CHECK(snapshot.equalizers[0].writable);
    CHECK_EQ(f.hp.reportedModel(), std::string("WH-1000XM5"));
    CHECK_EQ(f.hp.getBatteryLevel(), 85);
    CHECK(!wasSent(f.fake(), "06 01"));
    CHECK(!wasSent(f.fake(), "66 02"));
    CHECK(!wasSent(f.fake(), "10 00"));
    CHECK(!f.hp.capabilitiesKnown()); // V1 capability namespace stays empty
    f.hp.setAudioUpsampling(true); f.hp.setAsmLevel(12); f.hp.setChanges();
    CHECK(!wasSent(f.fake(), "E8 02 00 01"));
    CHECK(!f.hp.isChanged());
}

TEST(v2session_eq_write_requires_current_capability_and_reads_back)
{
    EngineFixture f; DeviceScript script; setupV2(f, script, false);
    CHECK(!wasSent(f.fake(), "06 00", DATA_TYPE::DATA_MDR_NO2));
    CHECK(!f.hp.setV2EqPreset(0, 0xa2));
    CHECK(f.hp.setV2EqPreset(0, 0x10));
    f.hp.setChanges();
    CHECK(wasSent(f.fake(), "58 00 10 00"));
    auto snapshot = f.hp.getV2Snapshot();
    CHECK(snapshot.equalizers[0].status == SettingStatus::Acknowledged);
    CHECK_EQ(snapshot.equalizers[0].selectedPreset, 0x10);
    CHECK(snapshot.equalizers[0].reported->preset == 0);
    f.hp.sendPendingQueries(); // device refuses the change and reports Off
    CHECK(testing::waitFor([&] { return f.hp.getV2Snapshot().equalizers[0].status == SettingStatus::Confirmed; }));
    CHECK(f.hp.getV2Snapshot().equalizers[0].selectedPreset == 0);
    CHECK(deliver(f, replies::hex("55 00 01")));
    CHECK(!f.hp.setV2EqPreset(0, 0x10));
    f.stopSession(); f.hp.resetState();
    CHECK(f.hp.getV2Snapshot().equalizers.empty());
    CHECK(f.hp.firmwareVersion().empty());
}

TEST(v2session_unknown_version_and_unsolicited_frames_cannot_enable_writes)
{
    EngineFixture f; DeviceScript script;
    f.fake().setSonyServices(false, true);
    script.on("00 00", replies::hex("01 00 7F 00 00 00 00 00")); script.installOn(f.fake()); f.start();
    CHECK(deliver(f, replies::hex("07 00 01 50 01")));
    f.hp.queryState();
    CHECK_EQ(f.hp.getV2Snapshot().phase, std::string("unsupported-version"));
    CHECK(!wasSent(f.fake(), "02 00"));
    CHECK(!f.hp.setV2EqPreset(0, 0x10));
    CHECK(deliver(f, replies::hex("51 00 06 15 01 10 00")));
    CHECK(f.hp.getV2Snapshot().equalizers.empty());
}

TEST(v2session_malformed_protocol_reply_times_out_without_legacy_fallback)
{
    EngineFixture f; DeviceScript script;
    f.fake().setSonyServices(false, true);
    script.on("00 00", replies::hex("01 00 03 00 30 37 02 00")); script.installOn(f.fake()); f.start();
    bool failed = false;
    try { f.hp.queryState(); } catch (const RecoverableException& e) { failed = e.shouldDisconnect; }
    CHECK(failed); CHECK(!wasSent(f.fake(), "06 01")); CHECK(!wasSent(f.fake(), "02 00"));
}

TEST(v2session_cancellation_during_reply_wait_does_not_start_more_discovery)
{
    EngineFixture f; f.fake().setSonyServices(false, true); f.start();
    auto work = std::async(std::launch::async, [&] { try { f.hp.queryState(); } catch (const RecoverableException&) { return true; } return false; });
    CHECK(testing::waitFor([&] { return wasSent(f.fake(), "00 00"); }));
    f.stopSession();
    CHECK(work.get());
    f.hp.resetState();
    CHECK(!f.hp.hasPendingQueries());
}

TEST(v2session_ambient_extended_mode_preserves_fields_and_confirms_readback)
{
    EngineFixture f; DeviceScript script;
    f.fake().setSonyServices(false, true);
    script.on("00 00", replies::hex("01 00 03 00 30 37 00 01"))
        .on("02 00", replies::hex("03 00 01 01 78"))
        .on("06 00", replies::hex("07 00 01 6D 01"))
        .on("60 19", replies::hex("61 19 01 00 01 14 01"))
        .on("62 19", replies::hex("63 19 00"))
        .on("66 19", replies::hex("67 19 01 01 00 00 0A 01 02"));
    script.installOn(f.fake()); f.start(); f.hp.queryState();
    for (int i=0; i<4; ++i) { f.hp.sendPendingQueries(); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    CHECK_EQ(f.hp.getV2Snapshot().ambient.size(), size_t(1));
    CHECK(f.hp.getV2Snapshot().ambient[0].writable);
    CHECK(!f.hp.setV2Ambient(0x19, SonyV2Ambient::Mode::Ambient, 21, false));
    CHECK(f.hp.setV2Ambient(0x19, SonyV2Ambient::Mode::Ambient, 8, false));
    f.hp.setChanges();
    CHECK(wasSent(f.fake(), "68 19 01 01 01 00 08 01 02"));
    CHECK(f.hp.getV2Snapshot().ambient[0].status == SettingStatus::Acknowledged);
    script.remove("66 19"); script.on("66 19", replies::hex("67 19 01 01 01 00 08 01 02"));
    f.hp.sendPendingQueries();
    CHECK(testing::waitFor([&] { return f.hp.getV2Snapshot().ambient[0].status == SettingStatus::Confirmed; }));
    CHECK(deliver(f, replies::hex("65 19 01")));
    CHECK(!f.hp.setV2Ambient(0x19, SonyV2Ambient::Mode::Off, 8, false));
}

TEST(v2session_curve_requires_band_metadata_and_preserves_readback_truth)
{
    EngineFixture f; DeviceScript script; setupV2(f, script, false);
    CHECK(!f.hp.setV2EqBand(0, 0, 1));
    CHECK(deliver(f, replies::hex("5B 00 06 10 00 01 01 01 90 02 00 01 01 09 C4 01 18 9C 02 00 10")));
    CHECK(f.hp.getV2Snapshot().equalizers[0].bandsEditable);
    CHECK(!f.hp.setV2EqBand(0, 6, 1)); CHECK(!f.hp.setV2EqBand(0, 0, 11));
    CHECK(f.hp.setV2EqBand(0, 0, 2));
    CHECK(!f.hp.setV2EqPreset(0, 0x10)); // pending curve owns this EQ operation
    f.hp.setChanges();
    CHECK(wasSent(f.fake(), "58 00 FF 06 0C 0A 0A 0A 0A 0A"));
    auto view = f.hp.getV2Snapshot().equalizers[0];
    CHECK_EQ(view.levels[0], 12); CHECK_EQ(view.reported->levels[0], 10);
    CHECK(view.status == SettingStatus::Acknowledged);
    f.hp.sendPendingQueries(); // unchanged state from the headphones wins
    CHECK(testing::waitFor([&] { return f.hp.getV2Snapshot().equalizers[0].status == SettingStatus::Confirmed; }));
    CHECK(f.hp.getV2Snapshot().equalizers[0].levels[0] == 10);
    CHECK(deliver(f, replies::hex("5B 00 01 00 00 00")));
    CHECK(!f.hp.setV2EqBand(0, 0, 2));
}

TEST(v2session_custom_ten_band_layout_is_described_and_edited_without_v1_preset_bytes)
{
    EngineFixture f; DeviceScript script;
    f.fake().setSonyServices(false, true);
    Buffer report{0x57, 0x31, 10}; report.insert(report.end(), 10, 10);
    Buffer metadata{0x5b, 0x31, 10};
    for (int i=1; i<=10; ++i) { metadata.push_back(1); metadata.push_back(0); metadata.push_back(static_cast<char>(i)); }
    script.on("00 00", replies::hex("01 00 03 00 30 37 00 01"))
        .on("02 00", replies::hex("03 00 01 01 78"))
        .on("06 00", replies::hex("07 00 01 55 01"))
        .on("50 31", replies::hex("51 31 0A 15"))
        .on("52 31", replies::hex("53 31 00"))
        .on("56 31", report).on("5A 31", metadata);
    script.installOn(f.fake()); f.start(); f.hp.queryState();
    for (int i=0;i<4;++i) { f.hp.sendPendingQueries(); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    CHECK(f.hp.getV2Snapshot().equalizers[0].bandsEditable);
    CHECK(!f.hp.setV2EqPreset(0x31, 0xa1));
    CHECK(f.hp.setV2EqBand(0x31, 9, 3));
    f.hp.setChanges();
    CHECK(wasSent(f.fake(), "58 31 0A 0A 0A 0A 0A 0A 0A 0A 0A 0A 0D"));
    CHECK(!wasSent(f.fake(), "58 01 A1 00"));
}

TEST(v2session_withdrawn_function_cancels_a_queued_write_at_send_time)
{
    EngineFixture f; DeviceScript script; setupV2(f, script, false);
    CHECK(f.hp.setV2EqPreset(0, 0x10));
    CHECK(deliver(f, replies::hex("07 00 01 20 01")));
    bool failed = false;
    try { f.hp.setChanges(); } catch (const RecoverableException&) { failed = true; }
    CHECK(failed);
    CHECK(!wasSent(f.fake(), "58 00 10 00"));
    CHECK(!f.hp.getV2Snapshot().equalizers[0].writable);
    CHECK(!f.hp.isChanged());
}

TEST(v2session_audio_is_gated_and_confirms_from_readback)
{
    EngineFixture f; DeviceScript script; setupV2(f, script, false);
    CHECK(!f.hp.setV2Audio(0, 1));
    CHECK(deliver(f, replies::hex("07 00 04 E1 01 E2 02 12 03 13 04")));
    CHECK(deliver(f, replies::hex("E1 01 03")));
    CHECK(deliver(f, replies::hex("E3 00 00")));
    CHECK(deliver(f, replies::hex("E3 01 00")));
    CHECK(deliver(f, replies::hex("E7 00 00")));
    CHECK(deliver(f, replies::hex("E7 01 00")));
    CHECK(deliver(f, replies::hex("13 02 10")));
    CHECK(deliver(f, replies::hex("13 03 03 02")));
    CHECK(f.hp.getV2Snapshot().codec->codec == 0x10);
    CHECK(f.hp.getV2Snapshot().effect->status == 2);
    CHECK(!f.hp.setV2Audio(0, 2));
    CHECK(f.hp.setV2Audio(0, 1)); CHECK(f.hp.setV2Audio(1, 1));
    f.hp.setChanges();
    CHECK(wasSent(f.fake(), "E8 00 01")); CHECK(wasSent(f.fake(), "E8 01 01"));
    CHECK(f.hp.getV2Snapshot().audio[1].status == SettingStatus::Acknowledged);
    CHECK(deliver(f, replies::hex("E9 01 00")));
    CHECK(f.hp.getV2Snapshot().audio[1].value == 0);
    CHECK(f.hp.getV2Snapshot().audio[1].status == SettingStatus::Confirmed);
    CHECK(deliver(f, replies::hex("E5 01 01")));
    CHECK(!f.hp.setV2Audio(1, 1));
    f.hp.sendPendingQueries(); CHECK(wasSent(f.fake(), "E6 01")); CHECK(!wasSent(f.fake(), "E0 00"));
    f.stopSession(); f.hp.resetState(); CHECK(f.hp.getV2Snapshot().audio.empty());
}
