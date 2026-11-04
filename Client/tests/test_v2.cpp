#include "V2Protocol.h"
#include "TestSupport.h"

namespace {
CommandSerializer::Message v2msg(const char* hex, DATA_TYPE table = DATA_TYPE::DATA_MDR) {
    return {table,0,replies::hex(hex)};
}
template<class Parser> void v2RejectsDamage(const char* text, Parser parser) {
    const auto valid = v2msg(text);
    CHECK(parser(valid).has_value());
    for (size_t n=0;n<valid.messageBytes.size();++n) {
        auto shortMessage=valid;
        shortMessage.messageBytes.resize(n);
        CHECK(!parser(shortMessage));
    }
    auto extra=valid;extra.messageBytes.push_back(0);
    CHECK(!parser(extra));
    auto wrongTable=valid;wrongTable.dataType=DATA_TYPE::DATA_MDR_NO2;
    CHECK(!parser(wrongTable));
}
}

TEST(v2_protocol_version_flags_and_capability_info_are_strict)
{
    using namespace SonyV2;
    const auto info=SonyV2::parseProtocolInfo(v2msg("01 00 03 00 30 37 00 01"));
    CHECK(info);CHECK_EQ(info->version,uint32_t(0x03003037));
    CHECK(info->table1Enabled);CHECK(!info->table2Enabled);
    CHECK(protocolVersionSupported(info->version));
    CHECK(!protocolVersionSupported(0x03003038));
    CHECK(!protocolVersionSupported(0x04000000));
    CHECK(!protocolVersionSupported(0));
    v2RejectsDamage("01 00 03 00 30 37 00 01",SonyV2::parseProtocolInfo);
    CHECK(!SonyV2::parseProtocolInfo(v2msg("01 00 03 00 30 37 02 00")));
    CHECK(!SonyV2::parseProtocolInfo(v2msg("01 01 03 00 30 37 00 00")));
    auto cap=SonyV2::parseCapabilityInfo(v2msg("03 00 07 03 61 62 63"));
    CHECK(cap);CHECK_EQ(cap->revision,7);CHECK_EQ(cap->identifier,std::string("abc"));
    v2RejectsDamage("03 00 07 03 61 62 63",SonyV2::parseCapabilityInfo);
    CHECK(!SonyV2::parseCapabilityInfo(v2msg("03 00 07 00")));
    CHECK(!SonyV2::parseCapabilityInfo(v2msg("03 00 07 01 FF")));
}

TEST(v2_supported_functions_preserve_table_and_order_without_v1_aliasing)
{
    using namespace SonyV2;
    auto table1=SonyV2::parseSupportedFunctions(v2msg("07 00 03 20 05 50 00 55 04"));
    CHECK(table1);CHECK(table1->supports(0x50));CHECK(!table1->supports(0x51));
    CHECK_EQ(table1->functions[0].order,5);
    CHECK_EQ(table1->functions[1].id,0x50);
    auto table2=SonyV2::parseSupportedFunctions(v2msg("07 00 01 50 09",DATA_TYPE::DATA_MDR_NO2));
    CHECK(table2);CHECK(table2->table==DATA_TYPE::DATA_MDR_NO2);
    CHECK(!SonyV2::parseSupportedFunctions(v2msg("07 01 01 50"))); // old V1 list
    CHECK(!SonyV2::parseSupportedFunctions(v2msg("07 00 02 50 00")));
    CHECK(!SonyV2::parseSupportedFunctions(v2msg("07 00 01 50 00 00")));
    CHECK(!SonyV2::parseSupportedFunctions(v2msg("07 00 02 50 00 50 01"))); // ambiguous duplicate
    CHECK(SonyV2::parseSupportedFunctions(v2msg("07 00 00")));
    CHECK(!SonyV2::parseSupportedFunctions(v2msg("07 00 00",DATA_TYPE::ACK)));
}

TEST(v2_identity_strings_and_battery_keep_unknown_charging_state)
{
    using namespace SonyV2;
    auto model=SonyV2::parseDeviceInfo(v2msg("05 01 0A 57 48 2D 31 30 30 30 58 4D 35"));
    CHECK(model);CHECK_EQ(model->value,std::string("WH-1000XM5"));
    v2RejectsDamage("05 02 05 31 2E 30 2E 30",SonyV2::parseDeviceInfo);
    CHECK(!SonyV2::parseDeviceInfo(v2msg("05 01 00")));
    CHECK(!SonyV2::parseDeviceInfo(v2msg("05 01 02 C0 AF"))); // overlong UTF-8
    CHECK(!SonyV2::parseDeviceInfo(v2msg("05 01 03 ED A0 80"))); // surrogate
    CHECK(!SonyV2::parseDeviceInfo(v2msg("05 01 01 0A"))); // UI control character
    CHECK(SonyV2::parseDeviceInfo(v2msg("05 01 02 C3 A9"))); // valid UTF-8
    auto battery=SonyV2::parseBattery(v2msg("23 00 64 02"));
    CHECK(battery);CHECK_EQ(battery->level,100);CHECK(!battery->chargingKnown);
    battery=SonyV2::parseBattery(v2msg("25 00 32 01"));
    CHECK(battery);CHECK(battery->charging);CHECK(battery->chargingKnown);
    v2RejectsDamage("23 00 32 00",SonyV2::parseBattery);
    CHECK(!SonyV2::parseBattery(v2msg("23 00 65 00")));
    CHECK(!SonyV2::parseBattery(v2msg("23 00 32 04")));
    CHECK(!SonyV2::parseBattery(v2msg("23 01 32 00"))); // left/right isn't single battery
}

TEST(v2_eq_variants_are_separate_and_reject_malformed_capabilities)
{
    using namespace SonyV2;
    auto preset=SonyV2::parseEqCapability(v2msg("51 00 06 15 02 00 00 A1 03 4F 6E 65"));
    CHECK(preset);CHECK_EQ(preset->bands,6);CHECK_EQ(preset->levelSteps,21);
    CHECK(preset->supports(0xa1));CHECK(!preset->supports(0xa2));
    CHECK_EQ(preset->presets[1].name,std::string("One"));
    auto custom=SonyV2::parseEqCapability(v2msg("51 31 0A 15"));
    CHECK(custom);CHECK_EQ(custom->bands,10);CHECK(custom->presets.empty());
    v2RejectsDamage("51 00 06 15 02 00 00 A1 03 4F 6E 65",SonyV2::parseEqCapability);
    v2RejectsDamage("51 31 0A 15",SonyV2::parseEqCapability);
    CHECK(!SonyV2::parseEqCapability(v2msg("51 01 06 15 00"))); // V1 EQ/ V2 EBB
    CHECK(!SonyV2::parseEqCapability(v2msg("51 00 06 15 02 00 00 00 00")));
    CHECK(!SonyV2::parseEqCapability(v2msg("51 00 06 15 01 00 01 FF")));
    CHECK(!SonyV2::parseEqCapability(v2msg("51 31 00 15")));
    CHECK(!SonyV2::parseEqCapability(v2msg("51 31 0A 00")));
}

TEST(v2_eq_readback_availability_and_writes_keep_raw_steps)
{
    using namespace SonyV2;
    const auto cap=SonyV2::parseEqCapability(v2msg("51 00 03 15 02 00 00 A1 00"));
    CHECK(cap);
    auto state=SonyV2::parseEqState(v2msg("57 00 A1 03 00 0A 14"));
    CHECK(state);CHECK(state->preset==0xa1);CHECK(eqStateFits(*cap,*state));
    auto wrongLevels=*state;wrongLevels.levels[2]=21;CHECK(!eqStateFits(*cap,wrongLevels));
    auto wrongPreset=*state;wrongPreset.preset=0xa2;CHECK(!eqStateFits(*cap,wrongPreset));
    v2RejectsDamage("57 00 A1 03 00 0A 14",SonyV2::parseEqState);
    v2RejectsDamage("59 31 03 00 0A 14",SonyV2::parseEqState);
    auto available=SonyV2::parseEqAvailability(v2msg("53 00 00"));
    CHECK(available && available->available);
    available=SonyV2::parseEqAvailability(v2msg("55 31 01"));CHECK(available && !available->available);
    v2RejectsDamage("53 00 00",SonyV2::parseEqAvailability);
    CHECK(!SonyV2::parseEqAvailability(v2msg("53 00 02")));
    CHECK(selectEqPreset(*cap,0xa1)==replies::hex("58 00 A1 00"));
    CHECK(!selectEqPreset(*cap,0xa2));
    CHECK(editEqCurve(*cap,{0,10,20})==replies::hex("58 00 FF 03 00 0A 14"));
    CHECK(!editEqCurve(*cap,{0,10,21}));
    CHECK(!editEqCurve(*cap,{0,10}));
    const auto custom=SonyV2::parseEqCapability(v2msg("51 31 03 15"));CHECK(custom);
    CHECK(editEqCurve(*custom,{0,10,20})==replies::hex("58 31 03 00 0A 14"));
    CHECK(!selectEqPreset(*custom,0));
    CHECK(queryProtocolInfo()==replies::hex("00 00"));
    CHECK(queryCapabilityInfo()==replies::hex("02 00"));
    CHECK(querySupportedFunctions()==replies::hex("06 00"));
    CHECK(queryBattery()==replies::hex("22 00"));
}

TEST(v2_eq_band_metadata_requires_complete_known_matching_descriptors)
{
    using namespace SonyV2;
    const auto cap = SonyV2::parseEqCapability(v2msg("51 00 03 15 00"));
    CHECK(cap);
    const auto metadata = SonyV2::parseEqBandMetadata(v2msg("5B 00 03 10 00 01 01 01 90 02 00 10"));
    CHECK(metadata);CHECK(eqBandMetadataFits(*cap,*metadata));
    CHECK_EQ(metadata->bands[0].kind,0x10);CHECK_EQ(metadata->bands[0].value,1);
    CHECK_EQ(metadata->bands[1].value,400);CHECK_EQ(metadata->bands[2].value,16);
    v2RejectsDamage("5B 00 03 10 00 01 01 01 90 02 00 10",SonyV2::parseEqBandMetadata);
    auto changed = *metadata;
    changed.selector = 0x31;CHECK(!eqBandMetadataFits(*cap,changed));
    changed = *metadata;changed.bands.pop_back();CHECK(!eqBandMetadataFits(*cap,changed));
    for (const auto descriptor : std::vector<EqBandDescriptor>{{0,0},{3,400},{0x10,2},{1,0},{2,0}}) {
        changed = *metadata;changed.bands[0] = descriptor;
        CHECK(!eqBandMetadataFits(*cap,changed));
    }
    const auto unknown = SonyV2::parseEqBandMetadata(v2msg("5B 00 01 FE 12 34"));
    CHECK(unknown);CHECK_EQ(unknown->bands[0].value,0x1234);CHECK(!unknown->bands[0].known());
    CHECK(!SonyV2::parseEqBandMetadata(v2msg("5B 01 01 01 01 90")));
    CHECK(!SonyV2::parseEqBandMetadata(v2msg("5B 00 02 01 01 90")));
    CHECK(SonyV2::queryEqBandMetadata(0x31)==replies::hex("5A 31"));
}
