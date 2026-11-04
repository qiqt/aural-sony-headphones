#pragma once

#include "IBluetoothConnector.h"
#include "CommandSerializer.h"
#include "Constants.h"
#include <chrono>
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
//
// ACK correlation: commands are serialized by _commandMtx, so at most one is in
// flight. Each one gets an id, and postAck() only releases the command that is
// actually in flight. An ACK that arrives after its command gave up is dropped
// instead of being banked for the next command.
// The ACK must carry the inverse of that command's sequence; a duplicate of the
// preceding successful exchange cannot release the next command or change its sequence.
//
// That alone is not enough. An ACK frame carries only a 1-bit seq that toggles on
// each *successful* exchange, so after a timeout the sequence does not advance and
// the next command goes out with the same seq. Nothing in the frame distinguishes
// "the ACK for the command that timed out" from "the ACK for the command running
// now" - a late ACK arriving while a later command is pending would release the
// wrong one. Since the protocol offers no resynchronisation primitive, an ACK
// timeout makes the link ambiguous and ends it: isDesynchronized() latches, every
// further sendCommand() fails with shouldDisconnect set, and only connect() clears
// it. Losing a session beats silently reporting a command as delivered.
//
// Sessions: connect() bumps a session counter. A sender that was waiting when
// the link went down fails instead of being satisfied by the new connection.
class BluetoothWrapper
{
public:
	BluetoothWrapper(std::unique_ptr<IBluetoothConnector> connector);

	BluetoothWrapper(const BluetoothWrapper&) = delete;
	BluetoothWrapper& operator=(const BluetoothWrapper&) = delete;


	// Send a command and (unless it is itself an ACK) block until the headphones
	// acknowledge it. Throws RecoverableException on timeout, and on every later call
	// while the link is desynchronized (see isDesynchronized).
	int sendCommand(const std::vector<char>& bytes, DATA_TYPE dataType = DATA_TYPE::DATA_MDR);

	// Acknowledge a received DATA frame. The ACK seq is the inverse of the received seq.
	// Brief, lock-light: safe to call from the Listener while a sender waits for its own ACK.
	void sendAck(unsigned char seqNumber);

	// Read one full framed + unpacked message (blocks). Intended for the Listener thread only.
	CommandSerializer::Message readMessage();

	// Called by the Listener when an ACK frame arrives: advances the seq and releases the
	// command that is currently in flight (if any).
	void postAck(unsigned char ackSeqNumber);

	bool isConnected() noexcept;
	// Try to connect to the headphones
	void connect(const std::string& addr, SonyProtocol protocol);
	void disconnect() noexcept;

	// Release every waiting sender because the link is gone. Idempotent; safe from the
	// reader thread. Cleared by the next connect().
	void notifyLinkLost() noexcept;

	// Increments on every connect(). Async results can carry this to detect a stale session.
	unsigned long long sessionId() const noexcept { return _session.load(); }

	// How many ACK frames were not matched to a command: they arrived with nothing in
	// flight, or after an ACK timeout left the link ambiguous. Either way they are counted
	// here instead of releasing an unrelated later command.
	unsigned long long droppedAckCount() const noexcept { return _droppedAcks.load(); }

	// True once a command timed out on this session. From that point no ACK can be
	// attributed to anything, so the link is unusable and every sendCommand() fails
	// immediately with shouldDisconnect set. Cleared only by connect().
	bool isDesynchronized() const noexcept { return _desynchronized.load(); }

	// How long a sender waits for an ACK. Default 2s; tests shorten it so a missing ACK is
	// cheap to assert on.
	void setAckTimeout(std::chrono::milliseconds timeout) noexcept;
	std::chrono::milliseconds ackTimeout() const noexcept;

	std::vector<BluetoothDevice> getConnectedDevices();
	// Connected and paired devices, each flagged (see IBluetoothConnector).
	std::vector<BluetoothDevice> getPairedDevices();

	// OS-reported battery for a connected device (Windows: PnP battery property), or -1 if unavailable.
	int getDeviceBatteryLevel(const std::string& deviceName);

	SonyProtocol getProtocol() const noexcept { return _protocol.load(); }
    // False means the supplied fallback was used because service discovery was inconclusive.
    bool protocolDiscovered() const noexcept { return _protocolDiscovered.load(); }

private:
	int _writeFrame(Buffer& frame); // Caller holds _sendMtx for the whole frame.
	void _waitForAck(unsigned long long commandId, unsigned long long session);
	CommandSerializer::Message _readFrame();

	std::unique_ptr<IBluetoothConnector> _connector;

	std::mutex _commandMtx;          // serializes a whole command (send + wait-for-ack); NOT taken by the Listener
	std::mutex _sendMtx;             // guards the raw socket write; shared by sendCommand and sendAck
	std::mutex _ackMtx;              // guards the command-in-flight bookkeeping below
	std::condition_variable _ackCv;
	unsigned long long _nextCommandId = 1;   // guarded by _ackMtx
	unsigned long long _pendingCommandId = 0; // command currently waiting for an ACK, 0 = none
	unsigned long long _ackedCommandId = 0;   // last command an ACK was matched to
	unsigned char _expectedAckSeq = 0;        // inverse of pending command's seq, guarded by _ackMtx
	std::atomic<unsigned long long> _droppedAcks{ 0 };
	std::atomic<bool> _aborted{ false }; // set on disconnect/link loss to release a waiting sender at once
	std::atomic<bool> _desynchronized{ false }; // set on ACK timeout: correlation is no longer possible
	std::atomic<unsigned long long> _session{ 0 };
	std::chrono::milliseconds _ackTimeout{ 2000 };

	/*
		seqNumber logic (matches the legacy v1 protocol, proven on XM4):
		- the next outgoing command uses the seqNumber the headphones put in their last ACK
		- our ACK for a received frame uses the inverse of that frame's seqNumber
	*/
	std::atomic<unsigned char> _seqNumber{ 0 };

	Buffer _readBuffer;              // leftover bytes between frames (Listener thread only, no lock needed)
	std::atomic<SonyProtocol> _protocol{ SonyProtocol::V1 };
    std::atomic<bool> _protocolDiscovered{ false };
};
