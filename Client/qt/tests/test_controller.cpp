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

int main(int argc, char* argv[])
{
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName("AuralControllerTests");
	QCoreApplication::setOrganizationName("Aural");
	return testing::runAll(argc, argv);
}
