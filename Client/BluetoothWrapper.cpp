#include "BluetoothWrapper.h"

#include <algorithm>
#include <chrono>

// How long a sender waits for the headphones to ACK a command before giving up.
// Without this a missing ACK would hang the sending thread forever.
constexpr auto ACK_TIMEOUT = std::chrono::seconds(2);

BluetoothWrapper::BluetoothWrapper(std::unique_ptr<IBluetoothConnector> connector)
    : _connector(std::move(connector))
{
    if (!_connector) throw std::invalid_argument("Bluetooth connector is required");
}





int BluetoothWrapper::sendCommand(const std::vector<char>& bytes, DATA_TYPE dataType)
{
	// Serialize whole commands so two senders can't share a seqNumber. The Listener
	// never takes _commandMtx, so it can still ACK incoming frames while we wait below.
	std::lock_guard commandGuard(this->_commandMtx);

	int bytesSent;
	{
		std::lock_guard sendGuard(this->_sendMtx);
		auto data = CommandSerializer::packageDataForBt(bytes, dataType, this->_seqNumber.load());
		bytesSent = this->_connector->send(data.data(), data.size());
	}

	if (dataType != DATA_TYPE::ACK)
		this->_waitForAck();

	return bytesSent;
}

void BluetoothWrapper::sendAck(unsigned char seqNumber)
{
	std::lock_guard sendGuard(this->_sendMtx);
	auto data = CommandSerializer::packageDataForBt({}, DATA_TYPE::ACK, seqNumber ^ 0x01);
	this->_connector->send(data.data(), data.size());
}

void BluetoothWrapper::_waitForAck()
{
	std::unique_lock<std::mutex> guard(this->_ackMtx);
	bool gotAck = this->_ackCv.wait_for(guard, ACK_TIMEOUT,
		[this]() { return this->_ackBuffer > 0 || this->_aborted.load(); });

	if (this->_aborted.load())
		throw RecoverableException("Disconnected while waiting for an acknowledgement", true);
	if (!gotAck)
		throw RecoverableException("Timed out waiting for the headphones to acknowledge a command", false);

	this->_ackBuffer--;
}

void BluetoothWrapper::postAck(unsigned char ackSeqNumber)
{
	this->_seqNumber.store(ackSeqNumber);
	{
		std::lock_guard guard(this->_ackMtx);
		this->_ackBuffer++;
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
	this->_seqNumber.store(0);
	this->_aborted.store(false);
	{
		std::lock_guard ackGuard(this->_ackMtx);
		this->_ackBuffer = 0;
	}
	this->_readBuffer.clear();
	this->_connector->connect(addr, protocol);
}

void BluetoothWrapper::disconnect() noexcept
{
	// Release any sender currently blocked in _waitForAck so teardown doesn't wait out the timeout.
	this->_aborted.store(true);
	this->_ackCv.notify_all();
	this->_seqNumber.store(0);
	// Closing the socket unblocks the Listener's pending recv so it can exit.
	this->_connector->disconnect();
}

std::vector<BluetoothDevice> BluetoothWrapper::getConnectedDevices()
{
	return this->_connector->getConnectedDevices();
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
		if (start != this->_readBuffer.end())
		{
			auto end = std::find(start + 1, this->_readBuffer.end(), END_MARKER);
			if (end != this->_readBuffer.end())
			{
				Buffer frame(start + 1, end);
				this->_readBuffer.erase(this->_readBuffer.begin(), end + 1);
				return CommandSerializer::unpackBtMessage(frame);
			}
		}

		auto numRecvd = this->_connector->recv(buf, sizeof(buf));
		if (numRecvd <= 0)
		{
			throw RecoverableException("Connection closed by the headphones", true);
		}
		this->_readBuffer.insert(this->_readBuffer.end(), buf, buf + numRecvd);
	}
}

CommandSerializer::Message BluetoothWrapper::readMessage()
{
	return this->_readFrame();
}
