#include "AmbientProtocol.h"
#include "TestSupport.h"

namespace {
CommandSerializer::Message ambientMessage(const char* hex) {
    return {DATA_TYPE::DATA_MDR, 0, replies::hex(hex)};
}
CommandSerializer::NcAsmReport ambientReport(const char* hex) {
    return *CommandSerializer::parseNcAsm(ambientMessage(hex));
}
}

TEST(ambientcap_preserves_representation_and_per_mode_ranges)
{
    const auto cap = SonyAmbient::parseCapability(ambientMessage("61 02 02 03 01 02 00 14 01 02"));
    CHECK(cap);
    CHECK(cap->supports(0) && cap->supports(1));
    CHECK_EQ(cap->maximum(0), 20);
    CHECK_EQ(cap->maximum(1), 2);
    CHECK(SonyAmbient::queryCapability() == replies::hex("60 02"));
    CHECK(SonyAmbient::queryState() == replies::hex("66 02"));
    const auto current = ambientReport("67 02 01 02 02 01 00 00");
    auto wire = SonyAmbient::buildModeSet(*cap, current, SonyAmbient::Mode::Ambient, 20, 0);
    CHECK(wire && *wire == replies::hex("68 02 11 02 00 01 00 14"));
    CHECK(!SonyAmbient::buildModeSet(*cap, current, SonyAmbient::Mode::Ambient, 21, 0));
    CHECK(!SonyAmbient::buildModeSet(*cap, current, SonyAmbient::Mode::Ambient, 3, 1));
    CHECK(!SonyAmbient::buildModeSet(*cap, current, SonyAmbient::Mode::Ambient, 0, 0));
    wire = SonyAmbient::buildModeSet(*cap, current, SonyAmbient::Mode::Ambient, 2, 1);
    CHECK(wire && *wire == replies::hex("68 02 11 02 00 01 01 02"));
}

TEST(ambientcap_mode_changes_keep_unedited_fields_and_single_nc)
{
    const auto cap = *SonyAmbient::parseCapability(ambientMessage("61 02 02 03 01 02 00 14 01 02"));
    const auto current = ambientReport("69 02 01 02 01 01 01 00");
    auto wire = SonyAmbient::buildModeSet(cap, current, SonyAmbient::Mode::NoiseCancelling, 10, 1);
    CHECK(wire && *wire == replies::hex("68 02 11 02 01 01 01 00"));
    wire = SonyAmbient::buildModeSet(cap, current, SonyAmbient::Mode::Off, 10, 1);
    CHECK(wire && *wire == replies::hex("68 02 00 02 01 01 01 00"));
    SonyAmbient::RawEdit edit;
    edit.asmId = 0;
    wire = SonyAmbient::buildSet(cap, current, edit);
    CHECK(wire && *wire == replies::hex("68 02 01 02 01 01 00 00"));
    auto mismatched = current;
    mismatched.ncSettingType = 1;
    CHECK(!SonyAmbient::buildSet(cap, mismatched, edit));
}

TEST(ambientcap_onoff_and_unknown_level_mapping_are_distinct)
{
    const auto cap = *SonyAmbient::parseCapability(ambientMessage("61 02 00 00 00 01 00 00"));
    const auto current = ambientReport("67 02 01 00 01 00 00 00");
    CHECK_EQ(cap.maximum(0), 1);
    auto wire = SonyAmbient::buildModeSet(cap, current, SonyAmbient::Mode::Ambient, 1, 0);
    CHECK(wire && *wire == replies::hex("68 02 11 00 00 00 00 01"));
    CHECK(!SonyAmbient::buildModeSet(cap, current, SonyAmbient::Mode::Ambient, 2, 0));
    const auto level = *SonyAmbient::parseCapability(ambientMessage("61 02 01 03 01 01 00 14"));
    CHECK(!level.modeWritesSupported());
    CHECK(!SonyAmbient::buildModeSet(level, current, SonyAmbient::Mode::NoiseCancelling, 0, 0));
}

TEST(ambientcap_rejects_truncation_tables_duplicates_and_invalid_enums)
{
    auto m = ambientMessage("61 02 02 03 01 02 00 14 01 02");
    const auto original = m.messageBytes;
    for (size_t n = 0; n < original.size(); ++n) {
        m.messageBytes.assign(original.begin(), original.begin() + n);
        CHECK(!SonyAmbient::parseCapability(m));
    }
    m.messageBytes = original;
    m.messageBytes.push_back(0);
    CHECK(!SonyAmbient::parseCapability(m));
    m.messageBytes = original;
    m.dataType = DATA_TYPE::DATA_MDR_NO2;
    CHECK(!SonyAmbient::parseCapability(m));
    for (const char* invalid : {"61 01 02 03 01 01 00 14", "61 02 03 03 01 01 00 14",
         "61 02 02 03 02 01 00 14", "61 02 02 03 01 01 02 14",
         "61 02 02 03 01 02 00 14 00 02", "61 02 02 03 01 01 00 00"})
        CHECK(!SonyAmbient::parseCapability(ambientMessage(invalid)));
}
