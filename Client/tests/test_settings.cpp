// SON-5 and SON-10: concurrent settings access, stale ACK fulfillment, and what a setting's
// status actually claims.

#include "TestSupport.h"

#include <thread>
#include <vector>

namespace {

// The NC/ASM SET frame carries the ambient level in its last byte.
int ambientLevelOf(const CommandSerializer::Message& msg)
{
	return static_cast<unsigned char>(msg.messageBytes.at(7));
}

bool isNcAsmSet(const CommandSerializer::Message& msg)
{
	return !msg.messageBytes.empty() &&
		static_cast<unsigned char>(msg.messageBytes[0]) ==
		static_cast<unsigned char>(COMMAND_TYPE::NCASM_SET_PARAM);
}

std::vector<CommandSerializer::Message> ncAsmFrames(FakeBluetoothConnector& fake)
{
	std::vector<CommandSerializer::Message> out;
	for (const auto& msg : fake.sentCommands())
		if (isNcAsmSet(msg))
			out.push_back(msg);
	return out;
}

} // namespace

// The bug this replaces: setChanges() sent value A, then fulfilled the property by copying
// whatever "desired" happened to hold when the ACK came back. A change made during the send
// was marked as already applied and never reached the headphones.
TEST(settings_value_changed_during_send_is_not_marked_fulfilled)
{
	EngineFixture f;

	// The device script changes the setting again at the exact moment the first frame is on
	// the wire, before its ACK is produced.
	std::atomic<bool> bumped{ false };
	f.fake().setScript([&](FakeBluetoothConnector& fake, const CommandSerializer::Message& msg) {
		if (isNcAsmSet(msg) && !bumped.exchange(true))
			f.hp.setAsmLevel(17); // the user moves the slider while we are sending
		if (msg.dataType != DATA_TYPE::ACK)
			fake.enqueueAck(msg.seqNumber);
	});
	f.start();

	f.hp.setAmbientSoundControl(true);
	f.hp.setAsmLevel(5);
	f.hp.setChanges();

	CHECK_MSG(f.hp.isChanged(), "the value changed mid-send must still be pending");

	f.hp.setChanges();
	CHECK_EQ(f.hp.isChanged(), false);

	auto frames = ncAsmFrames(f.fake());
	CHECK_EQ(frames.size(), size_t(2));
	CHECK_EQ(ambientLevelOf(frames[0]), 5);
	CHECK_EQ(ambientLevelOf(frames[1]), 17);
	CHECK_EQ(f.hp.getAsmLevel(), 17);
}

TEST(settings_unchanged_value_is_not_resent)
{
	EngineFixture f;
	f.start();

	f.hp.setAmbientSoundControl(true);
	f.hp.setAsmLevel(5);
	f.hp.setChanges();
	CHECK_EQ(ncAsmFrames(f.fake()).size(), size_t(1));

	// Selecting the same value again, now that we know it was pushed, sends nothing.
	f.hp.setAsmLevel(5);
	CHECK_EQ(f.hp.isChanged(), false);
	f.hp.setChanges();
	CHECK_EQ(ncAsmFrames(f.fake()).size(), size_t(1));
}

// SON-10: the UI default is not evidence about the device. Selecting the value the app
// happens to show must still produce a command while the real value is unknown.
TEST(settings_selecting_the_default_while_unknown_still_sends)
{
	EngineFixture f;
	f.start();

	CHECK_EQ(f.hp.getStatuses().dsee, SettingStatus::Unknown);

	f.hp.setAudioUpsampling(false); // false is also the local default
	CHECK_MSG(f.hp.isChanged(), "an unknown setting must be pushed even at its default value");

	f.hp.setChanges();
	CHECK_EQ(f.hp.getStatuses().dsee, SettingStatus::Acknowledged);

	// Now that it has actually been pushed, re-selecting it is a no-op.
	f.hp.setAudioUpsampling(false);
	CHECK_EQ(f.hp.isChanged(), false);
}

TEST(settings_ack_means_acknowledged_not_confirmed)
{
	EngineFixture f;
	f.start();

	f.hp.setAmbientSoundControl(true);
	f.hp.setAsmLevel(8);
	f.hp.setChanges();

	// The frame was ACKed. That says the headphones received it, nothing more.
	CHECK_EQ(f.hp.getStatuses().ambient, SettingStatus::Acknowledged);

	// A read-back reply is what makes it Confirmed.
	f.fake().enqueueData(replies::ncAsm(true, 8));
	CHECK(testing::waitFor([&] { return f.hp.getStatuses().ambient == SettingStatus::Confirmed; }));
}

TEST(settings_write_only_features_never_reach_confirmed)
{
	EngineFixture f;
	f.start();

	f.hp.setVoiceGuidance(true);
	f.hp.setChanges();

	// There is no voice-guidance read-back in the protocol implementation, so the honest
	// answer stays "acknowledged".
	CHECK_EQ(f.hp.getStatuses().voiceGuidance, SettingStatus::Acknowledged);
}

TEST(settings_failed_send_is_reported_and_retryable)
{
	EngineFixture f;
	f.fake().setAutoAck(false);
	f.start(std::chrono::milliseconds(100));

	f.hp.setAudioUpsampling(true);
	CHECK_THROWS(f.hp.setChanges(), RecoverableException);
	CHECK_EQ(f.hp.getStatuses().dsee, SettingStatus::Failed);

	// A failed setting is not retried on its own...
	CHECK_EQ(f.hp.isChanged(), false);

	// ...and an unacknowledged command ends the session (SON-9), so the retry happens on a
	// fresh link rather than on one where ACKs can no longer be attributed.
	f.fake().setAutoAck(true);
	f.reconnect();

	// Asking for it again queues a fresh attempt.
	f.hp.setAudioUpsampling(true);
	CHECK_EQ(f.hp.isChanged(), true);
	f.hp.setChanges();
	CHECK_EQ(f.hp.getStatuses().dsee, SettingStatus::Acknowledged);
}

// SON-10: "Confirmed" means the device holds the value the app is showing. A reply that
// describes the value the device *still* holds says nothing about a request the user has
// made since, so it must not settle it.
TEST(settings_reply_about_the_old_value_does_not_confirm_a_newer_request)
{
	EngineFixture f;
	f.start();

	// Baseline: ambient on at level 8, confirmed by a read-back.
	f.hp.setAmbientSoundControl(true);
	f.hp.setAsmLevel(8);
	f.hp.setChanges();
	f.fake().enqueueData(replies::ncAsm(true, 8));
	CHECK(testing::waitFor([&] { return f.hp.getStatuses().ambient == SettingStatus::Confirmed; }));

	// The user moves the slider. Nothing has reached the wire yet.
	f.hp.setAsmLevel(16);
	CHECK_EQ(f.hp.getStatuses().ambient, SettingStatus::Requested);

	// The device reports its state - the level part is still the old 8, because the request
	// for 16 has not been sent. Focus-on-voice comes back on in the same frame (someone used
	// the headset), which is what tells the test the notification has actually been applied;
	// the level alone could not, since it is unchanged by definition.
	f.fake().enqueueData(replies::ncAsm(true, 8, /*focusOnVoice*/ true, /*notify*/ true));
	CHECK(testing::waitFor([&] { return f.hp.getFocusOnVoice(); }));

	CHECK_EQ(f.hp.getAsmLevel(), 8);
	CHECK_MSG(f.hp.getStatuses().ambient == SettingStatus::Requested,
		"a reply about the old value must not mark a newer pending request as confirmed");
	CHECK_MSG(f.hp.isChanged(), "the queued request must still be waiting to be sent");

	// And once it is sent and answered, it does settle.
	f.hp.setChanges();
	f.fake().enqueueData(replies::ncAsm(true, 16, true));
	CHECK(testing::waitFor([&] { return f.hp.getStatuses().ambient == SettingStatus::Confirmed; }));
	CHECK_EQ(f.hp.getAsmLevel(), 16);
}

// SON-10, the other ordering: the device may apply the change and report it back before the
// transport-level ACK for the frame that carried it. "The device says it holds this" outranks
// "the device received the frame", so the ACK must not walk the status back down.
TEST(settings_read_back_that_beats_its_own_ack_is_not_downgraded)
{
	EngineFixture f;

	f.fake().setScript([&](FakeBluetoothConnector& fake, const CommandSerializer::Message& msg) {
		if (msg.dataType == DATA_TYPE::ACK)
			return; // our own ACK for the notification below
		if (isNcAsmSet(msg))
		{
			// Report the new state first, and only acknowledge the frame once the reader has
			// applied it - the ordering under test, forced rather than raced for.
			fake.enqueueData(replies::ncAsm(true, 12, false, /*notify*/ true));
			CHECK(testing::waitFor([&] {
				return f.hp.getStatuses().ambient == SettingStatus::Confirmed;
			}));
		}
		fake.enqueueAck(msg.seqNumber);
	});
	f.start();

	f.hp.setAmbientSoundControl(true);
	f.hp.setAsmLevel(12);
	f.hp.setChanges();

	CHECK_MSG(f.hp.getStatuses().ambient == SettingStatus::Confirmed,
		"an ACK must not downgrade a value the device already reported back");
	CHECK_EQ(f.hp.getAsmLevel(), 12);
	CHECK_EQ(f.hp.isChanged(), false);
}

// The same rule for the failure path: a change the device confirmed is not undone by the
// acknowledgement going missing afterwards.
TEST(settings_confirmed_value_is_not_reported_as_failed_when_the_ack_never_arrives)
{
	EngineFixture f;

	f.fake().setScript([&](FakeBluetoothConnector& fake, const CommandSerializer::Message& msg) {
		if (msg.dataType == DATA_TYPE::ACK)
			return;
		if (isNcAsmSet(msg))
		{
			fake.enqueueData(replies::ncAsm(true, 9, false, /*notify*/ true));
			CHECK(testing::waitFor([&] {
				return f.hp.getStatuses().ambient == SettingStatus::Confirmed;
			}));
			return; // ...and then never acknowledge the frame
		}
		fake.enqueueAck(msg.seqNumber);
	});
	f.start(std::chrono::milliseconds(150));

	f.hp.setAmbientSoundControl(true);
	f.hp.setAsmLevel(9);
	CHECK_THROWS(f.hp.setChanges(), RecoverableException);

	CHECK_MSG(f.hp.getStatuses().ambient == SettingStatus::Confirmed,
		"a missing ACK must not overwrite a device read-back that already confirmed the value");
}

TEST(settings_device_notification_updates_state_without_a_pending_change)
{
	EngineFixture f;
	f.start();

	// The physical ANC button was pressed on the headphones.
	f.fake().enqueueData(replies::ncAsm(true, 19, true, /*notify*/ true));

	CHECK(testing::waitFor([&] { return f.hp.getAsmLevel() == 19; }));
	CHECK_EQ(f.hp.getAmbientSoundControl(), true);
	CHECK_EQ(f.hp.getFocusOnVoice(), true);
	// A device notification is not a change we need to push back.
	CHECK_EQ(f.hp.isChanged(), false);
}

TEST(settings_pending_user_change_survives_a_device_notification)
{
	EngineFixture f;
	f.start();

	f.hp.setAmbientSoundControl(true);
	f.hp.setAsmLevel(15);            // queued, not sent yet
	f.fake().enqueueData(replies::ncAsm(true, 3, false, true));

	CHECK(testing::waitFor([&] { return f.hp.getAsmLevel() == 3; }));
	CHECK_MSG(f.hp.isChanged(), "the queued user change must survive an incoming notification");

	f.hp.setChanges();
	auto frames = ncAsmFrames(f.fake());
	CHECK_EQ(frames.size(), size_t(1));
	CHECK_EQ(ambientLevelOf(frames[0]), 15);
}

TEST(settings_spatial_off_from_unknown_sends_a_command_instead_of_throwing)
{
	EngineFixture f;
	f.start();

	// Both values already read as "off" locally, and the device value is unknown. The old
	// code called this impossible and threw std::logic_error.
	f.hp.setSurroundPosition(SOUND_POSITION_PRESET::OFF);
	f.hp.setChanges();

	CHECK_EQ(f.hp.isChanged(), false);
	bool sawVpt = false;
	for (const auto& msg : f.fake().sentCommands())
		if (!msg.messageBytes.empty() &&
			static_cast<unsigned char>(msg.messageBytes[0]) ==
			static_cast<unsigned char>(COMMAND_TYPE::VPT_SET_PARAM))
			sawVpt = true;
	CHECK(sawVpt);
}

// Not a race detector, but it does exercise every guarded path from several threads at once:
// setters, getters, isChanged() and setChanges() all touching the same state.
TEST(settings_concurrent_readers_and_writers_stay_consistent)
{
	EngineFixture f;
	f.start();

	std::atomic<bool> stop{ false };
	std::vector<std::thread> threads;

	threads.emplace_back([&] {
		for (int i = 1; i <= 200 && !stop; ++i)
		{
			f.hp.setAsmLevel(1 + (i % 19));
			f.hp.setAmbientSoundControl(i % 2 == 0);
		}
	});
	threads.emplace_back([&] {
		for (int i = 0; i < 200 && !stop; ++i)
		{
			(void)f.hp.getAsmLevel();
			(void)f.hp.getAmbientSoundControl();
			(void)f.hp.isFocusOnVoiceAvailable();
			(void)f.hp.getStatuses();
			(void)f.hp.getEqualizer();
		}
	});
	threads.emplace_back([&] {
		for (int i = 0; i < 50 && !stop; ++i)
		{
			try { if (f.hp.isChanged()) f.hp.setChanges(); }
			catch (const std::exception&) {}
		}
	});
	threads.emplace_back([&] {
		for (int i = 0; i < 100 && !stop; ++i)
			f.fake().enqueueData(replies::ncAsm(true, 1 + (i % 19), false, true));
	});

	for (auto& t : threads)
		t.join();
	stop = true;

	// Whatever interleaving happened, the engine still answers and stays in a legal state.
	const int level = f.hp.getAsmLevel();
	CHECK(level >= 0 && level <= 19);
}
