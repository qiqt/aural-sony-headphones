#pragma once

#include "BluetoothWrapper.h"
#include "Headphones.h"

#include <atomic>
#include <thread>

// Owns the single reader thread. It continuously reads framed messages and dispatches them:
//   - ACK frames release the sender that is waiting in BluetoothWrapper::sendCommand
//   - DATA frames update Headphones state and are acknowledged back
//
// The thread is stopped by closing the socket (BluetoothWrapper::disconnect), which unblocks
// the pending recv; stop()/the destructor then join it. (PR #103 leaked the thread into a
// std::async future whose destructor joined an infinite loop and hung connect - we don't.)
class Listener
{
public:
	Listener(Headphones& headphones, BluetoothWrapper& bt);
	~Listener();

	Listener(const Listener&) = delete;
	Listener& operator=(const Listener&) = delete;

	void start();
	void stop();

private:
	void _run();
	void _handleMessage(const CommandSerializer::Message& msg);

	Headphones& _headphones;
	BluetoothWrapper& _bt;
	std::atomic<bool> _running{ false };
	std::thread _thread;
};
