// Touch panel and wearing control writes. Both layouts come from Sound Connect 13.3.0 static code
// (the Python reference vectors); neither frame has been sent to a headset yet. These tests prove
// the engine writes only what discovery negotiated and believes only what the device reads back,
// not that an XM4 accepts the frames.

#include "TestSupport.h"

#include <atomic>
#include <memory>

using replies::hex;

namespace {

ToggleSnapshot touch(EngineFixture& f) { return f.hp.getTouchPanel(); }
ToggleSnapshot wearing(EngineFixture& f) { return f.hp.getWearingControl(); }

bool ready(const ToggleSnapshot& t)
{
	return t.support == CapabilityState::Supported && t.availability == Availability::Available && t.valueKnown;
}

std::string hexByte(unsigned char b) { return toHex(Buffer{ static_cast<char>(b) }); }

// Every table-1 payload sent since the last clearSent(), in order, for exact-sequence checks.
std::string sentLog(EngineFixture& f)
{
	std::string out;
	for (const auto& payload : sentPayloads(f.fake()))
		out += (out.empty() ? "" : " | ") + payload;
	return out;
}

// A headset with its touch panel in `slot` and wearing control alongside. It keeps both values,
// applies writes unless told not to, and answers every read with what it then holds.
struct ControlsDevice
{
	std::shared_ptr<std::atomic<int>> touchValue = std::make_shared<std::atomic<int>>(0);
	std::shared_ptr<std::atomic<int>> wearingValue = std::make_shared<std::atomic<int>>(1);
	std::shared_ptr<std::atomic<bool>> applyWrites = std::make_shared<std::atomic<bool>>(true);
	DeviceScript script;

	explicit ControlsDevice(unsigned char slot = 0xD2)
	{
		const std::string s = hexByte(slot);
		script.on("D0 " + s + " 01", replies::generalSettingCapability(slot, v1::GS_TITLE_TOUCH_PANEL))
			.on("D2 " + s, hex("D3 " + s + " 00"))
			.on("F0 03", hex("F1 03 00"))
			.on("F2 03", hex("F3 03 00"));

		auto touchV = touchValue;
		auto wearV = wearingValue;
		auto apply = applyWrites;
		for (int v = 0; v <= 1; ++v)
		{
			script.onRequest("D8 " + s + " 01 0" + std::to_string(v),
				[touchV, apply, v](FakeBluetoothConnector&, const CommandSerializer::Message&) { if (*apply) *touchV = v; });
			script.onRequest("F8 03 00 0" + std::to_string(v),
				[wearV, apply, v](FakeBluetoothConnector&, const CommandSerializer::Message&) { if (*apply) *wearV = v; });
		}
		script.onRequest("D6 " + s, [touchV, slot](FakeBluetoothConnector& device, const CommandSerializer::Message&) {
			device.enqueueData(Buffer{ static_cast<char>(0xD7), static_cast<char>(slot), 0x01,
				static_cast<char>(touchV->load()) });
		});
		script.onRequest("F6 03", [wearV](FakeBluetoothConnector& device, const CommandSerializer::Message&) {
			device.enqueueData(Buffer{ static_cast<char>(0xF7), 0x03, 0x00, static_cast<char>(wearV->load()) });
		});
	}
};

// Advertise the touch slot and wearing control (F3), then run discovery until both are usable.
void discoverControls(EngineFixture& f, unsigned char slot = 0xD2)
{
	CHECK(deliver(f, hex("07 01 02 " + hexByte(slot) + " F3")));
	f.hp.sendPendingQueries();
	CHECK(testing::waitFor([&] {
		return touch(f).support == CapabilityState::Supported && wearing(f).support == CapabilityState::Supported;
	}));
	f.hp.sendPendingQueries();
	CHECK_MSG(testing::waitFor([&] { return ready(touch(f)) && ready(wearing(f)); }),
		"discovery should end with both controls available and their values read");
	CHECK(settle(f));
}

} // namespace

// ---- the write and its confirmation -----------------------------------------------------------

TEST(toggles_touch_panel_write_uses_the_discovered_slot_and_is_confirmed_by_a_read)
{
	EngineFixture f;
	ControlsDevice device(0xD2);
	device.script.installOn(f.fake());
	f.start();
	discoverControls(f, 0xD2);
	CHECK_EQ(touch(f).value, false);

	f.fake().clearSent();
	CHECK(f.hp.setTouchPanel(true));
	CHECK(f.hp.isChanged());
	CHECK_EQ(touch(f).status, SettingStatus::Requested);
	CHECK_EQ(touch(f).requested, true);
	CHECK_EQ(touch(f).value, false);   // the request is not the device's value

	f.hp.setChanges();
	CHECK_EQ(sentLog(f), std::string("D8 D2 01 01"));
	// The ACK says the frame arrived; the value shown is still the last one the device reported.
	CHECK_EQ(touch(f).status, SettingStatus::Acknowledged);
	CHECK_EQ(touch(f).value, false);
	CHECK(f.hp.hasPendingQueries());

	f.hp.sendPendingQueries();
	CHECK(wasSent(f.fake(), "D6 D2"));
	CHECK(testing::waitFor([&] { return touch(f).status == SettingStatus::Confirmed; }));
	CHECK_EQ(touch(f).value, true);
	CHECK_EQ(f.hp.isChanged(), false);
}

TEST(toggles_wearing_control_write_is_confirmed_by_a_read)
{
	EngineFixture f;
	ControlsDevice device;
	device.script.installOn(f.fake());
	f.start();
	discoverControls(f);
	CHECK_EQ(wearing(f).value, true);

	f.fake().clearSent();
	CHECK(f.hp.setWearingControl(false));
	f.hp.setChanges();
	CHECK_EQ(sentLog(f), std::string("F8 03 00 00"));
	CHECK_EQ(wearing(f).status, SettingStatus::Acknowledged);

	f.hp.sendPendingQueries();
	CHECK(wasSent(f.fake(), "F6 03"));
	CHECK(testing::waitFor([&] { return wearing(f).status == SettingStatus::Confirmed; }));
	CHECK_EQ(wearing(f).value, false);
	// Touch was never asked to change and nothing was written to it.
	CHECK(!wasSent(f.fake(), "D8 D2 01 00"));
	CHECK(!wasSent(f.fake(), "D8 D2 01 01"));
}

TEST(toggles_a_value_the_device_did_not_apply_is_shown_as_the_device_state)
{
	EngineFixture f;
	ControlsDevice device;
	device.script.installOn(f.fake());
	f.start();
	discoverControls(f);

	*device.applyWrites = false;   // ACKs the frame, keeps its setting
	CHECK(f.hp.setTouchPanel(true));
	f.hp.setChanges();
	f.hp.sendPendingQueries();

	CHECK(testing::waitFor([&] { return touch(f).status == SettingStatus::Confirmed; }));
	CHECK_EQ(touch(f).value, false);
	CHECK_MSG(touch(f).requested == false, "the request the device ignored is not kept as the shown state");
	CHECK_EQ(f.hp.isChanged(), false);
}

TEST(toggles_reply_arriving_before_the_ack_confirms_the_write)
{
	EngineFixture f;
	f.fake().setScript([&](FakeBluetoothConnector& fake, const CommandSerializer::Message& msg) {
		if (msg.dataType == DATA_TYPE::ACK)
			return;
		if (toHex(msg.messageBytes) == "D8 D2 01 01")
		{
			// The device announces the new value before acknowledging the frame that set it.
			fake.enqueueData(hex("D9 D2 01 01"));
			CHECK(testing::waitFor([&] { return f.hp.getTouchPanel().value; }));
		}
		fake.enqueueAck(msg.seqNumber);
	});
	f.start();

	CHECK(deliver(f, hex("07 01 01 D2")));
	CHECK(deliver(f, replies::generalSettingCapability(0xD2, v1::GS_TITLE_TOUCH_PANEL)));
	CHECK(deliver(f, hex("D3 D2 00")));
	CHECK(deliver(f, hex("D7 D2 01 00")));
	CHECK(ready(touch(f)));

	CHECK(f.hp.setTouchPanel(true));
	f.hp.setChanges();
	CHECK_MSG(touch(f).status == SettingStatus::Confirmed,
		"an ACK landing after the device's own report must not walk Confirmed back to Acknowledged");
	CHECK_EQ(touch(f).value, true);
}

// ---- when a write is refused --------------------------------------------------------------------

TEST(toggles_writes_are_refused_until_the_device_has_described_and_reported_the_control)
{
	EngineFixture f;
	f.start();

	// Nothing known: no slot to write to.
	CHECK(!f.hp.setTouchPanel(true));
	CHECK(!f.hp.setWearingControl(true));

	CHECK(deliver(f, hex("07 01 02 D3 F3")));
	CHECK(deliver(f, replies::generalSettingCapability(0xD3, v1::GS_TITLE_TOUCH_PANEL)));
	CHECK(deliver(f, hex("F1 03 00")));
	// Described, but availability and value are unread: "unknown" must not be written over.
	CHECK(!f.hp.setTouchPanel(true));
	CHECK(!f.hp.setWearingControl(true));

	CHECK(deliver(f, hex("D3 D3 00")));
	CHECK(deliver(f, hex("F3 03 00")));
	// Available, value still unread.
	CHECK(!f.hp.setTouchPanel(true));
	CHECK(!f.hp.setWearingControl(true));

	CHECK(deliver(f, hex("D7 D3 01 00")));
	CHECK(deliver(f, hex("F7 03 00 00")));
	CHECK(f.hp.setTouchPanel(true));
	CHECK(f.hp.setWearingControl(true));

	f.fake().clearSent();
	f.hp.setChanges();
	CHECK(wasSent(f.fake(), "D8 D3 01 01"));
	CHECK(wasSent(f.fake(), "F8 03 00 01"));
}

TEST(toggles_unavailable_controls_refuse_writes)
{
	EngineFixture f;
	f.start();

	CHECK(deliver(f, hex("07 01 02 D1 F3")));
	CHECK(deliver(f, replies::generalSettingCapability(0xD1, v1::GS_TITLE_TOUCH_PANEL)));
	CHECK(deliver(f, hex("F1 03 00")));
	CHECK(deliver(f, hex("D7 D1 01 01")));
	CHECK(deliver(f, hex("F7 03 00 01")));
	// 01 = unavailable: the opposite polarity of an on/off value.
	CHECK(deliver(f, hex("D3 D1 01")));
	CHECK(deliver(f, hex("F3 03 01")));

	CHECK(!f.hp.setTouchPanel(false));
	CHECK(!f.hp.setWearingControl(false));
	CHECK_EQ(f.hp.isChanged(), false);

	// A notification makes them available again; writes are accepted from then on.
	CHECK(deliver(f, hex("D5 D1 00")));
	CHECK(deliver(f, hex("F5 03 00")));
	CHECK(f.hp.setTouchPanel(false));
	CHECK(f.hp.setWearingControl(false));
	CHECK(f.hp.isChanged());
}

TEST(toggles_availability_lost_before_the_send_fails_the_request_instead_of_sending_it)
{
	EngineFixture f;
	ControlsDevice device;
	device.script.installOn(f.fake());
	f.start();
	discoverControls(f);

	CHECK(f.hp.setTouchPanel(true));
	CHECK(f.hp.setWearingControl(false));
	// Before the command worker gets to them, the device says neither can change now.
	CHECK(deliver(f, hex("D5 D2 01")));
	CHECK(deliver(f, hex("F5 03 01")));

	f.fake().clearSent();
	f.hp.setChanges();
	CHECK_MSG(sentLog(f).empty(), "nothing may be written to a control the device reports unavailable");
	CHECK_EQ(touch(f).status, SettingStatus::Failed);
	CHECK_EQ(wearing(f).status, SettingStatus::Failed);
	// What the device reported is untouched by the failed request.
	CHECK_EQ(touch(f).value, false);
	CHECK_EQ(wearing(f).value, true);
	CHECK_EQ(f.hp.isChanged(), false);
}

TEST(toggles_wearing_control_needs_the_on_off_type_before_any_write)
{
	// F3 advertised, but the capability names a type with no known encoding.
	{
		EngineFixture f;
		f.start();
		CHECK(deliver(f, hex("07 01 01 F3")));
		CHECK(deliver(f, hex("F1 03 01")));
		CHECK(deliver(f, hex("F3 03 00")));
		CHECK(deliver(f, hex("F7 03 00 01")));
		CHECK(wearing(f).support == CapabilityState::Unsupported);
		CHECK(!f.hp.setWearingControl(false));
	}
	// The right type, but F3 was never advertised.
	{
		EngineFixture f;
		f.start();
		CHECK(deliver(f, hex("07 01 01 62")));
		CHECK(deliver(f, hex("F1 03 00")));
		CHECK(deliver(f, hex("F3 03 00")));
		CHECK(deliver(f, hex("F7 03 00 01")));
		CHECK(!f.hp.setWearingControl(false));
		f.fake().clearSent();
		f.hp.setChanges();
		CHECK(!wasSent(f.fake(), "F8 03 00 00"));
	}
}

// ---- pending requests and device reports -------------------------------------------------------

TEST(toggles_pending_request_survives_a_notification_of_the_old_value)
{
	EngineFixture f;
	ControlsDevice device;
	device.script.installOn(f.fake());
	f.start();
	discoverControls(f);

	CHECK(f.hp.setTouchPanel(true));
	// The device repeats its current value before our write goes out.
	CHECK(deliver(f, hex("D9 D2 01 00")));
	CHECK_EQ(touch(f).status, SettingStatus::Requested);
	CHECK_EQ(touch(f).requested, true);
	CHECK_EQ(touch(f).value, false);

	f.fake().clearSent();
	f.hp.setChanges();
	CHECK(wasSent(f.fake(), "D8 D2 01 01"));
}

TEST(toggles_device_notifications_update_the_value_when_nothing_is_pending)
{
	EngineFixture f;
	ControlsDevice device;
	device.script.installOn(f.fake());
	f.start();
	discoverControls(f);

	CHECK(deliver(f, hex("D9 D2 01 01")));
	CHECK(deliver(f, hex("F9 03 00 00")));
	CHECK_EQ(touch(f).value, true);
	CHECK_EQ(wearing(f).value, false);
	CHECK_EQ(touch(f).status, SettingStatus::Confirmed);
	CHECK_EQ(f.hp.isChanged(), false);
}

// ---- sessions ------------------------------------------------------------------------------------

TEST(toggles_disconnect_during_a_write_fails_it_and_reconnect_does_not_resend_it)
{
	EngineFixture f;
	ControlsDevice device;
	device.script.installOn(f.fake());
	f.start();
	discoverControls(f);

	// The link drops with the write in flight, before its ACK.
	f.fake().setScript([](FakeBluetoothConnector& fake, const CommandSerializer::Message& msg) {
		if (msg.dataType == DATA_TYPE::ACK)
			return;
		if (toHex(msg.messageBytes) == "D8 D2 01 01")
		{
			fake.closeRemote();
			return;
		}
		fake.enqueueAck(msg.seqNumber);
	});

	CHECK(f.hp.setTouchPanel(true));
	CHECK_THROWS(f.hp.setChanges(), RecoverableException);
	CHECK_EQ(touch(f).status, SettingStatus::Failed);
	CHECK_EQ(touch(f).value, false);
	CHECK_EQ(f.hp.hasPendingQueries(), false);

	// The session ends; the next one starts from nothing and rediscovers.
	f.hp.resetState();
	device.script.installOn(f.fake());
	f.reconnect();
	f.fake().clearSent();

	CHECK_EQ(f.hp.isChanged(), false);
	f.hp.setChanges();
	f.hp.sendPendingQueries();
	CHECK_MSG(sentLog(f).empty(), "nothing from the old session may reach the new link");

	discoverControls(f);
	f.hp.setChanges();
	CHECK_MSG(!wasSent(f.fake(), "D8 D2 01 01"), "a request from a previous session is never replayed");
	CHECK_EQ(touch(f).value, false);
	CHECK_EQ(touch(f).status, SettingStatus::Confirmed);
}

TEST(toggles_after_reconnect_writes_follow_the_newly_discovered_slot)
{
	EngineFixture f;
	{
		ControlsDevice first(0xD1);
		first.script.installOn(f.fake());
		f.start();
		discoverControls(f, 0xD1);
	}

	f.hp.resetState();
	ControlsDevice second(0xD3);
	second.script.installOn(f.fake());
	f.reconnect();
	discoverControls(f, 0xD3);
	CHECK_EQ(int(touch(f).selector), 0xD3);

	// A reply for the old slot says nothing about touch now.
	CHECK(deliver(f, hex("D9 D1 01 01")));
	CHECK_EQ(touch(f).value, false);

	f.fake().clearSent();
	CHECK(f.hp.setTouchPanel(true));
	f.hp.setChanges();
	CHECK_EQ(sentLog(f), std::string("D8 D3 01 01"));
}
