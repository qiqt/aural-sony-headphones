// SON-7 and SON-8, at the level where sessions actually live.
//
// The engine tests cover the transport. These cover HeadphonesController: the object that owns
// a session, starts asynchronous work under it, and has to make sure nothing from a finished
// session is ever believed again. That ownership cannot be exercised from the engine tests -
// it is the controller that holds the futures, the session counter and the teardown order.
//
// The controller is driven exactly as the app drives it: a real Qt event loop turning its
// 20 Hz pump(), and a FakeBluetoothConnector on the other side instead of the Windows stack.

#include "backend/HeadphonesController.h"

#include "FakeBluetoothConnector.h"
#include "TestHarness.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QSignalSpy>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {

// Spin the Qt event loop until pred() holds. The controller does all of its work from a
// QTimer, so nothing at all happens unless events are delivered - this is the equivalent of
// testing::waitFor for a QObject.
template <class Pred>
bool spinUntil(Pred pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (pred())
			return true;
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	return pred();
}

// Keep the loop turning for a while whether or not anything happens - used to show that a
// value stays wrong-free rather than merely starts out right.
void spinFor(std::chrono::milliseconds duration)
{
	const auto deadline = std::chrono::steady_clock::now() + duration;
	while (std::chrono::steady_clock::now() < deadline)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

const BluetoothDevice XM4{ "WH-1000XM4", "00:11:22:33:44:55" };
const BluetoothDevice XM3{ "WH-1000XM3", "AA:BB:CC:DD:EE:FF" };

// Controller plus the fake it is driving, wired the way main() wires the real one.
struct ControllerFixture
{
	FakeBluetoothConnector* fake;
	std::unique_ptr<HeadphonesController> hp;

	explicit ControllerFixture(std::vector<BluetoothDevice> devices = { XM4 })
		: fake(new FakeBluetoothConnector())
	{
		fake->setDiscoveredDevices(std::move(devices));
		hp = std::make_unique<HeadphonesController>(
			std::unique_ptr<IBluetoothConnector>(fake), nullptr);
	}

	// Discovery is started by the constructor; wait for it to land before touching devices().
	bool waitForDiscovery()
	{
		return spinUntil([this] { return !hp->devices().isEmpty(); });
	}

	bool connectTo(const BluetoothDevice& device)
	{
		hp->connectTo(QString::fromStdString(device.mac), QString::fromStdString(device.name));
		return spinUntil([this] { return hp->connected(); });
	}
};

} // namespace

TEST(controller_discovery_reports_recognised_and_unrecognised_devices)
{
	ControllerFixture f({ XM4, BluetoothDevice{ "Leon's Headphones", "11:22:33:44:55:66" } });
	CHECK(f.waitForDiscovery());

	const auto devices = f.hp->devices();
	CHECK_EQ(devices.size(), 2);
	CHECK_EQ(devices[0].toMap().value("recognised").toBool(), true);
	CHECK_EQ(devices[0].toMap().value("model").toString().toStdString(), std::string("WH-1000XM4"));
	// An unrecognised name is shown as itself, never dressed up as a model we know.
	CHECK_EQ(devices[1].toMap().value("recognised").toBool(), false);
	CHECK_EQ(devices[1].toMap().value("model").toString().toStdString(), std::string("Leon's Headphones"));
}

// SON-7: the user backs out while the connect is still in flight. The session must end
// cleanly, and the connect that was already running must not deliver a connected device
// afterwards.
TEST(controller_disconnect_during_connect_never_reports_a_connection)
{
	ControllerFixture f;
	CHECK(f.waitForDiscovery());

	// Freeze the connect attempt with the controller believing it is Connecting.
	f.fake->blockConnect();
	f.hp->connectTo(QStringLiteral("00:11:22:33:44:55"), QStringLiteral("WH-1000XM4"));
	CHECK(f.fake->waitForConnectAttempt(1));
	CHECK(spinUntil([&] { return f.hp->connecting(); }));

	// Back out mid-connect.
	f.hp->disconnectDevice();
	f.fake->releaseConnect();

	CHECK(spinUntil([&] { return !f.hp->connecting(); }));
	CHECK_MSG(!f.hp->connected(), "a connect abandoned mid-flight must not report a connection");
	CHECK_EQ(f.hp->deviceName().toStdString(), std::string(""));

	// And it stays that way: the abandoned attempt cannot surface a session later.
	spinFor(std::chrono::milliseconds(300));
	CHECK_MSG(!f.hp->connected(), "the abandoned attempt must not connect after the teardown");
}

// SON-7: an OS battery lookup started under one session must not describe the next one - or
// the disconnected state.
TEST(controller_battery_lookup_outstanding_at_teardown_is_discarded)
{
	ControllerFixture f;
	CHECK(f.waitForDiscovery());
	f.fake->setOsBatteryLevel(88);

	CHECK(f.connectTo(XM4));

	// Freeze a lookup in flight, then tear the session down underneath it.
	f.fake->blockBatteryLookup();
	f.hp->refreshBattery();
	CHECK_MSG(f.fake->waitForBatteryLookup(1), "the controller should have started an OS battery lookup");

	f.hp->disconnectDevice();
	CHECK(spinUntil([&] { return !f.hp->connected(); }));

	// The stale 88 belonged to a session that no longer exists.
	spinFor(std::chrono::milliseconds(300));
	CHECK_MSG(f.hp->batteryLevel() != 88,
		"a battery level from a finished session must not be shown");
	CHECK_EQ(f.hp->batteryLevel(), -1);
}

// SON-8: the headphones close the link. The UI has to end up genuinely disconnected - not
// merely "connected: false" with the old device still described.
TEST(controller_remote_close_returns_the_ui_to_a_disconnected_state)
{
	ControllerFixture f;
	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));
	CHECK_EQ(f.hp->deviceModel().toStdString(), std::string("WH-1000XM4"));

	QSignalSpy errors(f.hp.get(), &HeadphonesController::errorOccurred);

	f.fake->closeRemote();

	CHECK_MSG(spinUntil([&] { return !f.hp->connected(); }),
		"a remote close must return the controller to disconnected");
	CHECK_MSG(errors.count() >= 1, "the user has to be told the headphones closed the link");

	// Nothing about the old device survives.
	CHECK_EQ(f.hp->deviceName().toStdString(), std::string(""));
	CHECK_EQ(f.hp->batteryLevel(), -1);
	CHECK_EQ(f.hp->modelRecognised(), false);
	CHECK_EQ(f.hp->settingStatus().value("ambient").toString().toStdString(), std::string("unknown"));
	CHECK_EQ(f.hp->settingStatus().value("dsee").toString().toStdString(), std::string("unknown"));
	CHECK_EQ(f.hp->syncing(), false);
}

// SON-7: reconnecting to a different device must not inherit anything from the first one.
TEST(controller_second_device_does_not_inherit_the_first_ones_state)
{
	ControllerFixture f({ XM4, XM3 });
	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));

	// Move some settings away from their defaults on device one.
	f.hp->setAmbientMode(1);
	f.hp->setEqPreset(9);
	f.hp->setDsee(true);
	CHECK_EQ(f.hp->ambientMode(), 1);
	CHECK_EQ(f.hp->dsee(), true);

	f.hp->disconnectDevice();
	CHECK(spinUntil([&] { return !f.hp->connected(); }));
	CHECK(f.connectTo(XM3));

	CHECK_EQ(f.hp->deviceModel().toStdString(), std::string("WH-1000XM3"));
	CHECK_MSG(f.hp->ambientMode() == 2, "ambient mode must start from the device, not the last one");
	CHECK_EQ(f.hp->eqPreset(), 0);
	CHECK_EQ(f.hp->dsee(), false);
	CHECK_EQ(f.hp->settingStatus().value("dsee").toString().toStdString(), std::string("unknown"));
}

// SON-11: an unrecognised name gets a deliberately narrow control surface, not the full V1 one.
TEST(controller_unrecognised_device_is_treated_conservatively)
{
	const BluetoothDevice unknown{ "Leon's Headphones", "11:22:33:44:55:66" };
	ControllerFixture f({ unknown });
	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(unknown));

	CHECK_EQ(f.hp->modelRecognised(), false);
	// The raw name, never an invented model.
	CHECK_EQ(f.hp->deviceModel().toStdString(), std::string("Leon's Headphones"));

	// Model-specific extras stay off until something establishes the device supports them.
	CHECK_MSG(!f.hp->spatialAvailable(), "spatial audio must not be offered to an unknown model");
	CHECK_MSG(!f.hp->systemSettingsAvailable(), "write-only system settings must not be offered to an unknown model");
	CHECK_MSG(!f.hp->equalizerAvailable(), "the equalizer must not be offered to an unknown model");
	// Ambient sound is the one control the legacy attempt is actually for.
	CHECK_MSG(f.hp->ambientAvailable(), "the legacy attempt is about ambient sound; keep it available");
}

TEST(controller_recognised_v1_device_keeps_the_full_control_surface)
{
	ControllerFixture f;
	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));

	CHECK_EQ(f.hp->modelRecognised(), true);
	CHECK(f.hp->ambientAvailable());
	CHECK(f.hp->spatialAvailable());
	CHECK(f.hp->systemSettingsAvailable());
	CHECK(f.hp->equalizerAvailable());
}

// The capability reply's follow-up requests are queued by the reader and put on the wire by the
// controller's command worker - the reader itself must stay free to receive their ACKs.
TEST(controller_discovery_follow_ups_are_sent_by_the_command_worker)
{
	ControllerFixture f;
	DeviceScript device;
	device.on("06 01", replies::hex("07 01 03 62 D1 F3"))
		.on("D0 D1 01", replies::generalSettingCapability(0xD1, v1::GS_TITLE_TOUCH_PANEL))
		.on("F0 03", replies::hex("F1 03 00"));
	device.installOn(*f.fake);
	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));

	CHECK_MSG(spinUntil([&] { return wasSent(*f.fake, "D6 D1") && wasSent(*f.fake, "F6 03"); }),
		"discovery should end in availability and value reads for what the device described");
	CHECK(wasSent(*f.fake, "D2 D1"));
	CHECK(wasSent(*f.fake, "F2 03"));
	CHECK(f.hp->capabilitiesKnown());
	CHECK(f.hp->connected());
}

// A bounded reply is an answer: what it does not list is withdrawn, even for a model we know.
TEST(controller_capability_reply_withdraws_unadvertised_controls)
{
	ControllerFixture f;
	DeviceScript device;
	device.on("06 01", replies::hex("07 01 01 62"));
	device.installOn(*f.fake);
	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));

	CHECK(spinUntil([&] { return f.hp->capabilitiesKnown(); }));
	CHECK(f.hp->ambientAvailable());
	CHECK(!f.hp->spatialAvailable());
	CHECK(!f.hp->supportsSpeakToChat());
	CHECK(!f.hp->supportsMultipoint());
}

// The device's own values replace the app's defaults once they are read back.
TEST(controller_read_back_replaces_defaults_with_the_device_values)
{
	ControllerFixture f;
	DeviceScript device;
	device.on("06 01", replies::hex("07 01 06 51 E2 39 F4 F5 41"))
		.on("56 01", replies::hex("57 01 11 06 0A 0A 0A 0A 0A 0A"))     // Excited
		.on("E6 02", replies::hex("E7 02 00 01"))                       // DSEE on
		.on("46 01 01", replies::hex("47 01 01 01"), DATA_TYPE::DATA_MDR_NO2, DATA_TYPE::DATA_MDR_NO2)
		.on("F6 04", replies::hex("F7 04 01 10 00"))                    // when taken off
		.on("F6 05", replies::hex("F7 05 00 01"))                       // Speak-to-Chat on
		.on("FA 05", replies::hex("FB 05 00 01 01 02"))                 // high, voice focus, slow
		.on("46 01", replies::hex("47 01 03"));                         // VPT concert hall
	device.installOn(*f.fake);
	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));

	auto status = [&](const char* key) { return f.hp->settingStatus().value(key).toString().toStdString(); };
	CHECK_MSG(spinUntil([&] {
		return status("dsee") == "confirmed" && status("voiceGuidance") == "confirmed" &&
			status("autoPowerOff") == "confirmed" && status("speakToChat") == "confirmed" &&
			status("equalizer") == "confirmed";
	}), "every advertised setting should be read back on connect");

	CHECK_EQ(f.hp->dsee(), true);
	CHECK_EQ(f.hp->voiceGuidance(), true);
	CHECK_EQ(f.hp->autoPowerOff(), static_cast<int>(AUTO_POWER_OFF::WHEN_TAKEN_OFF));
	CHECK_EQ(f.hp->s2cEnabled(), true);
	CHECK_EQ(f.hp->s2cSensitivity(), 1);
	CHECK_EQ(f.hp->s2cVoice(), true);
	CHECK_EQ(f.hp->s2cAutoOff(), 2);
	CHECK_EQ(f.hp->eqPreset(), 2);   // Excited
	CHECK(spinUntil([&] { return f.hp->vptType() == 3; }));

	// Sound position (42) was not advertised: its control goes, VPT's stays.
	CHECK(f.hp->vptAvailable());
	CHECK(!f.hp->soundPositionAvailable());
	CHECK(f.hp->spatialAvailable());
}

// "A different reported value should be shown as the device's actual state."
TEST(controller_device_value_replaces_a_request_it_did_not_apply)
{
	ControllerFixture f;
	DeviceScript device;
	device.on("06 01", replies::hex("07 01 01 E2"))
		.on("E6 02", replies::hex("E7 02 00 00"));   // DSEE stays off whatever we write
	device.installOn(*f.fake);
	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));
	CHECK(spinUntil([&] { return f.hp->settingStatus().value("dsee").toString() == "confirmed"; }));

	f.hp->setDsee(true);
	CHECK_EQ(f.hp->dsee(), true);   // the request shows while it is in flight
	CHECK(spinUntil([&] { return wasSent(*f.fake, "E8 02 00 01"); }));
	CHECK_MSG(spinUntil([&] {
		return !f.hp->dsee() && f.hp->settingStatus().value("dsee").toString() == "confirmed";
	}), "the read-back after the write must replace the request with the device's value");
}

TEST(controller_unadvertised_system_settings_are_withdrawn)
{
	ControllerFixture f;
	DeviceScript device;
	device.on("06 01", replies::hex("07 01 01 62"));
	device.installOn(*f.fake);
	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));
	CHECK(spinUntil([&] { return f.hp->capabilitiesKnown(); }));

	CHECK(!f.hp->dseeAvailable());
	CHECK(!f.hp->voiceGuidanceAvailable());
	CHECK(!f.hp->autoPowerOffAvailable());
	CHECK(!f.hp->systemSettingsAvailable());
	CHECK(!f.hp->equalizerAvailable());

	f.hp->setDsee(true);
	spinFor(std::chrono::milliseconds(200));
	CHECK_EQ(f.hp->dsee(), false);
	CHECK(!wasSent(*f.fake, "E8 02 00 01"));
}

TEST(controller_auto_power_off_offers_only_the_models_choices)
{
	ControllerFixture f({ XM4, XM3 });
	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));

	const QVariantList xm4 = f.hp->autoPowerOffOptions();
	CHECK_EQ(xm4.size(), 2);
	CHECK(xm4.contains(static_cast<int>(AUTO_POWER_OFF::OFF)));
	CHECK(xm4.contains(static_cast<int>(AUTO_POWER_OFF::WHEN_TAKEN_OFF)));

	// A timer the XM4 does not offer is refused before it reaches the wire.
	f.hp->setAutoPowerOff(static_cast<int>(AUTO_POWER_OFF::AFTER_30_MIN));
	CHECK_EQ(f.hp->autoPowerOff(), static_cast<int>(AUTO_POWER_OFF::OFF));
	f.hp->setAutoPowerOff(static_cast<int>(AUTO_POWER_OFF::WHEN_TAKEN_OFF));
	CHECK(spinUntil([&] { return wasSent(*f.fake, "F8 04 01 10 00"); }));
	CHECK(!wasSent(*f.fake, "F8 04 01 01 01"));

	f.hp->disconnectDevice();
	CHECK(spinUntil([&] { return !f.hp->connected(); }));
	CHECK(f.connectTo(XM3));
	CHECK_EQ(f.hp->autoPowerOffOptions().size(), 6);
}

// Touch panel and wearing control: offered only as the device describes them, written only once
// their value is known and the device says they can change, and shown as what it reports.
TEST(controller_discovered_controls_follow_the_device)
{
	ControllerFixture f;
	auto touchValue = std::make_shared<std::atomic<int>>(0);
	DeviceScript device;
	device.on("06 01", replies::hex("07 01 02 D2 F3"))
		.on("D0 D2 01", replies::generalSettingCapability(0xD2, v1::GS_TITLE_TOUCH_PANEL))
		.on("D2 D2", replies::hex("D3 D2 00"))
		.on("F0 03", replies::hex("F1 03 00"))
		.on("F2 03", replies::hex("F3 03 01"))          // wearing control: unavailable right now
		.on("F6 03", replies::hex("F7 03 00 01"))
		.onRequest("D8 D2 01 01", [touchValue](FakeBluetoothConnector&, const CommandSerializer::Message&) { *touchValue = 1; })
		.onRequest("D6 D2", [touchValue](FakeBluetoothConnector& fake, const CommandSerializer::Message&) {
			fake.enqueueData(Buffer{ static_cast<char>(0xD7), static_cast<char>(0xD2), 0x01,
				static_cast<char>(touchValue->load()) });
		});
	device.installOn(*f.fake);

	auto touch = [&] { return f.hp->touchPanel(); };
	auto wearing = [&] { return f.hp->wearingControl(); };
	CHECK_EQ(touch().value("phase").toString().toStdString(), std::string("absent"));

	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));
	CHECK_MSG(spinUntil([&] {
		return touch().value("phase").toString() == "ready" && wearing().value("phase").toString() == "unavailable";
	}), "both controls should be described, read, and shown with the availability the device reported");
	CHECK_EQ(touch().value("checked").toBool(), false);
	CHECK_EQ(wearing().value("checked").toBool(), true);
	CHECK_EQ(wearing().value("valueKnown").toBool(), true);

	// The device says wearing control cannot change now: the request goes nowhere.
	f.hp->setWearingControlEnabled(false);
	spinFor(std::chrono::milliseconds(200));
	CHECK(!wasSent(*f.fake, "F8 03 00 00"));
	CHECK_EQ(wearing().value("checked").toBool(), true);

	// Touch can: the request shows at once as pending, then as the device's confirmed value.
	f.hp->setTouchPanelEnabled(true);
	CHECK_EQ(touch().value("checked").toBool(), true);
	CHECK_EQ(touch().value("pending").toBool(), true);
	CHECK(spinUntil([&] { return wasSent(*f.fake, "D8 D2 01 01"); }));
	CHECK_MSG(spinUntil([&] {
		return touch().value("status").toString() == "confirmed" && !touch().value("pending").toBool();
	}), "the read after the write should confirm it");
	CHECK_EQ(touch().value("checked").toBool(), true);
	CHECK_EQ(f.hp->settingStatus().value("touchPanel").toString().toStdString(), std::string("confirmed"));

	// A new session starts from nothing.
	f.hp->disconnectDevice();
	CHECK(spinUntil([&] { return !f.hp->connected(); }));
	CHECK_EQ(touch().value("phase").toString().toStdString(), std::string("absent"));
	CHECK_EQ(wearing().value("phase").toString().toStdString(), std::string("absent"));
	CHECK_EQ(f.hp->settingStatus().value("touchPanel").toString().toStdString(), std::string("unknown"));
}

// Multipoint mode: the headphones ask, the user accepts, the headphones drop the link to apply it.
// Aural reconnects on its own and reads the value. It never resends the change or the answer.
TEST(controller_multipoint_change_is_checked_after_the_headphones_reconnect)
{
	ControllerFixture f;
	MultipointHeadset headset;
	headset.s->dropOnAccept = true;
	headset.installOn(*f.fake);
	QSignalSpy toasts(f.hp.get(), &HeadphonesController::toast);

	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));
	auto mode = [&] { return f.hp->multipointMode(); };
	CHECK(spinUntil([&] { return mode().value("phase").toString() == "ready"; }));
	CHECK_EQ(mode().value("checked").toBool(), false);

	f.hp->setMultipointEnabled(true);
	CHECK_EQ(mode().value("busy").toBool(), true);
	CHECK_MSG(spinUntil([&] { return f.hp->multipointAlert().value("messageId").toInt() == 6; }),
		"the headphones' confirmation should reach the UI");
	CHECK_EQ(f.hp->multipointAlert().value("turningOn").toBool(), true);
	CHECK_EQ(f.hp->multipointAlert().value("choice").toBool(), true);
	CHECK_EQ(mode().value("step").toString().toStdString(), std::string("confirming"));
	spinFor(std::chrono::milliseconds(200));
	CHECK_MSG(!wasSent(*f.fake, "98 01 06 01") && !wasSent(*f.fake, "98 01 06 00"), "only the user answers");

	f.hp->answerMultipointAlert(6, true);
	CHECK(spinUntil([&] { return wasSent(*f.fake, "98 01 06 01"); }));
	// The link drops to apply the change: Aural says it is reconnecting, not that it failed.
	CHECK(spinUntil([&] { return !f.hp->connected(); }));
	CHECK(f.hp->reconnectReason().contains(QStringLiteral("multipoint")));
	CHECK(f.hp->multipointAlert().isEmpty());

	CHECK_MSG(spinUntil([&] { return f.hp->connected() && f.hp->reconnectReason().isEmpty(); },
		std::chrono::milliseconds(15000)), "Aural should reconnect on its own");
	CHECK_MSG(spinUntil([&] {
		return mode().value("phase").toString() == "ready" && mode().value("step").toString() == "idle";
	}), "the check should end once the new session has read the value");
	CHECK_EQ(mode().value("checked").toBool(), true);
	CHECK_EQ(mode().value("status").toString().toStdString(), std::string("confirmed"));
	CHECK_EQ(countSent(*f.fake, "D8 D3 01 01"), size_t(1));
	CHECK_EQ(countSent(*f.fake, "98 01 06 01"), size_t(1));

	bool toldTheOutcome = false;
	for (const auto& args : toasts)
		toldTheOutcome = toldTheOutcome || args.at(0).toString() == QStringLiteral("Multipoint is now on.");
	CHECK(toldTheOutcome);
}

// Seen on a WH-1000XM4 after accepting a multipoint change: the first reconnect attempt failed,
// and the next link opened but the headphones sent start-up notifications without acknowledging
// the INIT. The check has to ride through both and still read the setting.
TEST(controller_multipoint_check_survives_a_failed_connect_and_a_silent_handshake)
{
	ControllerFixture f;
	MultipointHeadset headset;
	headset.s->value = 1;
	headset.s->dropOnAccept = true;
	auto behave = headset.script();
	f.fake->setScript([behave](FakeBluetoothConnector& device, const CommandSerializer::Message& msg) {
		if (device.connectCount() == 2 && msg.dataType != DATA_TYPE::ACK && toHex(msg.messageBytes) == "00 00")
		{
			device.enqueueData(replies::hex("A5 01 00 02"));   // what the XM4 sent instead of the ACK
			return;
		}
		behave(device, msg);
	});
	QSignalSpy toasts(f.hp.get(), &HeadphonesController::toast);
	QSignalSpy errors(f.hp.get(), &HeadphonesController::errorOccurred);

	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));
	auto mode = [&] { return f.hp->multipointMode(); };
	CHECK(spinUntil([&] { return mode().value("phase").toString() == "ready"; }));

	f.hp->setMultipointEnabled(false);
	CHECK(spinUntil([&] { return f.hp->multipointAlert().value("messageId").toInt() == 7; }));
	f.hp->answerMultipointAlert(7, true);
	CHECK(spinUntil([&] { return !f.hp->connected(); }));
	f.fake->failConnect(true);
	CHECK_MSG(spinUntil([&] { return f.fake->connectAttempts() == 2; }, std::chrono::milliseconds(5000)),
		"the first reconnect attempt");
	f.fake->failConnect(false);

	CHECK_MSG(spinUntil([&] { return f.fake->connectCount() == 3 && f.hp->connected(); },
		std::chrono::milliseconds(20000)), "a third link after the silent one");
	CHECK_MSG(spinUntil([&] {
		return mode().value("phase").toString() == "ready" && mode().value("step").toString() == "idle"
			&& f.hp->reconnectReason().isEmpty();
	}, std::chrono::milliseconds(5000)), "the check should end once the setting is read");
	CHECK_EQ(mode().value("checked").toBool(), false);
	CHECK_EQ(mode().value("status").toString().toStdString(), std::string("confirmed"));
	CHECK_EQ(headset.value(), 0);
	CHECK_EQ(errors.count(), 0);

	bool toldTheOutcome = false;
	for (const auto& args : toasts)
		toldTheOutcome = toldTheOutcome || args.at(0).toString() == QStringLiteral("Multipoint is now off.");
	CHECK(toldTheOutcome);
}

TEST(controller_multipoint_cancel_keeps_the_headphones_setting)
{
	ControllerFixture f;
	MultipointHeadset headset;
	headset.installOn(*f.fake);
	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));
	auto mode = [&] { return f.hp->multipointMode(); };
	CHECK(spinUntil([&] { return mode().value("phase").toString() == "ready"; }));

	// An alert about a change nobody made is not put in front of the user.
	f.fake->enqueueData(replies::hex("99 01 06 01"));
	spinFor(std::chrono::milliseconds(200));
	CHECK(f.hp->multipointAlert().isEmpty());

	f.hp->setMultipointEnabled(true);
	CHECK(spinUntil([&] { return !f.hp->multipointAlert().isEmpty(); }));
	// An answer quoting another message ID goes nowhere.
	f.hp->answerMultipointAlert(7, true);
	spinFor(std::chrono::milliseconds(200));
	CHECK(!wasSent(*f.fake, "98 01 07 01"));
	CHECK(!f.hp->multipointAlert().isEmpty());

	f.hp->answerMultipointAlert(6, false);
	CHECK(spinUntil([&] { return wasSent(*f.fake, "98 01 06 00"); }));
	CHECK(spinUntil([&] {
		return f.hp->multipointAlert().isEmpty() && mode().value("step").toString() == "idle";
	}));
	CHECK_EQ(mode().value("checked").toBool(), false);
	CHECK(f.hp->connected());
	CHECK(f.hp->reconnectReason().isEmpty());
	CHECK_EQ(headset.value(), 0);
}

// Seen on a WH-1000XM4: the first command on a new link went unacknowledged, and the app showed
// "Connected" over a dead link until the user changed noise cancelling. The failed handshake now
// ends that session at once, and Aural reconnects once on a fresh link - which is what worked.
TEST(controller_unanswered_handshake_reconnects_once)
{
	ControllerFixture f;
	f.fake->setScript([](FakeBluetoothConnector& fake, const CommandSerializer::Message& msg) {
		if (msg.dataType == DATA_TYPE::ACK)
			return;
		if (fake.connectCount() == 1 && toHex(msg.messageBytes) == "00 00")
			return;   // the first link leaves the INIT unacknowledged
		fake.enqueueAck(msg.seqNumber);
	});
	QSignalSpy errors(f.hp.get(), &HeadphonesController::errorOccurred);

	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));
	CHECK_MSG(spinUntil([&] { return f.fake->connectCount() == 2 && countSent(*f.fake, "06 01") >= 1; },
		std::chrono::milliseconds(10000)), "the failed handshake should be followed by one fresh connection");
	CHECK(spinUntil([&] { return f.hp->connected(); }));
	spinFor(std::chrono::milliseconds(300));
	CHECK(f.hp->connected());
	CHECK_EQ(f.fake->connectCount(), 2);
	CHECK_EQ(errors.count(), 0);
}

TEST(controller_handshake_that_fails_twice_is_reported)
{
	ControllerFixture f;
	f.fake->setScript([](FakeBluetoothConnector& fake, const CommandSerializer::Message& msg) {
		if (msg.dataType == DATA_TYPE::ACK)
			return;
		if (toHex(msg.messageBytes) == "00 00")
			return;   // never acknowledged
		fake.enqueueAck(msg.seqNumber);
	});
	QSignalSpy errors(f.hp.get(), &HeadphonesController::errorOccurred);

	CHECK(f.waitForDiscovery());
	CHECK(f.connectTo(XM4));
	CHECK_MSG(spinUntil([&] { return errors.count() > 0; }, std::chrono::milliseconds(15000)),
		"a second failed handshake should be reported");
	CHECK(errors.at(0).at(0).toString().contains(QStringLiteral("didn't respond")));
	CHECK(!f.hp->connected());
	spinFor(std::chrono::milliseconds(2500));
	CHECK_MSG(f.fake->connectCount() == 2, "one automatic retry, not a loop");
}

int main(int argc, char* argv[])
{
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName("AuralControllerTests");
	QCoreApplication::setOrganizationName("Aural");
	return testing::runAll(argc, argv);
}
