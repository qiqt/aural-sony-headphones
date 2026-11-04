// SON-6: the fake transport itself, plus the framing path it shares with hardware.

#include "TestSupport.h"

#include <atomic>
#include <thread>

TEST(transport_command_round_trip_is_framed_and_acked)
{
	EngineFixture f;
	f.start();

	f.hp.requestBattery(); // blocks until the auto-ACK comes back through the Listener

	auto commands = f.fake().sentCommands();
	CHECK_EQ(commands.size(), size_t(1));
	CHECK_EQ(commands[0].dataType, DATA_TYPE::DATA_MDR);
	CHECK_EQ(static_cast<unsigned char>(commands[0].messageBytes.at(0)), BATTERY_LEVEL_GET);
}

TEST(transport_reply_split_across_recv_calls_is_reassembled)
{
	EngineFixture f;
	// One byte per recv: every frame arrives in pieces, like a real RFCOMM byte stream.
	f.fake().setChunkSize(1);
	f.start();

	f.fake().enqueueData(replies::battery(72, true));

	CHECK(testing::waitFor([&] { return f.hp.getBatteryLevel() == 72; }));
	CHECK_EQ(f.hp.isBatteryCharging(), true);
}

TEST(transport_several_replies_in_one_read_are_all_handled)
{
	EngineFixture f;
	f.start();

	// Two frames delivered as one contiguous block.
	Buffer both;
	auto first = CommandSerializer::packageDataForBt(replies::battery(41), DATA_TYPE::DATA_MDR, 0);
	auto second = CommandSerializer::packageDataForBt(replies::ncAsm(true, 12), DATA_TYPE::DATA_MDR, 1);
	both.insert(both.end(), first.begin(), first.end());
	both.insert(both.end(), second.begin(), second.end());
	f.fake().enqueueRaw(both);

	CHECK(testing::waitFor([&] { return f.hp.getBatteryLevel() == 41 && f.hp.getAsmLevel() == 12; }));
	CHECK_EQ(f.hp.getAmbientSoundControl(), true);
}

TEST(transport_data_frames_are_acknowledged_back_to_the_device)
{
	EngineFixture f;
	f.start();

	f.fake().enqueueData(replies::battery(50));

	// The Listener answers every DATA frame with an ACK frame.
	CHECK(testing::waitFor([&] {
		for (const auto& msg : f.fake().sentFrames())
			if (msg.dataType == DATA_TYPE::ACK)
				return true;
		return false;
	}));
}

TEST(transport_corrupt_frame_does_not_end_the_session)
{
	EngineFixture f;
	f.start();

	// A frame whose checksum is wrong: one lost frame, not a lost link.
	Buffer bad = CommandSerializer::packageDataForBt(replies::battery(99), DATA_TYPE::DATA_MDR, 0);
	bad[bad.size() - 2] = static_cast<char>(bad[bad.size() - 2] + 1);
	f.fake().enqueueRaw(bad);
	CHECK(f.fake().waitForInboxDrained());

	// Still connected, still listening: a good frame after it is processed normally.
	f.fake().enqueueData(replies::battery(33));
	CHECK(testing::waitFor([&] { return f.hp.getBatteryLevel() == 33; }));
	CHECK_EQ(f.disconnectCallbacks.load(), 0);
}

TEST(transport_remote_close_makes_the_connector_report_disconnected)
{
	EngineFixture f;
	f.start();

	f.fake().closeRemote();

	CHECK(testing::waitFor([&] { return !f.bt.isConnected(); }));
}

// Full duplex is the whole point of the reader thread: a command waiting for its ACK must not
// stop incoming DATA from being handled, and the ACK the reader owes the device must go out
// even though a sender is blocked in the middle of the same transport.
TEST(transport_data_arriving_while_a_sender_waits_for_its_ack_is_still_handled)
{
	EngineFixture f;
	f.fake().setAutoAck(false);
	f.start(std::chrono::milliseconds(5000)); // the test releases the sender, not the clock

	std::atomic<bool> done{ false };
	std::atomic<bool> threw{ false };
	std::thread sender([&] {
		try { f.hp.requestBattery(); }
		catch (const std::exception&) { threw = true; }
		done = true;
	});

	CHECK(f.fake().waitForSentCommands(1));
	const auto command = f.fake().sentCommands().back();
	CHECK_EQ(done.load(), false);

	// The physical ANC button is pressed while our command is still unanswered.
	f.fake().enqueueData(replies::ncAsm(true, 17, false, /*notify*/ true));

	CHECK_MSG(testing::waitFor([&] { return f.hp.getAsmLevel() == 17; }),
		"incoming DATA must be handled while a sender waits for its ACK");
	CHECK_MSG(testing::waitFor([&] {
		for (const auto& msg : f.fake().sentFrames())
			if (msg.dataType == DATA_TYPE::ACK)
				return true;
		return false;
	}), "the reader must be able to ACK that frame even with a sender mid-transport");

	// None of that satisfied the sender: it is still waiting for its own ACK.
	CHECK_EQ(done.load(), false);

	f.fake().enqueueAck(command.seqNumber);
	CHECK(testing::waitFor([&] { return done.load(); }));
	sender.join();
	CHECK_EQ(threw.load(), false);
}

TEST(transport_service_discovery_overrides_name_fallback_and_prefers_v2)
{
    EngineFixture f;
    f.fake().setSonyServices(true, true);
    f.bt.connect("00:11:22:33:44:55", SonyProtocol::V1);
    CHECK(f.bt.getProtocol() == SonyProtocol::V2);
    CHECK(f.fake().lastProtocol() == SonyProtocol::V2);
    CHECK(f.bt.protocolDiscovered());
    f.bt.disconnect();
    f.fake().setSonyServices(true, false);
    f.bt.connect("00:11:22:33:44:55", SonyProtocol::V2);
    CHECK(f.bt.getProtocol() == SonyProtocol::V1);
    CHECK(f.fake().lastProtocol() == SonyProtocol::V1);
    CHECK(f.bt.protocolDiscovered());
}

TEST(transport_inconclusive_discovery_uses_explicit_fallback_and_clears_source)
{
    EngineFixture f;
    f.fake().setSonyServices(false, true);
    f.bt.connect("00:11:22:33:44:55", SonyProtocol::V1);
    CHECK(f.bt.protocolDiscovered());
    f.bt.disconnect();
    f.fake().setSonyServices(false, false);
    f.bt.connect("AA:BB:CC:DD:EE:FF", SonyProtocol::V1);
    CHECK(f.bt.getProtocol() == SonyProtocol::V1);
    CHECK(!f.bt.protocolDiscovered());
    f.bt.disconnect();
    f.bt.connect("AA:BB:CC:DD:EE:FF", SonyProtocol::V2);
    CHECK(f.bt.getProtocol() == SonyProtocol::V2);
    CHECK(!f.bt.protocolDiscovered());
    CHECK(f.fake().sentCommands().empty()); // discovery itself sends no protocol commands
}

TEST(transport_cancel_during_service_discovery_does_not_open_a_socket)
{
    EngineFixture f;
    f.fake().setSonyServices(false, true);
    f.fake().blockServiceDiscovery();
    std::atomic<bool> cancelled{false};
    std::thread attempt([&] {
        try { f.bt.connect("00:11:22:33:44:55", SonyProtocol::V1); }
        catch (const RecoverableException&) { cancelled = true; }
    });
    const bool started = testing::waitFor([&] { return f.fake().serviceLookups() > 0; });
    f.bt.disconnect();
    attempt.join();
    CHECK(started);
    CHECK(cancelled.load());
    CHECK_EQ(f.fake().connectAttempts(), 0);
    CHECK(!f.bt.isConnected());
    CHECK(!f.bt.protocolDiscovered());
}
