#include "V2AmbientProtocol.h"
#include "TestSupport.h"

namespace {
CommandSerializer::Message v2AmbientMessage(const char* hex) {
    return {DATA_TYPE::DATA_MDR, 0, replies::hex(hex)};
}
}
TEST(v2ambient_capability_ranges_and_queries)
{
    const auto cap = SonyV2Ambient::parseCapability(v2AmbientMessage("61 13 02 00 01 14 01 01 02 0A 02"));
    CHECK(cap);
    CHECK(cap->range(0)->contains(20));
    CHECK(!cap->range(0)->contains(0));
    CHECK(cap->range(1)->contains(10));
    CHECK(!cap->range(1)->contains(3));
    CHECK(!cap->range(2));
    CHECK(SonyV2Ambient::queryCapability(0x13) == replies::hex("60 13"));
    CHECK(SonyV2Ambient::queryStatus(0x14) == replies::hex("62 14"));
    CHECK(SonyV2Ambient::queryState(0x14) == replies::hex("66 14"));
    for (const char* bad : {"61 13 01 00 01 14", "61 13 00 00", "61 18 00",
        "61 13 01 02 01 14 01", "61 13 01 00 01 14 00", "61 13 01 00 14 01 01",
        "61 13 02 00 01 14 01 00 01 14 01"}) {
        CHECK(!SonyV2Ambient::parseCapability(v2AmbientMessage(bad)));
    }
}
TEST(v2ambient_explicit_modes_preserve_auxiliary_fields)
{
    using namespace SonyV2Ambient;
    const auto cap16 = *parseCapability(v2AmbientMessage("61 16 01 00 01 14 01"));
    const auto state16 = *parseState(v2AmbientMessage("67 16 01 01 00 01 00 0A"));
    CHECK(state16.mode() == Mode::NoiseCancelling);
    auto wire = buildModeSet(cap16, state16, Mode::Ambient, 20, 0, true);
    CHECK(wire && *wire == replies::hex("68 16 01 01 01 01 00 14"));
    const auto cap17 = *parseCapability(v2AmbientMessage("61 17 01 00 01 14 01"));
    const auto state17 = *parseState(v2AmbientMessage("69 17 01 01 01 00 0A"));
    CHECK(state17.mode() == Mode::Ambient);
    wire = buildModeSet(cap17, state17, Mode::NoiseCancelling, 20, 0, true);
    CHECK(wire && *wire == replies::hex("68 17 01 01 00 00 0A"));
    const auto cap19 = *parseCapability(v2AmbientMessage("61 19 01 00 01 14 01"));
    const auto state19 = *parseState(v2AmbientMessage("67 19 01 01 01 00 0A 01 02"));
    wire = buildModeSet(cap19, state19, Mode::NoiseCancelling, 20, 0, true);
    CHECK(wire && *wire == replies::hex("68 19 01 01 00 00 0A 01 02"));
    wire = buildModeSet(cap19, state19, Mode::Off, 20, 0, true);
    CHECK(wire && *wire == replies::hex("68 19 01 00 01 00 0A 01 02"));
    for (const char* bad : {"67 17 01 01 02 00 0A", "67 16 01 01 00 06 00 0A",
        "67 19 01 01 01 00 0A 02 02", "67 19 01 01 01 00 0A 01 03",
        "67 19 01 01 01 00 0A 01", "67 17 01 01 01 00 0A 00"}) {
        CHECK(!parseState(v2AmbientMessage(bad)));
    }
}
TEST(v2ambient_state_and_inverted_availability)
{
    auto state = SonyV2Ambient::parseState(v2AmbientMessage("67 14 01 01 02 00 00"));
    CHECK(state && state->mode() == SonyV2Ambient::Mode::NoiseCancelling);
    state = SonyV2Ambient::parseState(v2AmbientMessage("69 13 01 01 00 01 0A"));
    CHECK(state && state->mode() == SonyV2Ambient::Mode::Ambient);
    state = SonyV2Ambient::parseState(v2AmbientMessage("69 13 01 00 00 01 0A"));
    CHECK(state && state->mode() == SonyV2Ambient::Mode::Off);
    CHECK(SonyV2Ambient::parseAvailability(v2AmbientMessage("63 13 00"))->available);
    CHECK(!SonyV2Ambient::parseAvailability(v2AmbientMessage("65 14 01"))->available);
    CHECK(!SonyV2Ambient::parseAvailability(v2AmbientMessage("65 14 02")));
    for (const char* bad : {"67 13 01 01 02 00 00", "67 14 01 01 06 00 00",
        "67 14 02 01 02 00 00", "67 14 01 02 02 00 00", "67 14 01 01 02 02 00",
        "67 14 01 01 02 00", "67 14 01 01 02 00 00 00", "67 16 01 01 02 00 00"}) {
        CHECK(!SonyV2Ambient::parseState(v2AmbientMessage(bad)));
    }
}
TEST(v2ambient_writes_preserve_modes_and_validate_ranges)
{
    using namespace SonyV2Ambient;
    const auto cap = *parseCapability(v2AmbientMessage("61 14 02 00 01 14 01 01 02 0A 02"));
    const auto current = *parseState(v2AmbientMessage("67 14 01 01 01 01 00"));
    auto wire = buildModeSet(cap, current, Mode::NoiseCancelling, 20, 1, true);
    CHECK(wire && *wire == replies::hex("68 14 01 01 01 01 00"));
    wire = buildModeSet(cap, current, Mode::Ambient, 10, 1, true);
    CHECK(wire && *wire == replies::hex("68 14 01 01 00 01 0A"));
    wire = buildModeSet(cap, current, Mode::Off, 255, 0, true);
    CHECK(wire && *wire == replies::hex("68 14 01 00 01 01 00"));
    CHECK(!buildModeSet(cap, current, Mode::Ambient, 3, 1, true));
    CHECK(!buildModeSet(cap, current, Mode::Ambient, 21, 0, true));
    CHECK(!buildModeSet(cap, current, Mode::Ambient, 10, 2, true));
    CHECK(!buildModeSet(cap, current, Mode::Ambient, 10, 0, false));
    auto unstable = current;
    unstable.changeStatus = 0;
    CHECK(!buildModeSet(cap, unstable, Mode::Ambient, 10, 0, true));
    unstable = current;
    unstable.selector = 0x13;
    CHECK(!buildModeSet(cap, unstable, Mode::Ambient, 10, 0, true));
    const auto onoff = *parseCapability(v2AmbientMessage("61 13 01 00 01 14 01"));
    const auto ambient = *parseState(v2AmbientMessage("67 13 01 01 00 00 14"));
    wire = buildModeSet(onoff, ambient, Mode::NoiseCancelling, 0, 0, true);
    CHECK(wire && *wire == replies::hex("68 13 01 01 01 00 00"));
}
