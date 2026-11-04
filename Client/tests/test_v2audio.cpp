#include "V2AudioProtocol.h"
#include "TestSupport.h"

namespace {
CommandSerializer::Message audioMessage(const char* hex) { return {DATA_TYPE::DATA_MDR,0,replies::hex(hex)}; }
template<class Parser> void audioRejectsDamage(const char* hex,Parser parser) {
    const auto valid=audioMessage(hex);CHECK(parser(valid));
    for (size_t n=0;n<valid.messageBytes.size();++n) {
        auto broken=valid;broken.messageBytes.resize(n);CHECK(!parser(broken));
    }
    auto broken=valid;broken.messageBytes.push_back(0);CHECK(!parser(broken));
    broken=valid;broken.dataType=DATA_TYPE::DATA_MDR_NO2;CHECK(!parser(broken));
}
}

TEST(v2audio_capabilities_require_exact_advertised_variants)
{
    using namespace SonyV2;
    auto functions=SonyV2::parseSupportedFunctions(audioMessage("07 00 02 E1 01 E2 00"));
    CHECK(functions);
    auto connection=connectionAudioCapability(*functions);CHECK(connection);
    CHECK_EQ(connection->selector,0);CHECK(!connection->upscalingType);
    functions->table=DATA_TYPE::DATA_MDR_NO2;CHECK(!connectionAudioCapability(*functions));
    functions->table=DATA_TYPE::DATA_MDR;functions->functions={{0xe3,0}};
    CHECK(!connectionAudioCapability(*functions)); // LDAC-status variant is separate
    CHECK(!queryAudioCapability(0));CHECK(!queryAudioCapability(2));
    CHECK(queryAudioCapability(1)==replies::hex("E0 01"));
    auto dsee=parseAudioCapability(audioMessage("E1 01 03"));CHECK(dsee);
    CHECK(dsee->upscalingType==3);CHECK(audioCapabilityValid(*dsee));
    audioRejectsDamage("E1 01 02",parseAudioCapability);
    CHECK(!parseAudioCapability(audioMessage("E1 00 00")));
    CHECK(!parseAudioCapability(audioMessage("E1 01 04")));
    CHECK(!parseAudioCapability(audioMessage("E1 0B 02")));
}

TEST(v2audio_availability_state_and_set_are_distinct)
{
    using namespace SonyV2;
    auto availability=parseAudioAvailability(audioMessage("E3 00 00"));
    CHECK(availability && availability->available);
    availability=parseAudioAvailability(audioMessage("E5 01 01"));CHECK(availability && !availability->available);
    audioRejectsDamage("E3 01 00",parseAudioAvailability);
    auto state=parseAudioState(audioMessage("E7 00 01"));CHECK(state);CHECK_EQ(state->value,1);
    auto dseeState=parseAudioState(audioMessage("E9 01 00"));CHECK(dseeState);
    audioRejectsDamage("E7 01 01",parseAudioState);
    CHECK(!parseAudioState(audioMessage("E3 01 00")));
    CHECK(!parseAudioAvailability(audioMessage("E7 01 00")));
    CHECK(!parseAudioAvailability(audioMessage("E3 01 02")));
    CHECK(!parseAudioState(audioMessage("E7 00 02"))); // no low-latency variant
    CHECK(!parseAudioState(audioMessage("E7 02 00")));
    const AudioCapability connection{0,{}},dsee{1,2};
    CHECK(audioStateFits(connection,*state));CHECK(!audioStateFits(dsee,*state));
    CHECK(setAudioState(connection,0)==replies::hex("E8 00 00"));
    CHECK(setAudioState(connection,1)==replies::hex("E8 00 01"));
    CHECK(setAudioState(dsee,0)==replies::hex("E8 01 00"));
    CHECK(setAudioState(dsee,1)==replies::hex("E8 01 01"));
    CHECK(!setAudioState(dsee,2));CHECK(!setAudioState(AudioCapability{1,{}},0));
    CHECK(!setAudioState(AudioCapability{1,4},0));CHECK(!setAudioState(AudioCapability{2,{}},0));
    CHECK(queryAudioAvailability(0)==replies::hex("E2 00"));
    CHECK(queryAudioState(1)==replies::hex("E6 01"));
}

TEST(v2audio_common_codec_and_upscaling_effect_remain_separate_from_settings)
{
    using namespace SonyV2;
    audioRejectsDamage("13 02 10",parseCodecStatus);
    auto codec=parseCodecStatus(audioMessage("15 02 30"));CHECK(codec);CHECK_EQ(codec->codec,0x30);
    for (Byte value : {0,1,2,0x10,0x20,0x21,0x30,0xff}) {
        auto message=audioMessage("13 02 00");message.messageBytes[2]=static_cast<char>(value);
        CHECK(parseCodecStatus(message));
    }
    CHECK(!parseCodecStatus(audioMessage("13 02 03")));
    CHECK(!parseCodecStatus(audioMessage("13 02 FE")));
    audioRejectsDamage("13 03 02 01",parseUpscalingEffect);
    auto effect=parseUpscalingEffect(audioMessage("15 03 03 02"));CHECK(effect);
    CHECK_EQ(effect->type,3);CHECK_EQ(effect->status,2); // invalid != off or disabled
    CHECK(!parseUpscalingEffect(audioMessage("13 03 04 00")));
    CHECK(!parseUpscalingEffect(audioMessage("13 03 00 03")));
    CHECK(!parseAudioState(audioMessage("13 03 02 01")));
    CHECK(queryCodecStatus()==replies::hex("12 02"));
    CHECK(queryUpscalingEffect()==replies::hex("12 03"));
}
