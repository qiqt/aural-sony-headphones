#include "BluetoothWrapper.h"
#include "ProtocolTrace.h"

#include <algorithm>
#include <chrono>

// Default time a sender waits for the headphones to ACK a command before giving up.
// Without this a missing ACK would hang the sending thread forever.
constexpr auto DEFAULT_ACK_TIMEOUT = std::chrono::milliseconds(2000);

BluetoothWrapper::BluetoothWrapper(std::unique_ptr<IBluetoothConnector> connector)
    : _connector(std::move(connector)), _ackTimeout(DEFAULT_ACK_TIMEOUT)
{
    if (!_connector) throw std::invalid_argument("Bluetooth connector is required");
}

void BluetoothWrapper::setAckTimeout(std::chrono::milliseconds timeout) noexcept
{
	std::lock_guard guard(this->_ackMtx);
	this->_ackTimeout = timeout;
}

std::chrono::milliseconds BluetoothWrapper::ackTimeout() const noexcept
{
	return this->_ackTimeout;
}

int BluetoothWrapper::sendCommand(const std::vector<char>& bytes, DATA_TYPE dataType)
{
	// Serialize whole commands so two senders can't share a seqNumber. The Listener
	// never takes _commandMtx, so it can still ACK incoming frames while we wait below.
	std::lock_guard commandGuard(this->_commandMtx);

	const bool expectsAck = (dataType != DATA_TYPE::ACK);
	const unsigned long long session = this->_session.load();
	unsigned long long commandId = 0;

	// A previous command timed out, so an incoming ACK can no longer be attributed to
	// anything (see the header). Refuse to put another frame on a link where "acknowledged"
	// has stopped meaning anything, and ask the caller to tear the session down.
	// ACKs we send for received frames are unaffected: they expect no reply.
	if (expectsAck && this->_desynchronized.load())
	{
		throw RecoverableException(
			"The link is out of sync after an unacknowledged command; reconnect to continue", true);
	}

	if (expectsAck)
	{
		std::lock_guard ackGuard(this->_ackMtx);
		commandId = this->_nextCommandId++;
		this->_expectedAckSeq = this->_seqNumber.load() ^ 0x01;
		// From here until the wait ends, an incoming ACK belongs to this command.
		this->_pendingCommandId = commandId;
	}

	int bytesSent;
	try
	{
		std::lock_guard sendGuard(this->_sendMtx);
		auto data = CommandSerializer::packageDataForBt(bytes, dataType, this->_seqNumber.load());
		ProtocolTrace::frame("TX", dataType, this->_seqNumber.load(), bytes);
		bytesSent = this->_writeFrame(data);
	}
	catch (...)
	{
		if (expectsAck)
		{
			std::lock_guard ackGuard(this->_ackMtx);
			if (this->_pendingCommandId == commandId)
				this->_pendingCommandId = 0;
		}
		throw;
	}

	if (expectsAck)
		this->_waitForAck(commandId, session);

	return bytesSent;
}

void BluetoothWrapper::sendAck(unsigned char seqNumber)
{
	std::lock_guard sendGuard(this->_sendMtx);
	auto data = CommandSerializer::packageDataForBt({}, DATA_TYPE::ACK, seqNumber ^ 0x01);
	ProtocolTrace::frame("TX", DATA_TYPE::ACK, seqNumber ^ 0x01, {});
	this->_writeFrame(data);
}

int BluetoothWrapper::_writeFrame(Buffer& frame)
{
	size_t offset = 0;
	try
	{
		while (offset < frame.size())
		{
			const auto remaining = frame.size() - offset;
			const int sent = _connector->send(frame.data() + offset, remaining);
			if (sent <= 0 || static_cast<size_t>(sent) > remaining)
				throw ConnectionClosedException("Transport did not complete the frame write");
			offset += static_cast<size_t>(sent);
		}
	}
	catch (...)
	{
		// A partial frame cannot be retried as a new command on the same stream.
		disconnect();
		throw;
	}
	return static_cast<int>(offset);
}

void BluetoothWrapper::_waitForAck(unsigned long long commandId, unsigned long long session)
{
	std::unique_lock<std::mutex> guard(this->_ackMtx);

	const bool gotAck = this->_ackCv.wait_for(guard, this->_ackTimeout, [this, commandId, session]() {
		return this->_ackedCommandId == commandId
			|| this->_aborted.load()
			|| this->_session.load() != session;
	});

	const bool acked = (this->_ackedCommandId == commandId);
	// Nothing is in flight any more: a later ACK for this command must not be banked for
	// whichever command runs next.
	if (this->_pendingCommandId == commandId)
		this->_pendingCommandId = 0;

	if (acked)
		return;

	if (this->_session.load() != session || this->_aborted.load())
	{
		ProtocolTrace::line("!! the link went away while waiting for an ACK");
		throw RecoverableException("Disconnected while waiting for an acknowledgement", true);
	}

	// wait_for returning true without a matching ack can only mean a spurious wakeup path;
	// treat it the same as a timeout.
	(void)gotAck;

	// The command may still be in flight, and its ACK - if it ever arrives - is
	// indistinguishable from the next command's. Latch the ambiguity so no later command can
	// be released by it, and report the session as unusable rather than merely "this one
	// command failed".
	this->_desynchronized.store(true);
	ProtocolTrace::line("!! no ACK within " + std::to_string(this->_ackTimeout.count()) + " ms: the session ends");
	throw RecoverableException(
		"Timed out waiting for the headphones to acknowledge a command", true);
}

void BluetoothWrapper::postAck(unsigned char ackSeqNumber)
{
	{
		std::lock_guard guard(this->_ackMtx);
		if (this->_pendingCommandId == 0 || this->_desynchronized.load()
			|| ackSeqNumber != this->_expectedAckSeq)
		{
			// Either nobody is waiting (the command timed out, or the device sent an extra
			// ACK), or a previous timeout means this ACK could belong to either command and
			// we cannot tell which. Dropping it is the point: banking it would let it
			// "acknowledge" whichever command runs next.
			this->_droppedAcks.fetch_add(1);
			return;
		}
		this->_seqNumber.store(ackSeqNumber);
		this->_ackedCommandId = this->_pendingCommandId;
		this->_pendingCommandId = 0;
	}
	this->_ackCv.notify_all();
}

bool BluetoothWrapper::isConnected() noexcept
{
	return this->_connector->isConnected();
}

void BluetoothWrapper::connect(const std::string& addr, SonyProtocol protocol)
{
	std::lock_guard commandGuard(this->_commandMtx);
	this->_protocol = protocol;
    this->_protocolDiscovered = false;
	this->_seqNumber.store(0);
	this->_aborted.store(false);
	// A fresh link is synchronized again: this is the only thing that clears the latch.
	this->_desynchronized.store(false);
	{
		std::lock_guard ackGuard(this->_ackMtx);
		this->_pendingCommandId = 0;
		this->_ackedCommandId = 0;
		// New session: anything still waiting on the old one must not be satisfied by it.
		this->_session.fetch_add(1);
	}
	this->_ackCv.notify_all();
	this->_readBuffer.clear();
    // Discovery may block. A disconnect during it must cancel this attempt rather
    // than let the returning discovery result open a fresh socket after teardown.
    const auto discovered = this->_connector->discoverProtocol(addr);
    if (this->_aborted.load())
        throw RecoverableException("Connection cancelled during service discovery", true);
    if (discovered)
    {
        this->_protocol.store(*discovered);
        this->_protocolDiscovered.store(true);
    }
	ProtocolTrace::line("CONNECT " + addr + (this->_protocol.load() == SonyProtocol::V2 ? " (v2)" : " (v1)") +
        (this->_protocolDiscovered.load() ? " service-discovered" : " fallback: service discovery inconclusive"));
	try
	{
		this->_connector->connect(addr, this->_protocol.load());
	}
	catch (const std::exception& exc)
	{
		ProtocolTrace::line(std::string("!! connect failed: ") + exc.what());
		throw;
	}
	ProtocolTrace::line("connected");
}

void BluetoothWrapper::disconnect() noexcept
{
	if (ProtocolTrace::enabled())
		ProtocolTrace::line("DISCONNECT (closed by Aural)");
	// Release any sender currently blocked in _waitForAck so teardown doesn't wait out the timeout.
	this->notifyLinkLost();
	this->_seqNumber.store(0);
	// Closing the socket unblocks the Listener's pending recv so it can exit.
	this->_connector->disconnect();
}

void BluetoothWrapper::notifyLinkLost() noexcept
{
	this->_aborted.store(true);
	{
		std::lock_guard guard(this->_ackMtx);
		this->_pendingCommandId = 0;
	}
	this->_ackCv.notify_all();
}

std::vector<BluetoothDevice> BluetoothWrapper::getConnectedDevices()
{
	return this->_connector->getConnectedDevices();
}

std::vector<BluetoothDevice> BluetoothWrapper::getPairedDevices()
{
	return this->_connector->getPairedDevices();
}

int BluetoothWrapper::getDeviceBatteryLevel(const std::string& deviceName)
{
	return this->_connector->getDeviceBatteryLevel(deviceName);
}

// Pull one complete frame (the bytes between START_MARKER and END_MARKER) out of the
// rolling read buffer, recv'ing more from the socket as needed. RFCOMM is a byte stream,
// so a single recv may contain part of a frame, a whole frame, or several frames; the
// leftover stays in _readBuffer for the next call. Only the Listener thread calls this.
CommandSerializer::Message BluetoothWrapper::_readFrame()
{
	char buf[MAX_BLUETOOTH_MESSAGE_SIZE] = { 0 };

	for (;;)
	{
		auto start = std::find(this->_readBuffer.begin(), this->_readBuffer.end(), START_MARKER);
		// Bytes before a frame cannot become part of a later frame.
		this->_readBuffer.erase(this->_readBuffer.begin(), start);
		start = this->_readBuffer.begin();
		if (start != this->_readBuffer.end())
		{
			auto end = std::find(start + 1, this->_readBuffer.end(), END_MARKER);
			auto restart = std::find(start + 1, end, START_MARKER);
			if (restart != end)
			{
				this->_readBuffer.erase(start, restart);
				continue;
			}
			if (end != this->_readBuffer.end())
			{
				if (static_cast<size_t>(end - start + 1) > MAX_BLUETOOTH_MESSAGE_SIZE)
				{
					this->_readBuffer.erase(start, end + 1);
					throw RecoverableException("Received Bluetooth frame is too large");
				}
				Buffer frame(start + 1, end);
				this->_readBuffer.erase(this->_readBuffer.begin(), end + 1);
				try
				{
					auto msg = CommandSerializer::unpackBtMessage(frame);
					ProtocolTrace::frame("RX", msg.dataType, msg.seqNumber, msg.messageBytes);
					return msg;
				}
				catch (const std::exception& exc)
				{
					// Raw (still escaped) bytes between the markers: what arrived, not what we hoped.
					if (ProtocolTrace::enabled())
						ProtocolTrace::line("RX unreadable frame  " + ProtocolTrace::hex(frame) + "  (" + exc.what() + ")");
					throw;
				}
			}
			if (this->_readBuffer.size() >= MAX_BLUETOOTH_MESSAGE_SIZE)
			{
				this->_readBuffer.clear();
				throw RecoverableException("Received Bluetooth frame has no end marker");
			}
		}

		auto numRecvd = this->_connector->recv(buf, sizeof(buf));
		if (numRecvd <= 0)
		{
			// The far end closed the RFCOMM channel. Wake any waiting sender: nothing else
			// is going to arrive on this link.
			ProtocolTrace::line("!! the headphones closed the link");
			this->notifyLinkLost();
			throw ConnectionClosedException("Connection closed by the headphones");
		}
		this->_readBuffer.insert(this->_readBuffer.end(), buf, buf + numRecvd);
	}
}

CommandSerializer::Message BluetoothWrapper::readMessage()
{
	try
	{
		return this->_readFrame();
	}
	catch (...)
	{
		// A dead transport releases waiting senders immediately. A corrupt frame does not:
		// the connector is still connected, so only this frame is lost.
		if (!this->_connector->isConnected())
			this->notifyLinkLost();
		throw;
	}
}
