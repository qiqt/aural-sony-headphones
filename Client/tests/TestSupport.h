#pragma once

// Shared wiring for the engine tests: a fake transport plugged into the real
// BluetoothWrapper, Headphones and Listener, exactly as the application wires them.

#include "FakeBluetoothConnector.h"
#include "Headphones.h"
#include "Listener.h"
#include "TestHarness.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

// Supported-functions reply: [07][01 inquiry][count][function IDs...].
inline Buffer capabilities(std::vector<FUNCTION_TYPE> functions)
{
	Buffer payload;
	payload.push_back(static_cast<char>(v1::t1::CONNECT_RET_SUPPORT_FUNCTION));
	payload.push_back(static_cast<char>(v1::SUPPORT_FUNCTION_INQUIRY));
	payload.push_back(static_cast<char>(functions.size()));
	for (auto fn : functions)
		payload.push_back(static_cast<char>(fn));
	return payload;
}

// Bytes written as hex, e.g. hex("D8 D2 01 01"). Keeps packet vectors readable and identical
// to the ones in the protocol notes and the Python reference.
inline Buffer hex(const std::string& text)
{
	Buffer out;
	int nibbles = 0;
	int value = 0;
	for (char c : text)
	{
		int digit;
		if (c >= '0' && c <= '9') digit = c - '0';
		else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
		else continue;
		value = value * 16 + digit;
		if (++nibbles == 2)
		{
			out.push_back(static_cast<char>(value));
			nibbles = 0;
			value = 0;
		}
	}
	return out;
}

// General-setting capability: [D1][slot][02 ENUM_NAME][len][title][len][summary][01 BOOLEAN].
inline Buffer generalSettingCapability(unsigned char slot, const std::string& title,
	const std::string& summary = "", unsigned char type = v1::GS_TYPE_BOOLEAN)
{
	Buffer payload = { static_cast<char>(v1::t1::GENERAL_SETTING_RET_CAPABILITY), static_cast<char>(slot),
		static_cast<char>(v1::GS_STRING_FORMAT_ENUM_NAME), static_cast<char>(title.size()) };
	payload.insert(payload.end(), title.begin(), title.end());
	payload.push_back(static_cast<char>(summary.size()));
	payload.insert(payload.end(), summary.begin(), summary.end());
	payload.push_back(static_cast<char>(type));
	return payload;
}

} // namespace replies

// The payload of every command the host has sent so far, as hex strings ("D0 D2 01"), so a
// test can assert on exact bytes and print something readable when it fails.
inline std::string toHex(const Buffer& bytes)
{
	static const char* digits = "0123456789ABCDEF";
	std::string out;
	for (size_t i = 0; i < bytes.size(); ++i)
	{
		if (i) out.push_back(' ');
		const unsigned char b = static_cast<unsigned char>(bytes[i]);
		out.push_back(digits[b >> 4]);
		out.push_back(digits[b & 0x0F]);
	}
	return out;
}

inline std::vector<std::string> sentPayloads(FakeBluetoothConnector& fake, DATA_TYPE table = DATA_TYPE::DATA_MDR)
{
	std::vector<std::string> out;
	for (const auto& msg : fake.sentCommands())
		if (msg.dataType == table)
			out.push_back(toHex(msg.messageBytes));
	return out;
}

inline bool wasSent(FakeBluetoothConnector& fake, const std::string& payloadHex, DATA_TYPE table = DATA_TYPE::DATA_MDR)
{
	const std::string wanted = toHex(replies::hex(payloadHex));
	for (const auto& sent : sentPayloads(fake, table))
		if (sent == wanted)
			return true;
	return false;
}

inline size_t countSent(FakeBluetoothConnector& fake, const std::string& payloadHex, DATA_TYPE table = DATA_TYPE::DATA_MDR)
{
	const std::string wanted = toHex(replies::hex(payloadHex));
	size_t n = 0;
	for (const auto& sent : sentPayloads(fake, table))
		if (sent == wanted)
			++n;
	return n;
}

// A headset that ACKs every command and answers the requests it has a rule for, in the order a
// real device does: the ACK first, then the RET. Rules live behind a shared_ptr so the script
// stays valid however the test orders its locals.
class DeviceScript
{
public:
	using Hook = std::function<void(FakeBluetoothConnector&, const CommandSerializer::Message&)>;

	DeviceScript() : _rules(std::make_shared<Rules>()) {}

	// When the host sends exactly `requestHex` on `requestTable`, reply with `reply`.
	DeviceScript& on(const std::string& requestHex, const Buffer& reply,
		DATA_TYPE replyTable = DATA_TYPE::DATA_MDR, DATA_TYPE requestTable = DATA_TYPE::DATA_MDR)
	{
		std::lock_guard<std::mutex> guard(_rules->mtx);
		_rules->list.push_back(Rule{ requestTable, toBytes(requestHex), reply, replyTable, nullptr });
		return *this;
	}

	// Run arbitrary device behaviour when that request arrives (after its ACK).
	DeviceScript& onRequest(const std::string& requestHex, Hook hook, DATA_TYPE requestTable = DATA_TYPE::DATA_MDR)
	{
		std::lock_guard<std::mutex> guard(_rules->mtx);
		_rules->list.push_back(Rule{ requestTable, toBytes(requestHex), {}, DATA_TYPE::DATA_MDR, std::move(hook) });
		return *this;
	}

	// Forget the rules for one request (e.g. the device stops answering it).
	void remove(const std::string& requestHex)
	{
		const Buffer bytes = toBytes(requestHex);
		std::lock_guard<std::mutex> guard(_rules->mtx);
		auto& list = _rules->list;
		list.erase(std::remove_if(list.begin(), list.end(), [&](const Rule& r) { return r.request == bytes; }), list.end());
	}

	void installOn(FakeBluetoothConnector& fake)
	{
		auto rules = _rules;
		fake.setScript([rules](FakeBluetoothConnector& device, const CommandSerializer::Message& msg) {
			if (msg.dataType == DATA_TYPE::ACK)
				return;
			device.enqueueAck(msg.seqNumber);
			std::vector<Rule> matched;
			{
				std::lock_guard<std::mutex> guard(rules->mtx);
				for (const auto& rule : rules->list)
					if (rule.requestTable == msg.dataType && rule.request == msg.messageBytes)
						matched.push_back(rule);
			}
			for (const auto& rule : matched)
			{
				if (rule.hook)
					rule.hook(device, msg);
				else
					device.enqueueData(rule.reply, rule.replyTable);
			}
		});
	}

private:
	static Buffer toBytes(const std::string& text);

	struct Rule
	{
		DATA_TYPE requestTable;
		Buffer request;
		Buffer reply;
		DATA_TYPE replyTable;
		Hook hook;
	};
	struct Rules
	{
		std::mutex mtx;
		std::vector<Rule> list;
	};
	std::shared_ptr<Rules> _rules;
};

inline Buffer DeviceScript::toBytes(const std::string& text) { return replies::hex(text); }

// A headset with multipoint mode in one general-setting slot. It advertises that slot, keeps the
// value and answers reads. A write is applied at once, ignored, or held while the headset asks for
// confirmation with a fixed-message alert (after the write's ACK, or before it). An accepted change
// is then applied and announced with D9, or applied by dropping the link. Every command is ACKed.
class MultipointHeadset
{
public:
	enum class OnWrite { Apply, Ask, Ignore };

	struct State
	{
		std::mutex mtx;
		unsigned char slot = 0xD3;
		int value = 0;
		OnWrite onWrite = OnWrite::Ask;
		unsigned char actionType = v1::ALERT_ACTION_POSITIVE_NEGATIVE;
		bool alertBeforeAck = false;
		bool dropOnAccept = false;
		int held = -1;   // the value awaiting confirmation
	};
	std::shared_ptr<State> s = std::make_shared<State>();

	void installOn(FakeBluetoothConnector& fake)
	{
		auto st = s;
		fake.setScript([st](FakeBluetoothConnector& device, const CommandSerializer::Message& msg) {
			if (msg.dataType == DATA_TYPE::ACK)
				return;
			const std::string req = toHex(msg.messageBytes);
			std::vector<Buffer> out;
			bool drop = false;
			bool before = false;
			{
				std::lock_guard<std::mutex> guard(st->mtx);
				const char slot = static_cast<char>(st->slot);
				const std::string sl = toHex(Buffer{ slot });
				auto valueFrame = [&](unsigned char cmd) {
					return Buffer{ static_cast<char>(cmd), slot, 0x01, static_cast<char>(st->value) };
				};
				if (msg.dataType != DATA_TYPE::DATA_MDR)
				{
					// Table 2 (paired devices) is ACKed and otherwise ignored.
				}
				else if (req == "06 01")
					out.push_back(Buffer{ 0x07, 0x01, 0x01, slot });
				else if (req == "D0 " + sl + " 01")
					out.push_back(replies::generalSettingCapability(st->slot, v1::GS_TITLE_MULTIPOINT));
				else if (req == "D2 " + sl)
					out.push_back(Buffer{ static_cast<char>(0xD3), slot, 0x00 });
				else if (req == "D6 " + sl)
					out.push_back(valueFrame(0xD7));
				else if (req == "D8 " + sl + " 01 00" || req == "D8 " + sl + " 01 01")
				{
					const int v = req.back() == '1' ? 1 : 0;
					if (st->onWrite == OnWrite::Apply)
					{
						st->value = v;
						out.push_back(valueFrame(0xD9));
					}
					else if (st->onWrite == OnWrite::Ask)
					{
						st->held = v;
						out.push_back(Buffer{ static_cast<char>(0x99), 0x01,
							static_cast<char>(v ? v1::ALERT_MSG_MULTIPOINT_TO_ON : v1::ALERT_MSG_MULTIPOINT_TO_OFF),
							static_cast<char>(st->actionType) });
						before = st->alertBeforeAck;
					}
				}
				else if (req.rfind("98 01 ", 0) == 0)
				{
					if (req.back() == '1' && st->held >= 0)
					{
						st->value = st->held;
						if (st->dropOnAccept)
							drop = true;
						else
							out.push_back(valueFrame(0xD9));
					}
					st->held = -1;
				}
			}
			if (before)
				for (const auto& frame : out)
					device.enqueueData(frame);
			device.enqueueAck(msg.seqNumber);
			if (!before)
				for (const auto& frame : out)
					device.enqueueData(frame);
			if (drop)
				device.closeRemote();
		});
	}

	int value()
	{
		std::lock_guard<std::mutex> guard(s->mtx);
		return s->value;
	}
};

// Wait until the reader has read and acknowledged every reply the device has sent so far.
inline bool settle(EngineFixture& f)
{
	return testing::waitFor([&] { return f.fake().quiescent(); });
}

// Deliver a reply and wait until the reader has consumed it. A frame that is (correctly)
// ignored changes no state, so "the ACK for it went out" is the only observable sign that the
// engine has finished with it. Earlier replies are waited out first: one the reader has applied
// but not yet acknowledged would otherwise have its ACK mistaken for this frame's.
inline bool deliver(EngineFixture& f, const Buffer& payload, DATA_TYPE table = DATA_TYPE::DATA_MDR)
{
	if (!settle(f))
		return false;
	const size_t acksBefore = f.fake().hostAcks();
	f.fake().enqueueData(payload, table);
	return testing::waitFor([&] { return f.fake().hostAcks() > acksBefore; });
}
