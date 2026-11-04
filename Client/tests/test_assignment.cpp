#include "AssignmentProtocol.h"
#include "TestSupport.h"

namespace {
CommandSerializer::Message message(const std::string& bytes, DATA_TYPE table = DATA_TYPE::DATA_MDR)
{
    return {table, 0, replies::hex(bytes)};
}
const std::string xm4Capability = "F1 06 01 02 01 00 03 00 02 00 01 21 02 31 03 00 31 01 33 22 32 32 01 00 34";
}

TEST(assignment_engine_discovers_gates_reads_back_and_resets)
{
    EngineFixture f;
    DeviceScript script;
    script.on("F0 06", replies::hex(xm4Capability))
        .on("F2 06", replies::hex("F3 06 01 00"))
        .on("F6 06", replies::hex("F7 06 01 00"));
    script.installOn(f.fake());
    f.start();
    CHECK(!f.hp.setAssignment(0, 0x31));
    CHECK(deliver(f, replies::hex("07 01 01 F6")));
    f.hp.sendPendingQueries();
    CHECK(testing::waitFor([&] { return f.hp.getAssignmentCapability().has_value(); }));
    f.hp.sendPendingQueries();
    CHECK(testing::waitFor([&] { return f.hp.getAssignments().has_value() && f.hp.getAssignmentAvailability().has_value(); }));
    CHECK(!f.hp.setAssignment(0, 0x20)); // not offered by this XM4 key
    CHECK(f.hp.setAssignment(0, 0x31));
    CHECK(f.hp.pendingAssignments()->at(0) == 0x31);
    CHECK(deliver(f, replies::hex("F5 06 01 01")));
    bool refused = false;
    try { f.hp.setChanges(); } catch (const RecoverableException&) { refused = true; }
    CHECK(refused);
    CHECK(!wasSent(f.fake(), "F8 06 01 31"));
    CHECK(deliver(f, replies::hex("F5 06 01 00")));
    CHECK(f.hp.setAssignment(0, 0x31));
    CHECK(f.hp.pendingAssignments()->at(0) == 0x31);
    f.hp.setChanges();
    CHECK(wasSent(f.fake(), "F8 06 01 31"));
    CHECK(f.hp.pendingAssignments()->at(0) == 0x31);
    CHECK(f.hp.getAssignments()->at(0) == 0); // transport ACK cannot change observed assignment
    script.remove("F6 06");
    script.on("F6 06", replies::hex("F7 06 01 31"));
    f.hp.sendPendingQueries();
    CHECK(testing::waitFor([&] { return f.hp.getAssignments()->at(0) == 0x31; }));
    CHECK_EQ(f.hp.getStatuses().assignment, SettingStatus::Confirmed);
    CHECK(!f.hp.pendingAssignments());
    f.stopSession();
    f.hp.resetState();
    CHECK(!f.hp.getAssignmentCapability());
    CHECK(!f.hp.getAssignments());
    CHECK(!f.hp.getAssignmentAvailability());
}

TEST(assignment_xm4_capability_from_hardware_capture)
{
    const auto cap = SonyAssignment::parseCapability(message(xm4Capability));
    CHECK(cap.has_value());
    CHECK_EQ(cap->keys.size(), size_t(1));
    CHECK_EQ(cap->keys[0].id, 2);
    CHECK_EQ(cap->keys[0].type, 1);
    CHECK_EQ(cap->keys[0].defaultPreset, 0);
    CHECK_EQ(cap->keys[0].presets.size(), size_t(3));
    CHECK_EQ(cap->keys[0].presets[1].id, 0x31);
    CHECK_EQ(cap->keys[0].presets[1].actions[2].action, 0x22);
    CHECK_EQ(cap->keys[0].presets[1].actions[2].function, 0x32);
    auto state = SonyAssignment::parseState(message("F7 06 01 00"));
    auto available = SonyAssignment::parseAvailability(message("F3 06 01 00"));
    CHECK(state && available);
    CHECK_EQ(state->at(0), 0);
    CHECK(available->at(0));
    auto write = SonyAssignment::buildSet(*cap, *state, 0, 0x32);
    CHECK(write && *write == replies::hex("F8 06 01 32"));
    CHECK(!SonyAssignment::buildSet(*cap, *state, 0, 0x10));
}

TEST(assignment_capability_rejects_truncation_tables_and_ambiguous_keys)
{
    auto valid = message(xm4Capability);
    for (size_t n = 0; n < valid.messageBytes.size(); ++n)
    {
        auto truncated = valid;
        truncated.messageBytes.resize(n);
        CHECK(!SonyAssignment::parseCapability(truncated));
    }
    valid.messageBytes.push_back(0);
    CHECK(!SonyAssignment::parseCapability(valid));
    CHECK(!SonyAssignment::parseCapability(message(xm4Capability, DATA_TYPE::DATA_MDR_NO2)));
    CHECK(!SonyAssignment::parseCapability(message("F1 05 00")));
    CHECK(!SonyAssignment::parseCapability(message("F1 06 02 00 01 00 01 00 00 00 01 00 01 00 00"))); // duplicate key
    CHECK(!SonyAssignment::parseCapability(message("F1 06 01 00 01 00 02 00 00 00 00"))); // duplicate preset
    CHECK(!SonyAssignment::parseCapability(message("F1 06 01 00 01 00 01 00 02 00 01 00 02"))); // duplicate action
}

TEST(assignment_state_and_availability_validate_counts_and_values)
{
    CHECK(SonyAssignment::parseState(message("F9 06 02 00 FF")));
    CHECK(!SonyAssignment::parseState(message("F7 06 02 00")));
    CHECK(!SonyAssignment::parseState(message("F7 06 01 FE")));
    CHECK(!SonyAssignment::parseState(message("F7 06 01 00 00")));
    CHECK(!SonyAssignment::parseState(message("F7 03 01 00")));
    CHECK(!SonyAssignment::parseState(message("F7 06 01 00", DATA_TYPE::DATA_MDR_NO2)));
    auto available = SonyAssignment::parseAvailability(message("F5 06 02 00 01"));
    CHECK(available && available->at(0) && !available->at(1));
    CHECK(!SonyAssignment::parseAvailability(message("F3 06 01 02")));
    CHECK(!SonyAssignment::parseAvailability(message("F3 06 02 00")));
    CHECK(!SonyAssignment::parseAvailability(message("F3 06 01 00", DATA_TYPE::DATA_MDR_NO2)));
}

TEST(assignment_write_preserves_capability_order_and_all_other_keys)
{
    // Right key comes first. Positions are capability order, not numerical key ID.
    auto cap = SonyAssignment::parseCapability(message("F1 06 02 01 01 20 02 20 00 10 00 00 00 00 02 00 00 FF 00"));
    CHECK(cap);
    const std::vector<SonyAssignment::Byte> state{0x20, 0x00};
    auto write = SonyAssignment::buildSet(*cap, state, 1, 0xff);
    CHECK(write && *write == replies::hex("F8 06 02 20 FF"));
    CHECK(!SonyAssignment::buildSet(*cap, {0x31, 0x00}, 1, 0xff)); // unrelated preserved key invalid
    CHECK(!SonyAssignment::buildSet(*cap, {0x20}, 0, 0x10));
    CHECK(!SonyAssignment::buildSet(*cap, state, 2, 0x10));
    CHECK(!SonyAssignment::buildSet(*cap, state, 0, 0xff));
}
