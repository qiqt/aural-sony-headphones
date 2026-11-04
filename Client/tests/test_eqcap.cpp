#include "TestSupport.h"
#include "EqualizerProtocol.h"

namespace {
CommandSerializer::Message message(const Buffer& p, DATA_TYPE table = DATA_TYPE::DATA_MDR) {
    return {table, 0, p};
}
const Buffer capture = replies::hex("51 01 06 15 0C 00 00 10 00 11 00 12 00 13 00 14 00 15 00 16 00 17 00 A0 00 A1 00 A2 00");
}

TEST(eqcap_real_xm4_capability_and_gain_conversion)
{
    const auto cap = SonyEq::parseCapability(message(capture));
    CHECK(cap.has_value());
    CHECK_EQ(cap->bands, 6);
    CHECK_EQ(cap->levelSteps, 21);
    CHECK_EQ(cap->presets.size(), size_t(12));
    CHECK(cap->supports(0xA1));
    CHECK(cap->supports(0xA2));
    CHECK(!cap->supports(0xFF));
    CHECK_EQ(cap->minimum(), -10);
    CHECK_EQ(cap->maximum(), 10);
    const auto signedLevels = SonyEq::decodeLevels(*cap, {18, 9, 11, 10, 13, 15});
    CHECK(signedLevels.has_value());
    CHECK(*signedLevels == (std::vector<int>{8, -1, 1, 0, 3, 5}));
    CHECK(SonyEq::editCurve(*cap, *signedLevels) == std::optional<Buffer>(replies::hex("58 01 FF 06 12 09 0B 0A 0D 0F")));
    CHECK(!SonyEq::editCurve(*cap, {11, 0, 0, 0, 0, 0}));
    CHECK(!SonyEq::editCurve(*cap, {0, 0, 0}));
    CHECK(!SonyEq::decodeLevels(*cap, {21, 10, 10, 10, 10, 10}));
}

TEST(eqcap_rejects_truncation_counts_and_wrong_tables)
{
    for (size_t i = 0; i < capture.size(); ++i)
        CHECK(!SonyEq::parseCapability(message(Buffer(capture.begin(), capture.begin() + i))));
    CHECK(!SonyEq::parseCapability(message(capture, DATA_TYPE::DATA_MDR_NO2)));
    auto extra = capture; extra.push_back(0);
    CHECK(!SonyEq::parseCapability(message(extra)));
    CHECK(!SonyEq::parseCapability(message(replies::hex("51 01 06 15 02 A1 00 A1 00"))));
    CHECK(!SonyEq::parseCapability(message(replies::hex("51 01 06 00 00"))));
    CHECK(!SonyEq::parseCapability(message(replies::hex("51 01 06 15 01 A1 81"))));
    const auto named = SonyEq::parseCapability(message(replies::hex("51 01 06 15 01 A1 03 4F 6E 65")));
    CHECK(named && named->presets[0].name == "One");
}

TEST(eqcap_power_off_capability_preserves_advertised_choices)
{
    const Buffer p = replies::hex("F1 04 02 10 11");
    const auto ids = SonyEq::parsePowerOffCapability(message(p));
    CHECK(ids && *ids == (std::vector<unsigned char>{0x10, 0x11}));
    for (size_t i = 0; i < p.size(); ++i)
        CHECK(!SonyEq::parsePowerOffCapability(message(Buffer(p.begin(), p.begin() + i))));
    CHECK(!SonyEq::parsePowerOffCapability(message(p, DATA_TYPE::DATA_MDR_NO2)));
    CHECK(!SonyEq::parsePowerOffCapability(message(replies::hex("F1 04 02 10 10"))));
    CHECK(!SonyEq::parsePowerOffCapability(message(replies::hex("F1 04 01 FF"))));
    CHECK(!SonyEq::parsePowerOffCapability(message(replies::hex("F1 04 01 10 11"))));
}

namespace {
void readyToSave(EngineFixture& f) {
    f.start();
    CHECK(deliver(f, replies::capabilities({FUNCTION_TYPE::PRESET_EQ})));
    CHECK(deliver(f, capture));
    CHECK(deliver(f, replies::hex("57 01 A0 06 0A 0A 0A 0A 0A 0A")));
}
}

TEST(eqcap_save_waits_for_selection_and_verifies_exact_curve)
{
    EngineFixture f;
    readyToSave(f);
    CHECK(f.hp.saveEqualizerCurve(0xA2, 1, {2,3,4,5,6}));
    f.hp.setChanges();
    CHECK(wasSent(f.fake(), "58 01 A2 00"));
    CHECK(!wasSent(f.fake(), "58 01 FF 06 0B 0C 0D 0E 0F 10"));
    CHECK(f.hp.equalizerSavePending());
    CHECK_EQ(f.hp.getStatuses().equalizer, SettingStatus::Requested);
    CHECK(deliver(f, replies::hex("57 01 A1 06 0A 0A 0A 0A 0A 0A")));
    f.hp.setChanges();
    CHECK(!wasSent(f.fake(), "58 01 FF 06 0B 0C 0D 0E 0F 10"));
    CHECK(deliver(f, replies::hex("57 01 A2 06 0A 0A 0A 0A 0A 0A")));
    CHECK(f.hp.isChanged());
    f.hp.setChanges();
    CHECK(wasSent(f.fake(), "58 01 FF 06 0B 0C 0D 0E 0F 10"));
    CHECK(deliver(f, replies::hex("57 01 FF 06 0A 0A 0A 0A 0A 0A")));
    CHECK(f.hp.equalizerSavePending());
    CHECK_EQ(f.hp.getStatuses().equalizer, SettingStatus::Requested);
    CHECK(deliver(f, replies::hex("57 01 FF 06 0B 0C 0D 0E 0F 10")));
    CHECK(!f.hp.equalizerSavePending());
    CHECK_EQ(f.hp.getStatuses().equalizer, SettingStatus::Confirmed);
    CHECK_EQ(f.hp.getEqualizer().preset, 0xA2);
    CHECK_EQ(f.hp.getEqualizer().clearBass, 1);
    CHECK(f.hp.getEqualizer().bands == (std::vector<int>{2,3,4,5,6}));
    f.hp.setChanges();
    CHECK_EQ(countSent(f.fake(), "58 01 FF 06 0B 0C 0D 0E 0F 10"), size_t(1));
}

TEST(eqcap_save_rejects_unknown_and_cancels_on_new_edit_or_reset)
{
    EngineFixture unknown;
    CHECK(!unknown.hp.saveEqualizerCurve(0xA1, 0, {0,0,0,0,0}));
    EngineFixture f;
    readyToSave(f);
    CHECK(!f.hp.saveEqualizerCurve(0xA0, 0, {0,0,0,0,0}));
    CHECK(!f.hp.saveEqualizerCurve(0xA1, 11, {0,0,0,0,0}));
    CHECK(deliver(f, replies::hex("51 01 06 15 01 A1 00")));
    CHECK(!f.hp.saveEqualizerCurve(0xA2, 0, {0,0,0,0,0}));
    CHECK(f.hp.saveEqualizerCurve(0xA1, 1, {0,0,0,0,0}));
    f.hp.setChanges();
    f.hp.setEqualizer({0xA1, 0, {0,0,0,0,0}});
    CHECK(!f.hp.equalizerSavePending());
    CHECK(deliver(f, replies::hex("57 01 A1 06 0A 0A 0A 0A 0A 0A")));
    f.hp.setChanges();
    CHECK(!wasSent(f.fake(), "58 01 FF 06 0B 0A 0A 0A 0A 0A"));
    CHECK(f.hp.saveEqualizerCurve(0xA1, 1, {0,0,0,0,0}));
    f.hp.resetState();
    CHECK(!f.hp.equalizerSavePending());
    f.hp.setChanges();
    CHECK(!wasSent(f.fake(), "58 01 FF 06 0B 0A 0A 0A 0A 0A"));
}

TEST(eqcap_save_without_selection_reply_expires_without_replay)
{
    EngineFixture f;
    readyToSave(f);
    CHECK(f.hp.saveEqualizerCurve(0xA1, 1, {0,0,0,0,0}));
    f.hp.setChanges();
    std::this_thread::sleep_for(std::chrono::milliseconds(15020));
    CHECK(!f.hp.equalizerSavePending());
    CHECK_EQ(f.hp.getStatuses().equalizer, SettingStatus::Failed);
    CHECK(deliver(f, replies::hex("57 01 A1 06 0A 0A 0A 0A 0A 0A")));
    f.hp.setChanges();
    CHECK_EQ(countSent(f.fake(), "58 01 A1 00"), size_t(1));
    CHECK(!wasSent(f.fake(), "58 01 FF 06 0B 0A 0A 0A 0A 0A"));
}

TEST(eqcap_save_cancels_when_another_controller_changes_selected_slot)
{
    EngineFixture f;
    readyToSave(f);
    CHECK(f.hp.saveEqualizerCurve(0xA1, 1, {0,0,0,0,0}));
    CHECK(!f.hp.saveEqualizerCurve(0xA2, 2, {0,0,0,0,0}));
    f.hp.setChanges();
    CHECK(deliver(f, replies::hex("57 01 A1 06 0A 0A 0A 0A 0A 0A")));
    CHECK(f.hp.isChanged());
    CHECK(deliver(f, replies::hex("57 01 A2 06 0A 0A 0A 0A 0A 0A")));
    CHECK(!f.hp.equalizerSavePending());
    CHECK_EQ(f.hp.getStatuses().equalizer, SettingStatus::Failed);
    f.hp.setChanges();
    CHECK(!wasSent(f.fake(), "58 01 FF 06 0B 0A 0A 0A 0A 0A"));
}
