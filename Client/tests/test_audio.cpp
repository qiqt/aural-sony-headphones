#include "AudioProtocol.h"
#include "TestSupport.h"

namespace
{
CommandSerializer::Message audioMessage(const char* text)
{
    return { DATA_TYPE::DATA_MDR, 0, replies::hex(text) };
}
template<class Parser> void rejectsBrokenAudio(const char* valid, Parser parse)
{
    auto m = audioMessage(valid);
    CHECK(parse(m).has_value());
    const auto original = m.messageBytes;
    for (size_t n = 0; n < original.size(); ++n)
    {
        m.messageBytes.assign(original.begin(), original.begin() + n);
        CHECK(!parse(m).has_value());
    }
    m.messageBytes = original;
    m.messageBytes.push_back(0);
    CHECK(!parse(m).has_value());
    m.messageBytes = original;
    m.dataType = DATA_TYPE::DATA_MDR_NO2;
    CHECK(!parse(m).has_value());
}
}

TEST(audio_connection_preference_and_availability)
{
    using namespace SonyAudio;
    CHECK(connectionCapabilityQuery() == replies::hex("E0 01"));
    CHECK(connectionAvailabilityQuery() == replies::hex("E2 01"));
    CHECK(connectionModeQuery() == replies::hex("E6 01"));
    CHECK(setConnectionMode(ConnectionMode::SoundQuality) == replies::hex("E8 01 00 00"));
    CHECK(setConnectionMode(ConnectionMode::ConnectionStability) == replies::hex("E8 01 00 01"));
    CHECK(parseConnectionMode(audioMessage("E7 01 00 00")) == ConnectionMode::SoundQuality);
    CHECK(parseConnectionMode(audioMessage("E9 01 00 01")) == ConnectionMode::ConnectionStability);
    CHECK(parseConnectionAvailability(audioMessage("E3 01 00")) == true);
    CHECK(parseConnectionAvailability(audioMessage("E5 01 01")) == false);
    rejectsBrokenAudio("E1 01 00", parseConnectionCapability);
    rejectsBrokenAudio("E3 01 00", parseConnectionAvailability);
    rejectsBrokenAudio("E7 01 00 00", parseConnectionMode);
    CHECK(!parseConnectionCapability(audioMessage("E1 01 01")));
    CHECK(!parseConnectionAvailability(audioMessage("E5 01 02")));
    CHECK(!parseConnectionMode(audioMessage("E9 01 00 02")));
    CHECK(!parseConnectionMode(audioMessage("E9 02 00 01"))); // DSEE is not preference
    bool threw = false;
    try { (void)setConnectionMode(static_cast<ConnectionMode>(2)); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

TEST(audio_codec_and_effect_are_distinct_from_settings)
{
    using namespace SonyAudio;
    CHECK(codecQuery() == replies::hex("18 00"));
    CHECK(dseeActivityQuery() == replies::hex("14 00"));
    CHECK(parseCodec(audioMessage("19 00 02")) == 0x02); // XM4 read capture: AAC
    for (const auto* p : { "1B 00 00", "1B 00 01", "1B 00 10", "1B 00 20", "1B 00 21", "1B 00 FF" })
        CHECK(parseCodec(audioMessage(p)));
    CHECK(!parseCodec(audioMessage("1B 00 03")));
    CHECK(!parseCodec(audioMessage("1B 01 02")));
    CHECK(parseDseeActivity(audioMessage("15 00 02 00")) == (DseeActivity{2, 0})); // captured
    CHECK(parseDseeActivity(audioMessage("17 00 02 01")) == (DseeActivity{2, 1}));
    CHECK(parseDseeActivity(audioMessage("17 00 00 02")) == (DseeActivity{0, 2}));
    CHECK(!parseDseeActivity(audioMessage("E9 02 00 01")));
    CHECK(!parseDseeActivity(audioMessage("17 01 02 01")));
    CHECK(!parseDseeActivity(audioMessage("17 00 03 01")));
    CHECK(!parseDseeActivity(audioMessage("17 00 02 03")));
    rejectsBrokenAudio("19 00 02", parseCodec);
    rejectsBrokenAudio("15 00 02 00", parseDseeActivity);
}

TEST(audio_discovery_gates_writes_and_requests_only_advertised_features)
{
    using SonyAudio::ConnectionMode;
    EngineFixture f;
    f.start();
    CHECK(!f.hp.connectionModeReady());
    CHECK(!f.hp.setConnectionMode(ConnectionMode::ConnectionStability));
    CHECK(deliver(f, replies::hex("07 01 03 E1 13 12")));
    f.hp.sendPendingQueries();
    for (const auto* request : { "E0 01", "E2 01", "E6 01", "18 00", "14 00" })
        CHECK(wasSent(f.fake(), request));
    CHECK(!wasSent(f.fake(), "E6 02")); // effect support does not imply the DSEE setting
    CHECK(deliver(f, replies::hex("E1 01 00")));
    CHECK(deliver(f, replies::hex("E7 01 00 00")));
    CHECK(!f.hp.connectionModeReady()); // availability still unknown
    CHECK(deliver(f, replies::hex("E3 01 01")));
    CHECK(!f.hp.setConnectionMode(ConnectionMode::ConnectionStability));
    CHECK(deliver(f, replies::hex("E5 01 00")));
    CHECK(f.hp.connectionModeReady());
    CHECK(f.hp.setConnectionMode(ConnectionMode::ConnectionStability));
    CHECK(deliver(f, replies::hex("E5 01 01"))); // gate changes after the edit was queued
    bool refused = false;
    try { f.hp.setChanges(); }
    catch (const RecoverableException&) { refused = true; }
    CHECK(refused);
    CHECK(!wasSent(f.fake(), "E8 01 00 01"));
    CHECK_EQ(f.hp.getStatuses().connectionMode, SettingStatus::Failed);
    CHECK(!f.hp.pendingConnectionMode());
}

TEST(audio_preference_write_reads_back_and_indicators_reset_with_session)
{
    using SonyAudio::ConnectionMode;
    EngineFixture f;
    DeviceScript script;
    script.on("E6 01", replies::hex("E7 01 00 01"));
    script.installOn(f.fake());
    f.start();
    CHECK(deliver(f, replies::hex("07 01 03 E1 13 12")));
    // Drain discovery first, then establish the old setting independently.
    f.hp.sendPendingQueries();
    CHECK(deliver(f, replies::hex("E1 01 00")));
    CHECK(deliver(f, replies::hex("E3 01 00")));
    CHECK(deliver(f, replies::hex("E7 01 00 00")));
    CHECK(f.hp.setConnectionMode(ConnectionMode::ConnectionStability));
    CHECK(f.hp.pendingConnectionMode() == ConnectionMode::ConnectionStability);
    f.hp.setChanges();
    CHECK(f.hp.pendingConnectionMode() == ConnectionMode::ConnectionStability);
    CHECK(wasSent(f.fake(), "E8 01 00 01"));
    CHECK(f.hp.getConnectionMode() == ConnectionMode::SoundQuality); // ACK is not read-back
    f.hp.sendPendingQueries();
    CHECK(testing::waitFor([&] { return f.hp.getConnectionMode() == ConnectionMode::ConnectionStability; }));
    CHECK_EQ(f.hp.getStatuses().connectionMode, SettingStatus::Confirmed);
    CHECK(!f.hp.pendingConnectionMode());
    CHECK(deliver(f, replies::hex("19 00 02")));
    CHECK(deliver(f, replies::hex("15 00 02 00")));
    CHECK(f.hp.getCodec() == 0x02);
    CHECK(f.hp.getDseeActivity() == (SonyAudio::DseeActivity{2, 0}));
    CHECK(deliver(f, replies::hex("1B 00 10")));
    CHECK(deliver(f, replies::hex("17 00 02 01")));
    CHECK(f.hp.getCodec() == 0x10);
    CHECK(f.hp.getDseeActivity() == (SonyAudio::DseeActivity{2, 1}));
    CHECK_EQ(f.hp.getStatuses().dsee, SettingStatus::Unknown); // activity is not configuration
    CHECK(deliver(f, replies::hex("1B 00 01"), DATA_TYPE::DATA_MDR_NO2));
    CHECK(f.hp.getCodec() == 0x10);
    f.stopSession();
    f.hp.resetState();
    CHECK(!f.hp.getCodec());
    CHECK(!f.hp.getDseeActivity());
    CHECK(!f.hp.getConnectionMode());
    CHECK(!f.hp.connectionModeReady());
}
