// Multipoint mode and its confirmation flow. The write, the alert and the answer are Sound Connect
// 13.3.0 static evidence (the Python reference vectors); no headset has been asked yet. These tests
// prove the engine only answers the alert it was sent, with the user's choice, and believes only
// what the device reports - not that an XM4 raises the alert or drops the link the way scripted
// here.

#include "TestSupport.h"

using replies::hex;

namespace {

ToggleSnapshot mode(EngineFixture& f) { return f.hp.getMultipointMode(); }
MultipointChange change(EngineFixture& f) { return f.hp.getMultipointChange(); }

std::string hexByte(unsigned char b) { return toHex(Buffer{ static_cast<char>(b) }); }

std::string sentLog(EngineFixture& f)
{
	std::string out;
	for (const auto& payload : sentPayloads(f.fake()))
		out += (out.empty() ? "" : " | ") + payload;
	return out;
}

bool anyAnswerSent(EngineFixture& f)
{
	for (const auto& payload : sentPayloads(f.fake()))
		if (payload.rfind("98", 0) == 0)
			return true;
	return false;
}

// The scripted headset lives in TestSupport.h; the controller tests drive the same one.
using MultipointDevice = MultipointHeadset;

// Advertise the multipoint slot and run discovery until its value is known.
void discoverMultipoint(EngineFixture& f, unsigned char slot = 0xD3)
{
	CHECK(deliver(f, hex("07 01 01 " + hexByte(slot))));
	f.hp.sendPendingQueries();
	CHECK(testing::waitFor([&] { return mode(f).support == CapabilityState::Supported; }));
	f.hp.sendPendingQueries();
	CHECK_MSG(testing::waitFor([&] { return mode(f).availability == Availability::Available && mode(f).valueKnown; }),
		"discovery should end with multipoint mode available and its value read");
	CHECK(settle(f));
}

// A session with a scripted headset whose multipoint value is known.
struct Session
{
	EngineFixture f;
	MultipointDevice device;

	explicit Session(MultipointDevice::OnWrite onWrite = MultipointDevice::OnWrite::Ask, int value = 0,
		bool installAlertHandling = true)
	{
		device.s->onWrite = onWrite;
		device.s->value = value;
		device.installOn(f.fake());
		f.start();
		if (installAlertHandling)
			f.hp.installAlertHandling();
		discoverMultipoint(f);
		f.fake().clearSent();
	}

	// Write the change and wait for the device to ask about it.
	void requestAndWaitForAlert(bool on)
	{
		CHECK(f.hp.setMultipointMode(on));
		f.hp.setChanges();
		CHECK_MSG(testing::waitFor([&] { return change(f).step == MultipointStep::Confirming; }),
			"the device's alert should put the change in front of the user");
	}
};

} // namespace

// ---- before the write --------------------------------------------------------------------------

TEST(multipoint_changes_need_alert_handling_and_a_known_available_value)
{
	{
		// Nobody to show a confirmation to: refused even though the control is ready.
		Session s(MultipointDevice::OnWrite::Ask, 0, false);
		CHECK(!s.f.hp.setMultipointMode(true));
		s.f.hp.setChanges();
		CHECK_EQ(sentLog(s.f), std::string());
	}

	EngineFixture f;
	f.start();
	f.hp.installAlertHandling();
	CHECK(!f.hp.setMultipointMode(true));   // not described

	CHECK(deliver(f, hex("07 01 01 D1")));
	CHECK(deliver(f, replies::generalSettingCapability(0xD1, v1::GS_TITLE_MULTIPOINT)));
	CHECK(deliver(f, hex("D3 D1 00")));
	CHECK(!f.hp.setMultipointMode(true));   // value unread

	CHECK(deliver(f, hex("D7 D1 01 00")));
	CHECK(deliver(f, hex("D5 D1 01")));
	CHECK(!f.hp.setMultipointMode(true));   // unavailable

	CHECK(deliver(f, hex("D5 D1 00")));
	CHECK(f.hp.setMultipointMode(true));
	CHECK_EQ(change(f).step, MultipointStep::Requested);
	CHECK_EQ(mode(f).requested, true);
	CHECK_EQ(mode(f).value, false);
	CHECK_EQ(f.hp.getStatuses().multipointMode, SettingStatus::Requested);
	// The paired-device status is a different thing and is untouched.
	CHECK_EQ(f.hp.getStatuses().multipoint, SettingStatus::Unknown);
}

TEST(multipoint_a_change_withdrawn_before_it_goes_out_sends_nothing)
{
	Session s;
	CHECK(s.f.hp.setMultipointMode(true));
	CHECK(s.f.hp.setMultipointMode(false));   // back to the reported value
	CHECK_EQ(change(s.f).step, MultipointStep::None);
	CHECK_EQ(s.f.hp.isChanged(), false);
	s.f.hp.setChanges();
	CHECK_EQ(sentLog(s.f), std::string());
}

TEST(multipoint_a_control_made_unavailable_before_the_send_drops_the_change)
{
	Session s;
	CHECK(s.f.hp.setMultipointMode(true));
	CHECK(deliver(s.f, hex("D5 D3 01")));
	s.f.hp.setChanges();
	CHECK_MSG(sentLog(s.f).empty(), "neither the alert enable nor the write may go to an unavailable control");
	CHECK_EQ(change(s.f).step, MultipointStep::None);
	CHECK_EQ(s.f.hp.isChanged(), false);
}

// ---- the write ----------------------------------------------------------------------------------

TEST(multipoint_alerts_are_enabled_once_before_the_first_write)
{
	Session s(MultipointDevice::OnWrite::Apply);

	CHECK(s.f.hp.setMultipointMode(true));
	s.f.hp.setChanges();
	CHECK_EQ(sentLog(s.f), std::string("94 01 00 | D8 D3 01 01"));
	CHECK(testing::waitFor([&] { return change(s.f).step == MultipointStep::None; }));
	CHECK_EQ(mode(s.f).value, true);
	CHECK_EQ(s.f.hp.getStatuses().multipointMode, SettingStatus::Confirmed);

	s.f.fake().clearSent();
	CHECK(s.f.hp.setMultipointMode(false));
	s.f.hp.setChanges();
	CHECK_EQ(sentLog(s.f), std::string("D8 D3 01 00"));
}

TEST(multipoint_a_report_of_the_old_value_does_not_end_the_change)
{
	Session s(MultipointDevice::OnWrite::Ignore);   // ACKs the write, says nothing

	CHECK(s.f.hp.setMultipointMode(true));
	s.f.hp.setChanges();
	CHECK_EQ(change(s.f).step, MultipointStep::Sent);
	CHECK_EQ(s.f.hp.getStatuses().multipointMode, SettingStatus::Acknowledged);

	// The read that follows the write reports the old value.
	CHECK(s.f.hp.hasPendingQueries());
	s.f.hp.sendPendingQueries();
	CHECK(settle(s.f));
	CHECK_EQ(change(s.f).step, MultipointStep::Sent);
	CHECK_EQ(mode(s.f).value, false);
	CHECK_EQ(mode(s.f).requested, true);

	// Another change cannot be stacked on top of it.
	CHECK(!s.f.hp.setMultipointMode(false));
}

TEST(multipoint_abandoning_an_unanswered_change_rereads_without_resending)
{
	Session s(MultipointDevice::OnWrite::Ignore);
	CHECK(s.f.hp.setMultipointMode(true));
	s.f.hp.setChanges();
	s.f.hp.sendPendingQueries();
	CHECK(settle(s.f));

	s.f.hp.abandonMultipointChange();
	CHECK_EQ(change(s.f).step, MultipointStep::None);
	CHECK_EQ(mode(s.f).value, false);
	CHECK_EQ(s.f.hp.getStatuses().multipointMode, SettingStatus::Confirmed);
	CHECK(s.f.hp.hasPendingQueries());
	s.f.hp.setChanges();
	s.f.hp.sendPendingQueries();
	CHECK_EQ(countSent(s.f.fake(), "D6 D3"), size_t(2));
	CHECK_EQ(countSent(s.f.fake(), "D8 D3 01 01"), size_t(1));
}

// ---- the confirmation ---------------------------------------------------------------------------

TEST(multipoint_confirmation_is_answered_with_the_received_id_and_the_users_choice)
{
	Session s;
	s.requestAndWaitForAlert(true);

	const auto alert = change(s.f).alert;
	CHECK(alert.has_value());
	CHECK_EQ(int(alert->messageId), int(v1::ALERT_MSG_MULTIPOINT_TO_ON));
	CHECK_EQ(int(alert->actionType), int(v1::ALERT_ACTION_POSITIVE_NEGATIVE));
	// Nothing answers it but the user.
	CHECK_EQ(s.f.hp.isChanged(), false);
	CHECK(!anyAnswerSent(s.f));

	// The read after the write reports the old value; the change still waits for the user.
	s.f.hp.sendPendingQueries();
	CHECK(settle(s.f));
	CHECK_EQ(change(s.f).step, MultipointStep::Confirming);

	CHECK_MSG(!s.f.hp.answerMultipointAlert(v1::ALERT_MSG_MULTIPOINT_TO_OFF, true),
		"an answer must quote the message ID the device sent");
	CHECK(s.f.hp.answerMultipointAlert(v1::ALERT_MSG_MULTIPOINT_TO_ON, true));
	CHECK_EQ(change(s.f).step, MultipointStep::Answering);
	CHECK(!change(s.f).alert.has_value());
	CHECK(s.f.hp.isChanged());

	s.f.hp.setChanges();
	CHECK(wasSent(s.f.fake(), "98 01 06 01"));
	CHECK_MSG(testing::waitFor([&] { return change(s.f).step == MultipointStep::None && mode(s.f).value; }),
		"the device's report of the new value settles the change");
	CHECK_EQ(s.f.hp.getStatuses().multipointMode, SettingStatus::Confirmed);
	CHECK_EQ(countSent(s.f.fake(), "D8 D3 01 01"), size_t(1));
}

TEST(multipoint_cancelling_sends_a_negative_answer_and_keeps_the_device_value)
{
	Session s;
	s.requestAndWaitForAlert(true);

	CHECK(s.f.hp.answerMultipointAlert(v1::ALERT_MSG_MULTIPOINT_TO_ON, false));
	s.f.hp.setChanges();
	CHECK(wasSent(s.f.fake(), "98 01 06 00"));
	CHECK_EQ(change(s.f).step, MultipointStep::None);
	CHECK_EQ(mode(s.f).value, false);
	CHECK_EQ(s.device.value(), 0);
	// And the value is read again rather than assumed.
	CHECK(s.f.hp.hasPendingQueries());
}

TEST(multipoint_turning_off_is_confirmed_with_message_07)
{
	Session s(MultipointDevice::OnWrite::Ask, 1);
	s.device.s->actionType = v1::ALERT_ACTION_CONFIRMATION_ONLY;
	s.requestAndWaitForAlert(false);

	CHECK_EQ(int(change(s.f).alert->messageId), int(v1::ALERT_MSG_MULTIPOINT_TO_OFF));
	CHECK_EQ(int(change(s.f).alert->actionType), int(v1::ALERT_ACTION_CONFIRMATION_ONLY));
	CHECK(s.f.hp.answerMultipointAlert(v1::ALERT_MSG_MULTIPOINT_TO_OFF, true));
	s.f.hp.setChanges();
	CHECK(wasSent(s.f.fake(), "98 01 07 01"));
	CHECK(testing::waitFor([&] { return change(s.f).step == MultipointStep::None && !mode(s.f).value; }));
}

TEST(multipoint_alert_arriving_before_the_write_ack_still_finds_its_change)
{
	Session s;
	s.device.s->alertBeforeAck = true;
	s.requestAndWaitForAlert(true);
	CHECK_EQ(int(change(s.f).alert->messageId), int(v1::ALERT_MSG_MULTIPOINT_TO_ON));
	CHECK_EQ(s.f.hp.getStatuses().multipointMode, SettingStatus::Acknowledged);
}

TEST(multipoint_unrelated_alerts_are_never_answered)
{
	Session s(MultipointDevice::OnWrite::Ignore);

	// No change out: an alert is not ours.
	CHECK(deliver(s.f, hex("99 01 06 01")));
	CHECK_EQ(change(s.f).step, MultipointStep::None);
	CHECK(!change(s.f).alert.has_value());

	// A change asked for but not yet written: still not ours.
	CHECK(s.f.hp.setMultipointMode(true));
	CHECK(deliver(s.f, hex("99 01 06 01")));
	CHECK_EQ(change(s.f).step, MultipointStep::Requested);

	s.f.hp.setChanges();
	CHECK_EQ(change(s.f).step, MultipointStep::Sent);
	// The opposite direction, an unknown message, a malformed frame, the other table.
	CHECK(deliver(s.f, hex("99 01 07 01")));
	CHECK(deliver(s.f, hex("99 01 08 01")));
	CHECK(deliver(s.f, hex("99 01 06 02")));
	CHECK(deliver(s.f, hex("99 02 06 01")));
	CHECK(deliver(s.f, hex("99 01 06")));
	CHECK(deliver(s.f, hex("99 01 06 01"), DATA_TYPE::DATA_MDR_NO2));
	CHECK_EQ(change(s.f).step, MultipointStep::Sent);
	CHECK_EQ(s.f.hp.isChanged(), false);

	// An answer cannot be invented for an alert that never came.
	CHECK(!s.f.hp.answerMultipointAlert(v1::ALERT_MSG_MULTIPOINT_TO_ON, true));
	s.f.hp.setChanges();
	CHECK(!anyAnswerSent(s.f));

	// The matching alert is recognised.
	CHECK(deliver(s.f, hex("99 01 06 01")));
	CHECK_EQ(change(s.f).step, MultipointStep::Confirming);
}

// ---- the link drops -----------------------------------------------------------------------------

TEST(multipoint_link_dropped_to_apply_the_change_ends_the_session_and_nothing_is_replayed)
{
	Session s;
	s.device.s->dropOnAccept = true;
	s.requestAndWaitForAlert(true);
	CHECK(s.f.hp.answerMultipointAlert(v1::ALERT_MSG_MULTIPOINT_TO_ON, true));
	s.f.hp.setChanges();
	CHECK(wasSent(s.f.fake(), "98 01 06 01"));

	// Accepted, and the device dropped the link to apply it: unconfirmed, not failed.
	CHECK(testing::waitFor([&] { return s.f.disconnectCallbacks.load() > 0; }));
	CHECK_EQ(change(s.f).step, MultipointStep::Accepted);
	CHECK_EQ(mode(s.f).value, false);

	// The host ends the session; the next starts from nothing.
	s.f.hp.resetState();
	CHECK_EQ(change(s.f).step, MultipointStep::None);
	s.f.reconnect();
	s.f.fake().clearSent();
	s.f.hp.setChanges();
	s.f.hp.sendPendingQueries();
	CHECK_MSG(sentLog(s.f).empty(), "nothing from the previous session may reach the new link");

	// Rediscovery reads what the device now holds. Nothing is rewritten or re-answered.
	discoverMultipoint(s.f);
	CHECK_EQ(mode(s.f).value, true);
	CHECK_EQ(countSent(s.f.fake(), "D8 D3 01 01"), size_t(0));
	CHECK(!anyAnswerSent(s.f));
	// Alerts are re-enabled only before this session's own first write.
	CHECK_EQ(countSent(s.f.fake(), "94 01 00"), size_t(0));
}

TEST(multipoint_a_write_lost_with_the_link_is_left_for_the_host_to_verify)
{
	Session s;
	s.f.fake().setScript([](FakeBluetoothConnector& fake, const CommandSerializer::Message& msg) {
		if (msg.dataType == DATA_TYPE::ACK)
			return;
		if (toHex(msg.messageBytes) == "D8 D3 01 01")
		{
			fake.closeRemote();   // the link drops with the write in flight
			return;
		}
		fake.enqueueAck(msg.seqNumber);
	});

	CHECK(s.f.hp.setMultipointMode(true));
	CHECK_THROWS(s.f.hp.setChanges(), RecoverableException);
	// The frame may or may not have arrived: the change stays out, for the host to read back.
	CHECK_EQ(change(s.f).step, MultipointStep::Sent);
	CHECK_EQ(s.f.hp.isChanged(), false);
}
