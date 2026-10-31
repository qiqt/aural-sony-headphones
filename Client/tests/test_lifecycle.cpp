// SON-7 and SON-8: session ownership, remote closure, and reconnect.

#include "TestSupport.h"

#include <thread>

// Seen on a WH-1000XM4: the very first INIT on a new link went unacknowledged. The handshake used
// to swallow that and carry on, so the app showed "connected" over a link that refused every
// later command - and failed only when the user changed noise cancelling.
TEST(lifecycle_unacknowledged_handshake_ends_the_session)
{
	EngineFixture f;
	f.fake().setScript([](FakeBluetoothConnector& fake, const CommandSerializer::Message& msg) {
		if (msg.dataType == DATA_TYPE::ACK)
			return;
		if (toHex(msg.messageBytes) == "00 00")
			return;   // never acknowledged
		fake.enqueueAck(msg.seqNumber);
	});
	f.start(std::chrono::milliseconds(200));

	CHECK_THROWS(f.hp.queryState(), RecoverableException);
	CHECK_MSG(f.fake().sentCommands().size() == 1, "nothing more is tried on a link that cannot be used");
}

// A request the device ignores still gets its ACK: the handshake carries on past it.
TEST(lifecycle_handshake_continues_past_requests_the_device_does_not_answer)
{
	EngineFixture f;
	f.start(std::chrono::milliseconds(200));   // the fake ACKs everything and answers nothing

	f.hp.queryState();
	CHECK(wasSent(f.fake(), "00 00"));
	CHECK(wasSent(f.fake(), "06 01"));
	CHECK(wasSent(f.fake(), "66 02"));
}

TEST(lifecycle_remote_close_ends_the_session_once)
{
	EngineFixture f;
	f.start();

	f.fake().closeRemote();

	CHECK_MSG(testing::waitFor([&] { return f.disconnectCallbacks.load() == 1; }),
		"remote closure must report the lost link exactly once");
	CHECK_EQ(f.bt.isConnected(), false);
	CHECK_EQ(f.listener->linkLost(), true);
}

// The old reader kept looping while isConnected() still said true, so a closed channel meant
// recv() returning 0 forever at full speed.
TEST(lifecycle_remote_close_does_not_spin_the_reader)
{
	EngineFixture f;
	f.start();

	f.fake().closeRemote();
	CHECK(testing::waitFor([&] { return f.disconnectCallbacks.load() == 1; }));

	const int afterClose = f.fake().recvCount();
	// Give a spinning reader every chance to prove it is spinning.
	CHECK(!testing::waitFor([&] { return f.fake().recvCount() > afterClose + 2; },
		std::chrono::milliseconds(150)));
}

TEST(lifecycle_send_failure_ends_the_session)
{
	EngineFixture f;
	f.start();

	f.fake().failSends(true);
	CHECK_THROWS(f.hp.requestBattery(), RecoverableException);

	CHECK(testing::waitFor([&] { return f.disconnectCallbacks.load() == 1; }));
	CHECK_EQ(f.bt.isConnected(), false);
}

TEST(lifecycle_local_disconnect_does_not_report_a_lost_link)
{
	EngineFixture f;
	f.start();

	f.stopSession(); // requestStop -> close socket -> join

	// We closed it on purpose: the owner already knows, so no disconnect callback.
	CHECK_EQ(f.disconnectCallbacks.load(), 0);
	CHECK_EQ(f.bt.isConnected(), false);
}

TEST(lifecycle_reset_clears_every_setting_between_sessions)
{
	EngineFixture f;
	f.start();

	f.fake().enqueueData(replies::ncAsm(true, 14));
	f.fake().enqueueData(replies::battery(88));
	f.fake().enqueueData(replies::capabilities({ FUNCTION_TYPE::NC_ASM }));
	CHECK(testing::waitFor([&] {
		return f.hp.getAsmLevel() == 14 && f.hp.getBatteryLevel() == 88 && f.hp.capabilitiesKnown();
	}));

	f.hp.resetState();

	CHECK_EQ(f.hp.getBatteryLevel(), -1);
	CHECK_EQ(f.hp.getAsmLevel(), 0);
	CHECK_EQ(f.hp.getAmbientSoundControl(), false);
	CHECK_EQ(f.hp.capabilitiesKnown(), false);
	CHECK_EQ(f.hp.getStatuses().ambient, SettingStatus::Unknown);
	CHECK_EQ(f.hp.isChanged(), false);
	CHECK_EQ(f.hp.getDevices().size(), size_t(1));
}

TEST(lifecycle_reconnect_starts_a_new_session_id)
{
	EngineFixture f;
	f.start();
	const auto first = f.bt.sessionId();

	f.hp.resetState();
	f.reconnect("00:11:22:33:44:55");

	CHECK(f.bt.sessionId() != first);
	CHECK_EQ(f.bt.isConnected(), true);
	CHECK_EQ(f.fake().connectCount(), 2);

	// The new session works normally.
	f.fake().enqueueData(replies::battery(21));
	CHECK(testing::waitFor([&] { return f.hp.getBatteryLevel() == 21; }));
}

TEST(lifecycle_state_from_a_previous_device_does_not_leak_into_the_next)
{
	EngineFixture f;
	f.start();

	f.fake().enqueueData(replies::ncAsm(true, 11));
	CHECK(testing::waitFor([&] { return f.hp.getAsmLevel() == 11; }));

	// Session ends and a different headset connects.
	f.hp.resetState();
	f.reconnect("AA:BB:CC:DD:EE:FF");

	CHECK_MSG(f.hp.getAsmLevel() == 0, "the previous device's ambient level must not carry over");
	CHECK_EQ(f.hp.getStatuses().ambient, SettingStatus::Unknown);
}

TEST(lifecycle_listener_stops_promptly_when_the_socket_closes)
{
	EngineFixture f;
	f.start();

	// Make sure the reader really is parked inside recv before closing under it.
	CHECK(testing::waitFor([&] { return f.fake().recvCount() >= 1; }));

	// stop() joins the reader thread; it can only return if closing the socket released the
	// recv it was blocked in. A hang here is the failure.
	f.bt.disconnect();
	f.listener->stop();
	CHECK_EQ(f.listener->linkLost(), true);
}
