#pragma once

// A scripted, in-memory stand-in for a real RFCOMM link.
//
// It is the same seam the Windows/Linux/macOS connectors plug into (IBluetoothConnector), so
// BluetoothWrapper, Listener and Headphones run exactly as they do against hardware - framing,
// escaping, checksums, the ACK handshake and the reader thread all included.
//
// Everything a test needs to control is explicit and event-driven:
//   * auto-ACK on/off, or a custom script per received frame
//   * enqueue an ACK or a DATA reply at the exact moment the test chooses (delayed replies)
//   * never reply at all (ACK timeout)
//   * closeRemote() - the headphones close the channel (recv returns 0)
//   * failSends() - the socket starts refusing writes
//   * setChunkSize() - deliver bytes in small pieces so frames arrive fragmented
//   * waitForSentFrames(n) - block until the code under test has actually sent n frames
//
// There is no "sleep and hope": tests wait on conditions, so they neither flake on a slow
// machine nor pass by accident on a fast one.

#include "CommandSerializer.h"
#include "Constants.h"
#include "IBluetoothConnector.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class FakeBluetoothConnector final : public IBluetoothConnector
{
public:
	// Called for every frame the code under test sends. Default behaviour is auto-ACK.
	using Script = std::function<void(FakeBluetoothConnector&, const CommandSerializer::Message&)>;

	FakeBluetoothConnector() = default;

	// ---------------- IBluetoothConnector ----------------

	int send(char* buf, size_t length) override
	{
		std::vector<CommandSerializer::Message> parsed;
		{
			std::unique_lock<std::mutex> guard(_mtx);
			if (!_connected)
				throw ConnectionClosedException("fake: send on a closed socket");
			if (_sendsFail)
			{
				_connected = false;
				_cv.notify_all();
				throw ConnectionClosedException("fake: send failed");
			}
			_sentBytes.insert(_sentBytes.end(), buf, buf + length);
			parsed = _drainCompleteFramesLocked();
			_sent.insert(_sent.end(), parsed.begin(), parsed.end());
			for (const auto& msg : parsed)
				if (msg.dataType == DATA_TYPE::ACK)
					++_hostAcks;
			_cv.notify_all();
		}

		// The device's answer is produced outside the lock so a script may call back into
		// enqueueAck()/enqueueData() freely.
		for (const auto& msg : parsed)
			_respond(msg);

		return static_cast<int>(length);
	}

	int recv(char* buf, size_t length) override
	{
		std::unique_lock<std::mutex> guard(_mtx);
		++_recvCount;
		_cv.wait(guard, [this] {
			return !_incoming.empty() || _remoteClosed || !_connected;
		});

		if (_incoming.empty())
		{
			// Contract (IBluetoothConnector::recv): report the link as down *before*
			// returning 0 or throwing, so the reader can tell "one bad frame" from
			// "the session is over".
			const bool localClose = !_connected;
			_connected = false;
			if (localClose)
				throw ConnectionClosedException("fake: recv on a closed socket");
			return 0; // graceful close by the peer
		}

		const size_t chunk = _chunkSize == 0 ? length : (std::min)(_chunkSize, length);
		const size_t count = (std::min)(chunk, _incoming.size());
		for (size_t i = 0; i < count; ++i)
		{
			buf[i] = _incoming.front();
			_incoming.pop_front();
		}
		return static_cast<int>(count);
	}

	void connect(const std::string& addr, SonyProtocol protocol) override
	{
		std::unique_lock<std::mutex> guard(_mtx);
		++_connectAttempts;
		_connectAborted = false;
		_cv.notify_all();

		// Optional gate: hold the attempt open so a test can act while it is in flight.
		// A disconnect() while we are held models the real thing - closing the socket under
		// a connect makes the connect fail rather than complete late.
		_cv.wait(guard, [this] { return !_connectBlocked || _connectAborted; });
		if (_connectAborted)
			throw RecoverableException("fake: connect aborted by disconnect", true);

		if (_connectFails)
			throw RecoverableException("fake: couldn't connect", true);
		_lastAddr = addr;
		_lastProtocol = protocol;
		++_connectCount;
		_connected = true;
		_remoteClosed = false;
		_sendsFail = false;
		_incoming.clear();
		_sentBytes.clear();
		_dataFramesQueued = 0;
		_hostAcks = 0;
		_cv.notify_all();
	}

	void disconnect() noexcept override
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_connected = false;
		++_disconnectCount;
		// Anything held on a gate is released by the socket going away, exactly as a real
		// blocking call is. Without this a test could deadlock its own teardown.
		_connectAborted = true;
		_batteryBlocked = false;
		_cv.notify_all();
	}

	bool isConnected() noexcept override
	{
		std::lock_guard<std::mutex> guard(_mtx);
		return _connected;
	}

	std::vector<BluetoothDevice> getConnectedDevices() override
	{
		std::lock_guard<std::mutex> guard(_mtx);
		if (_discoveryFails)
			throw RecoverableException(NO_BLUETOOTH_DEVICES_ERROR);
		return _discovered;
	}

	int getDeviceBatteryLevel(const std::string&) override
	{
		std::unique_lock<std::mutex> guard(_mtx);
		++_batteryLookups;
		_cv.notify_all();
		// Same gate idea as connect(): a test can freeze an OS battery lookup in flight and
		// tear the session down underneath it.
		_cv.wait(guard, [this] { return !_batteryBlocked; });
		return _osBattery;
	}

	// ---------------- scripting ----------------

	// Replace the default auto-ACK behaviour. Pass {} to restore it.
	void setScript(Script script)
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_script = std::move(script);
	}

	// When off, nothing answers a command: the sender hits its ACK timeout.
	void setAutoAck(bool on)
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_autoAck = on;
	}

	// The device closes the channel. Bytes already queued are still delivered first.
	void closeRemote()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_remoteClosed = true;
		_cv.notify_all();
	}

	// The socket starts refusing writes (cable pulled, adapter reset, ...).
	void failSends(bool on = true)
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_sendsFail = on;
	}

	void failConnect(bool on = true)
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_connectFails = on;
	}

	void failDiscovery(bool on = true)
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_discoveryFails = on;
	}

	// Deliver at most n bytes per recv() so frames arrive fragmented. 0 = no limit.
	void setChunkSize(size_t n)
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_chunkSize = n;
	}

	// Hold every connect() attempt open until releaseConnect() (or a disconnect()).
	void blockConnect(bool on = true)
	{
		std::lock_guard<std::mutex> guard(_mtx);
		// _connectAborted is only cleared when a fresh attempt starts, never here: releasing the
		// gate must not resurrect an attempt a disconnect already killed.
		_connectBlocked = on;
		_cv.notify_all();
	}

	void releaseConnect() { blockConnect(false); }

	bool waitForConnectAttempt(int n = 1, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
	{
		std::unique_lock<std::mutex> guard(_mtx);
		return _cv.wait_for(guard, timeout, [this, n] { return _connectAttempts >= n; });
	}

	// Hold every OS battery lookup open until releaseBatteryLookup() (or a disconnect()).
	void blockBatteryLookup(bool on = true)
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_batteryBlocked = on;
		_cv.notify_all();
	}

	void releaseBatteryLookup() { blockBatteryLookup(false); }

	bool waitForBatteryLookup(int n = 1, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
	{
		std::unique_lock<std::mutex> guard(_mtx);
		return _cv.wait_for(guard, timeout, [this, n] { return _batteryLookups >= n; });
	}

	void setDiscoveredDevices(std::vector<BluetoothDevice> devices)
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_discovered = std::move(devices);
	}

	void setOsBatteryLevel(int level)
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_osBattery = level;
	}

	// Device -> host: an ACK for a frame that went out with seqNumber `seq`.
	void enqueueAck(unsigned char seq)
	{
		enqueueRaw(CommandSerializer::packageDataForBt({}, DATA_TYPE::ACK, seq ^ 0x01));
	}

	// Device -> host: a DATA reply carrying `payload`.
	void enqueueData(const Buffer& payload, DATA_TYPE dataType = DATA_TYPE::DATA_MDR,
		unsigned char seq = 0)
	{
		// The host's reader acknowledges frames from the two command tables (see quiescent()).
		if (dataType == DATA_TYPE::DATA_MDR || dataType == DATA_TYPE::DATA_MDR_NO2)
		{
			std::lock_guard<std::mutex> guard(_mtx);
			++_dataFramesQueued;
		}
		enqueueRaw(CommandSerializer::packageDataForBt(payload, dataType, seq));
	}

	// Device -> host: exactly these bytes (malformed frames, split frames, noise).
	void enqueueRaw(const Buffer& bytes)
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_incoming.insert(_incoming.end(), bytes.begin(), bytes.end());
		_cv.notify_all();
	}

	// ---------------- observation ----------------

	std::vector<CommandSerializer::Message> sentFrames()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		return _sent;
	}

	size_t sentCount()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		return _sent.size();
	}

	// Frames the host sent that are not ACKs - i.e. actual commands.
	std::vector<CommandSerializer::Message> sentCommands()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		std::vector<CommandSerializer::Message> out;
		for (const auto& msg : _sent)
			if (msg.dataType != DATA_TYPE::ACK)
				out.push_back(msg);
		return out;
	}

	bool waitForSentFrames(size_t n, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
	{
		std::unique_lock<std::mutex> guard(_mtx);
		return _cv.wait_for(guard, timeout, [this, n] { return _sent.size() >= n; });
	}

	bool waitForSentCommands(size_t n, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
	{
		std::unique_lock<std::mutex> guard(_mtx);
		return _cv.wait_for(guard, timeout, [this, n] {
			size_t commands = 0;
			for (const auto& msg : _sent)
				if (msg.dataType != DATA_TYPE::ACK)
					++commands;
			return commands >= n;
		});
	}

	// Block until everything queued for the host has been read out of the socket.
	bool waitForInboxDrained(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
	{
		std::unique_lock<std::mutex> guard(_mtx);
		return _cv.wait_for(guard, timeout, [this] { return _incoming.empty(); });
	}

	// ACK frames the host has sent this connection. Unlike sentFrames(), clearSent() leaves it alone.
	size_t hostAcks()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		return _hostAcks;
	}

	// Every DATA reply queued with enqueueData() has been read and acknowledged. The reader applies
	// a reply before it ACKs it, so the state a reply changed can be visible while the reader is
	// still busy with it: this, not that state, says the reader is done. Frames queued with
	// enqueueRaw() are not counted.
	bool quiescent()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		return _incoming.empty() && _hostAcks >= _dataFramesQueued;
	}

	int connectCount()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		return _connectCount;
	}

	// How many times the reader has called recv(). A reader that spins on a dead link makes
	// this grow without bound, which is exactly the failure SON-8 is about.
	int recvCount()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		return _recvCount;
	}

	int disconnectCount()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		return _disconnectCount;
	}

	std::string lastAddress()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		return _lastAddr;
	}

	SonyProtocol lastProtocol()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		return _lastProtocol;
	}

	void clearSent()
	{
		std::lock_guard<std::mutex> guard(_mtx);
		_sent.clear();
	}

private:
	// Pull every complete <START ... END> frame out of the accumulated host bytes.
	// Called with _mtx held.
	std::vector<CommandSerializer::Message> _drainCompleteFramesLocked()
	{
		std::vector<CommandSerializer::Message> parsed;
		for (;;)
		{
			auto start = std::find(_sentBytes.begin(), _sentBytes.end(), START_MARKER);
			if (start == _sentBytes.end())
				break;
			auto end = std::find(start + 1, _sentBytes.end(), END_MARKER);
			if (end == _sentBytes.end())
				break;

			Buffer frame(start + 1, end);
			_sentBytes.erase(_sentBytes.begin(), end + 1);
			try
			{
				parsed.push_back(CommandSerializer::unpackBtMessage(frame));
			}
			catch (const std::exception&)
			{
				// A frame we can't parse is still a frame the host emitted; ignore it here
				// rather than failing the fake.
			}
		}
		return parsed;
	}

	void _respond(const CommandSerializer::Message& msg)
	{
		Script script;
		bool autoAck;
		{
			std::lock_guard<std::mutex> guard(_mtx);
			script = _script;
			autoAck = _autoAck;
		}

		if (script)
		{
			script(*this, msg);
			return;
		}

		// Default device behaviour: ACK every command, ignore the host's own ACKs.
		if (autoAck && msg.dataType != DATA_TYPE::ACK)
			enqueueAck(msg.seqNumber);
	}

	std::mutex _mtx;
	std::condition_variable _cv;

	bool _connected = false;
	bool _remoteClosed = false;
	bool _sendsFail = false;
	bool _connectFails = false;
	bool _discoveryFails = false;
	bool _autoAck = true;

	bool _connectBlocked = false;
	bool _connectAborted = false;
	bool _batteryBlocked = false;

	size_t _chunkSize = 0;
	int _osBattery = -1;
	int _connectCount = 0;
	int _connectAttempts = 0;   // incremented on entry, before the gate; _connectCount on success
	int _batteryLookups = 0;
	int _disconnectCount = 0;
	int _recvCount = 0;
	size_t _dataFramesQueued = 0;   // enqueueData() frames the host's reader will ACK
	size_t _hostAcks = 0;

	std::string _lastAddr;
	SonyProtocol _lastProtocol = SonyProtocol::V1;

	std::deque<char> _incoming;   // device -> host
	Buffer _sentBytes;            // host -> device, not yet split into frames
	std::vector<CommandSerializer::Message> _sent;
	std::vector<BluetoothDevice> _discovered;
	Script _script;
};
