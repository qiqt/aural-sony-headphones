// SON-9: ACK correlation and timeout recovery - delayed, missing, extra and previous-session
// replies.
//
// The hard case is a *late* ACK. The frame carries only a 1-bit seq that toggles on each
// successful exchange, so after a timeout the sequence has not advanced and the next command
// goes out with the same seq: nothing on the wire tells command 1's ACK apart from command 2's.
// The design answer (see BluetoothWrapper.h) is that a timeout makes the link ambiguous and
// ends it. These tests pin that down from both directions - a stale ACK can never complete a
// later command, and a reconnect is what makes the link usable again.

#include "TestSupport.h"

#include <atomic>
#include <thread>

TEST(ack_delayed_reply_still_completes_the_command)
{
	EngineFixture f;
	f.fake().setAutoAck(false);
	f.start(std::chrono::milliseconds(5000)); // generous: the test, not the clock, releases it

	std::atomic<bool> done{ false };
	std::atomic<bool> threw{ false };
	std::thread sender([&] {
		try { f.hp.requestBattery(); }
		catch (const std::exception&) { threw = true; }
		done = true;
	});

	// The command is on the wire and the sender is waiting. Answer it only now.
	CHECK(f.fake().waitForSentCommands(1));
	CHECK_EQ(done.load(), false);

	auto commands = f.fake().sentCommands();
	f.fake().enqueueAck(commands.back().seqNumber);

	CHECK(testing::waitFor([&] { return done.load(); }));
	sender.join();
	CHECK_EQ(threw.load(), false);
	// A command that completed normally leaves the link usable.
	CHECK_EQ(f.bt.isDesynchronized(), false);
}

TEST(ack_missing_reply_times_out_and_ends_the_session)
{
	EngineFixture f;
	f.fake().setAutoAck(false);
	f.start(std::chrono::milliseconds(100));

	bool timedOut = false;
	bool disconnectRequested = false;
	try
	{
		f.hp.requestBattery();
	}
	catch (const RecoverableException& exc)
	{
		timedOut = true;
		disconnectRequested = exc.shouldDisconnect;
	}

	CHECK_MSG(timedOut, "a command with no ACK must not block forever");
	// The socket is still open, but the *protocol* is no longer usable: we cannot tell a late
	// ACK for this command from the next command's, so the caller is asked to reconnect.
	CHECK_EQ(f.bt.isConnected(), true);
	CHECK_MSG(disconnectRequested, "an ambiguous timeout must ask the caller to tear the session down");
	CHECK_EQ(f.bt.isDesynchronized(), true);
}

TEST(ack_no_further_command_reaches_the_wire_after_a_timeout)
{
	EngineFixture f;
	f.fake().setAutoAck(false);
	f.start(std::chrono::milliseconds(100));

	CHECK_THROWS(f.hp.requestBattery(), RecoverableException);
	const size_t afterTimeout = f.fake().sentCommands().size();
	CHECK_EQ(afterTimeout, size_t(1));

	// Even with the device answering again, the link stays closed for business: sending would
	// mean putting a frame on a link where an incoming ACK is unattributable.
	f.fake().setAutoAck(true);
	bool disconnectRequested = false;
	try { f.hp.requestBattery(); }
	catch (const RecoverableException& exc) { disconnectRequested = exc.shouldDisconnect; }

	CHECK_MSG(disconnectRequested, "commands on a desynchronized link must fail with shouldDisconnect");
	CHECK_MSG(f.fake().sentCommands().size() == afterTimeout,
		"nothing may reach the wire once correlation is impossible");
}

// The exact ordering a "was anything pending?" check cannot survive: the stale ACK arrives
// *while a later command is being started*, not in the quiet gap before it.
TEST(ack_stale_reply_cannot_release_a_command_started_after_it)
{
	EngineFixture f;
	f.fake().setAutoAck(false);
	f.start(std::chrono::milliseconds(120));

	// Command 1 goes unanswered and times out.
	CHECK_THROWS(f.hp.requestBattery(), RecoverableException);
	const auto stale = f.fake().sentCommands().back();
	const size_t sentAfterTimeout = f.fake().sentCommands().size();
	const auto droppedBefore = f.bt.droppedAckCount();

	// Command 2 is attempted at the same moment command 1's ACK finally turns up.
	std::atomic<bool> secondCompleted{ false };
	std::atomic<bool> done{ false };
	std::thread sender([&] {
		try { f.hp.requestBattery(); secondCompleted = true; }
		catch (const std::exception&) {}
		done = true;
	});
	f.fake().enqueueAck(stale.seqNumber);

	CHECK(testing::waitFor([&] { return done.load(); }));
	sender.join();

	CHECK_MSG(!secondCompleted.load(), "a stale ACK must never complete a later command");
	CHECK_MSG(f.fake().sentCommands().size() == sentAfterTimeout,
		"the later command must not have been put on the wire at all");
	CHECK(testing::waitFor([&] { return f.bt.droppedAckCount() > droppedBefore; }));
}

// The same guarantee stated at the level the Listener works at: once the link is ambiguous,
// no ACK frame is correlated to anything, whatever its seq.
TEST(ack_frames_are_inert_once_the_link_is_desynchronized)
{
	EngineFixture f;
	f.fake().setAutoAck(false);
	f.start(std::chrono::milliseconds(100));

	CHECK_THROWS(f.hp.requestBattery(), RecoverableException);
	const auto droppedBefore = f.bt.droppedAckCount();

	f.bt.postAck(0);
	f.bt.postAck(1);

	CHECK_EQ(f.bt.droppedAckCount(), droppedBefore + 2);
	CHECK_EQ(f.bt.isDesynchronized(), true);
}

TEST(ack_reconnecting_makes_the_link_usable_again)
{
	EngineFixture f;
	f.fake().setAutoAck(false);
	f.start(std::chrono::milliseconds(100));

	CHECK_THROWS(f.hp.requestBattery(), RecoverableException);
	CHECK_EQ(f.bt.isDesynchronized(), true);

	// A new link is synchronized by definition - it is the only thing that clears the latch.
	f.fake().setAutoAck(true);
	f.reconnect();
	CHECK_EQ(f.bt.isDesynchronized(), false);

	f.fake().clearSent();
	f.hp.requestBattery(); // must not throw
	CHECK_EQ(f.fake().sentCommands().size(), size_t(1));
}

TEST(ack_extra_reply_is_dropped_not_banked)
{
	EngineFixture f;
	f.start(std::chrono::milliseconds(100)); // auto-ACK on

	f.hp.requestBattery(); // completes normally
	auto first = f.fake().sentCommands().back();

	// The device sends a second ACK for the same command.
	f.fake().enqueueAck(first.seqNumber);
	CHECK(testing::waitFor([&] { return f.bt.droppedAckCount() >= 1; }));

	// The extra ACK was not banked: the link is still healthy and the next command completes
	// on its own ACK rather than on the leftover one.
	CHECK_EQ(f.bt.isDesynchronized(), false);
	f.hp.requestBattery();
	CHECK_EQ(f.fake().sentCommands().size(), size_t(2));
}

TEST(ack_from_a_previous_session_is_not_applied_to_the_new_one)
{
	EngineFixture f;
	f.fake().setAutoAck(false);
	f.start(std::chrono::milliseconds(100));

	CHECK_THROWS(f.hp.requestBattery(), RecoverableException);
	auto stale = f.fake().sentCommands().back();
	const auto sessionA = f.bt.sessionId();

	// Reconnect: new session.
	f.reconnect();
	CHECK(f.bt.sessionId() != sessionA);
	f.fake().clearSent();

	// The old session's ACK arrives on the new link before anything is sent on it.
	f.fake().enqueueAck(stale.seqNumber);
	CHECK(testing::waitFor([&] { return f.bt.droppedAckCount() >= 1; }));

	// A command in the new session still has to wait for its own reply.
	CHECK_THROWS(f.hp.requestBattery(), RecoverableException);
	CHECK_EQ(f.fake().sentCommands().size(), size_t(1));
}

TEST(ack_duplicate_of_completed_command_cannot_complete_the_next_command)
{
	EngineFixture f;
	f.start(std::chrono::milliseconds(5000));
	f.hp.requestBattery();
	const auto first = f.fake().sentCommands().back();
	f.fake().setAutoAck(false);

	std::atomic<bool> done{ false };
	std::atomic<bool> failed{ false };
	std::thread sender([&] {
		try { f.hp.requestBattery(); }
		catch (...) { failed = true; }
		done = true;
	});
	const bool sent = f.fake().waitForSentCommands(2);
	const auto dropped = f.bt.droppedAckCount();
	f.fake().enqueueAck(first.seqNumber);
	const bool observed = testing::waitFor([&] {
		return done.load() || f.bt.droppedAckCount() > dropped;
	});
	const bool completedByDuplicate = done.load();
	if (sent)
		f.fake().enqueueAck(f.fake().sentCommands().back().seqNumber);
	else
		f.bt.disconnect();
	sender.join();

	CHECK(sent);
	CHECK(observed);
	CHECK_MSG(!completedByDuplicate, "an earlier command's duplicate ACK must not release this command");
	CHECK(!failed.load());
	CHECK_EQ(f.bt.droppedAckCount(), dropped + 1);
}

TEST(ack_waiting_sender_is_released_when_the_link_drops)
{
	EngineFixture f;
	f.fake().setAutoAck(false);
	f.start(std::chrono::milliseconds(30000)); // long enough that only the link loss can end it

	std::atomic<bool> done{ false };
	std::atomic<bool> sawDisconnectFlag{ false };
	std::thread sender([&] {
		try { f.hp.requestBattery(); }
		catch (const RecoverableException& exc) { sawDisconnectFlag = exc.shouldDisconnect; }
		catch (const std::exception&) {}
		done = true;
	});

	CHECK(f.fake().waitForSentCommands(1));
	f.fake().closeRemote();

	CHECK_MSG(testing::waitFor([&] { return done.load(); }),
		"a sender waiting for an ACK must be released when the link dies");
	sender.join();
	CHECK_EQ(sawDisconnectFlag.load(), true);
	// A dead link is not an ambiguous one: the session is over either way, but nothing was
	// left unattributable, so the latch stays clear.
	CHECK_EQ(f.bt.isDesynchronized(), false);
}
