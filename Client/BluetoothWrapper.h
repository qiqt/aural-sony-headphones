#pragma once

#include "IBluetoothConnector.h"
#include "CommandSerializer.h"
#include "Constants.h"
#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <condition_variable>


// Thread-safety: This class is thread-safe.
//
// The protocol is full-duplex: a dedicated Listener thread owns all reads
// (readMessage) while any number of threads may send commands. Each outgoing
// command blocks until the headphones ACK it; the ACK is delivered by the
// Listener via postAck(), not read inline. This decoupling is what makes reads
// (battery, settings) reliable instead of racing the ACK.
class BluetoothWrapper
{
public:
	BluetoothWrapper(std::unique_ptr<IBluetoothConnector> connector);

	BluetoothWrapper(const BluetoothWrapper&) = delete;
	BluetoothWrapper& operator=(const BluetoothWrapper&) = delete;


	// Send a command and (unless it is itself an ACK) block until the headphones
	// acknowledge it. Throws RecoverableException on timeout.
	int sendCommand(const std::vector<char>& bytes, DATA_TYPE dataType = DATA_TYPE::DATA_MDR);

	// Acknowledge a received DATA frame. The ACK seq is the inverse of the received seq.
	// Brief, lock-light: safe to call from the Listener while a sender waits for its own ACK.
	void sendAck(unsigned char seqNumber);

	// Read one full framed + unpacked message (blocks). Intended for the Listener thread only.
	CommandSerializer::Message readMessage();

	// Called by the Listener when an ACK frame arrives: advances the seq and releases a waiting sender.
	void postAck(unsigned char ackSeqNumber);

	bool isConnected() noexcept;
	// Try to connect to the headphones
	void connect(const std::string& addr, SonyProtocol protocol);
	void disconnect() noexcept;

	std::vector<BluetoothDevice> getConnectedDevices();

	// OS-reported battery for a connected device (Windows: PnP battery property), or -1 if unavailable.
	int getDeviceBatteryLevel(const std::string& deviceName);

	SonyProtocol getProtocol() const noexcept { return _protocol; }

private:
	void _waitForAck();
	CommandSerializer::Message _readFrame();

	std::unique_ptr<IBluetoothConnector> _connector;

	std::mutex _commandMtx;          // serializes a whole command (send + wait-for-ack); NOT taken by the Listener
	std::mutex _sendMtx;             // guards the raw socket write; shared by sendCommand and sendAck
	std::mutex _ackMtx;              // guards _ackBuffer
	std::condition_variable _ackCv;
	unsigned int _ackBuffer = 0;     // counts ACKs delivered by the Listener but not yet consumed
	std::atomic<bool> _aborted{ false }; // set on disconnect to release a waiting sender at once

	/*
		seqNumber logic (matches the legacy v1 protocol, proven on XM4):
		- the next outgoing command uses the seqNumber the headphones put in their last ACK
		- our ACK for a received frame uses the inverse of that frame's seqNumber
	*/
	std::atomic<unsigned char> _seqNumber{ 0 };

	Buffer _readBuffer;              // leftover bytes between frames (Listener thread only, no lock needed)
	SonyProtocol _protocol = SonyProtocol::V1;
};
