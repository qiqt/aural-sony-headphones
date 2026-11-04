// SON-11: model identity from a (user-editable) Bluetooth name, and capability handling.

#include "TestSupport.h"
#include "ProtocolTrace.h"

TEST(model_known_product_codes_are_recognised)
{
	auto xm4 = identifyDevice("WH-1000XM4");
	CHECK_EQ(xm4.recognised, true);
	CHECK(xm4.model == SonyModel::WH_1000XM4);
	CHECK(xm4.protocol == SonyProtocol::V1);
	CHECK_EQ(std::string(xm4.marketingName), std::string("WH-1000XM4"));

	auto xm5 = identifyDevice("WH-1000XM5");
	CHECK_EQ(xm5.recognised, true);
	CHECK(xm5.protocol == SonyProtocol::V2);
}

TEST(model_separators_and_case_do_not_matter)
{
	for (const char* name : { "wh1000xm4", "WH_1000XM4", "WH 1000XM4", "Sony WH-1000XM4 (LE)" })
	{
		auto id = identifyDevice(name);
		CHECK_MSG(id.recognised, std::string("should recognise ") + name);
		CHECK(id.model == SonyModel::WH_1000XM4);
	}
}

// The regression this replaces: any name containing "XM5" was classified as a WH-1000XM5 and
// switched to the V2 protocol - including headphones the user simply renamed.
TEST(model_a_bare_suffix_is_not_an_identity)
{
	auto id = identifyDevice("My XM5");
	CHECK_EQ(id.recognised, false);
	CHECK(id.model == SonyModel::Unknown);
	CHECK_EQ(std::string(id.marketingName), std::string(""));
}

TEST(model_renamed_device_is_unknown_and_gets_the_legacy_attempt)
{
	auto id = identifyDevice("Leon's Headphones");
	CHECK_EQ(id.recognised, false);
	CHECK(id.model == SonyModel::Unknown);
	// V1 is the protocol we attempt, not a claim about the device.
	CHECK(id.protocol == SonyProtocol::V1);
}

TEST(model_empty_name_is_unknown)
{
	auto id = identifyDevice("");
	CHECK_EQ(id.recognised, false);
	CHECK(id.protocol == SonyProtocol::V1);
}

TEST(model_capabilities_are_unknown_until_the_device_answers)
{
	EngineFixture f;
	f.start();

	CHECK_EQ(f.hp.capabilitiesKnown(), false);
	CHECK(f.hp.featureState(DEVICE_CAPABILITIES::MULTI_POINT) == CapabilityState::Unknown);
	// Unknown does not hide a control - most v1 devices never answer this query.
	CHECK_EQ(f.hp.supportsFeature(DEVICE_CAPABILITIES::MULTI_POINT), true);
}

TEST(model_capability_reply_distinguishes_supported_from_unsupported)
{
	EngineFixture f;
	f.start();

	f.fake().enqueueData(replies::capabilities({ FUNCTION_TYPE::NC_ASM, FUNCTION_TYPE::OPTIMIZER }));
	CHECK(testing::waitFor([&] { return f.hp.capabilitiesKnown(); }));

	CHECK(f.hp.featureState(DEVICE_CAPABILITIES::NC_ASM) == CapabilityState::Supported);
	CHECK(f.hp.featureState(DEVICE_CAPABILITIES::OPTIMIZER) == CapabilityState::Supported);
	CHECK(f.hp.featureState(DEVICE_CAPABILITIES::MULTI_POINT) == CapabilityState::Unsupported);
	CHECK_EQ(f.hp.supportsFeature(DEVICE_CAPABILITIES::MULTI_POINT), false);
}

TEST(model_a_reply_listing_nothing_still_counts_as_an_answer)
{
	EngineFixture f;
	f.start();

	f.fake().enqueueData(replies::capabilities({}));
	CHECK(testing::waitFor([&] { return f.hp.capabilitiesKnown(); }));

	CHECK(f.hp.featureState(DEVICE_CAPABILITIES::SPEAK_TO_CHAT) == CapabilityState::Unsupported);
}

TEST(model_device_info_is_bounded_table_specific_and_session_scoped)
{
    EngineFixture f;
    auto apply = [&](const char* text, DATA_TYPE table = DATA_TYPE::DATA_MDR) {
        f.hp.setStateFromReply({table, 0, replies::hex(text)});
    };
    apply("05 01 0A 57 48 2D 31 30 30 30 58 4D 34");
    apply("05 02 05 32 2E 39 2E 30");
    CHECK_EQ(f.hp.reportedModel(), std::string("WH-1000XM4"));
    CHECK_EQ(f.hp.firmwareVersion(), std::string("2.9.0"));
    for (const char* bad : {"05", "05 02", "05 02 00", "05 02 02 31", "05 02 01 31 32", "05 02 01 00", "05 02 01 FF", "05 03 01 31"}) apply(bad);
    apply("05 02 01 31", DATA_TYPE::DATA_MDR_NO2);
    CHECK_EQ(f.hp.firmwareVersion(), std::string("2.9.0"));
    f.hp.resetState();
    CHECK(f.hp.reportedModel().empty());
    CHECK(f.hp.firmwareVersion().empty());
}

TEST(model_diagnostic_ring_is_bounded)
{
    ProtocolTrace::line("ring-first-marker");
    for (int i = 0; i < 256; ++i) ProtocolTrace::line("ring-entry-" + std::to_string(i));
    const auto log = ProtocolTrace::recentLog();
    CHECK(log.find("ring-first-marker") == std::string::npos);
    CHECK(log.find("ring-entry-255") != std::string::npos);
    CHECK_EQ(std::count(log.begin(), log.end(), '\n'), 256);
}
