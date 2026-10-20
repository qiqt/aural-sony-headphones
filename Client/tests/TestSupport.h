#pragma once

// Shared wiring for the engine tests: a fake transport plugged into the real
// BluetoothWrapper, Headphones and Listener, exactly as the application wires them.

#include "FakeBluetoothConnector.h"
#include "Headphones.h"
#include "Listener.h"
#include "TestHarness.h"

#include <atomic>
#include <memory>
#include <string>

class EngineFixture
{
	// Owned by bt; kept as a raw pointer so tests can script the device side.
	FakeBluetoothConnector* _fake;

public:
	BluetoothWrapper bt;
	Headphones hp;
	std::unique_ptr<Listener> listener;
	std::atomic<int> disconnectCallbacks{ 0 };

	EngineFixture()
		: _fake(new FakeBluetoothConnector())
		, bt(std::unique_ptr<IBluetoothConnector>(_fake))
		, hp(bt)
	{
	}

	~EngineFixture()
	{
		stopSession();
	}

	FakeBluetoothConnector& fake() { return *_fake; }

	// Connect and start the reader. The short ACK timeout keeps "no reply" cases quick;
	// every other case waits on a condition, not on the clock.
	void start(std::chrono::milliseconds ackTimeout = std::chrono::milliseconds(200))
	{
		bt.setAckTimeout(ackTimeout);
		bt.connect("00:11:22:33:44:55", SonyProtocol::V1);
		_startListener();
	}

	// A deliberate local shutdown, in the order the application must use: tell the reader
	// this is our doing, close the socket to release it, then join.
	void stopSession()
	{
		if (listener)
			listener->requestStop();
		bt.disconnect();
		if (listener)
		{
			listener->stop();
			listener.reset();
		}
	}

	void reconnect(const std::string& addr = "AA:BB:CC:DD:EE:FF")
	{
		stopSession();
		bt.connect(addr, SonyProtocol::V1);
		_startListener();
	}

private:
	void _startListener()
	{
		listener = std::make_unique<Listener>(hp, bt);
		listener->setDisconnectHandler([this]() { disconnectCallbacks.fetch_add(1); });
		listener->start();
	}
};

namespace replies {

// The 8-byte NC/ASM RET payload: [cmd][inquiredType][effect][ncSettingType][dualSingle]
// [asmSettingType][asmId][asmLevel].
inline Buffer ncAsm(bool ambientOn, int level, bool focusOnVoice = false, bool notify = false)
{
	Buffer payload;
	payload.push_back(static_cast<char>(notify ? COMMAND_TYPE::NCASM_NTFY_PARAM
		: COMMAND_TYPE::NCASM_RET_PARAM));
	payload.push_back(static_cast<char>(NC_ASM_INQUIRED_TYPE::NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE));
	payload.push_back(static_cast<char>(ambientOn ? NC_ASM_EFFECT::ADJUSTMENT_COMPLETION : NC_ASM_EFFECT::OFF));
	payload.push_back(static_cast<char>(NC_ASM_SETTING_TYPE::LEVEL_ADJUSTMENT));
	payload.push_back(static_cast<char>(NC_DUAL_SINGLE_VALUE::OFF));
	payload.push_back(static_cast<char>(ASM_SETTING_TYPE::LEVEL_ADJUSTMENT));
	payload.push_back(static_cast<char>(focusOnVoice ? ASM_ID::VOICE : ASM_ID::NORMAL));
	payload.push_back(static_cast<char>(level));
	return payload;
}

// Battery RET: [cmd][batteryType][level][charging]
inline Buffer battery(int level, bool charging = false)
{
	Buffer payload;
	payload.push_back(static_cast<char>(BATTERY_LEVEL_RET));
	payload.push_back(0x00);
	payload.push_back(static_cast<char>(level));
	payload.push_back(charging ? 0x01 : 0x00);
	return payload;
}

// Capability reply: [cmd] followed by the function-type bytes the device supports.
inline Buffer capabilities(std::vector<FUNCTION_TYPE> functions)
{
	Buffer payload;
	payload.push_back(static_cast<char>(COMMAND_TYPE::CAPABILITY_QUERY_RESPONSE));
	for (auto fn : functions)
		payload.push_back(static_cast<char>(fn));
	return payload;
}

} // namespace replies
