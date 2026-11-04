#pragma once

#include "BluetoothWrapper.h"
#include "Headphones.h"

#include <atomic>
#include <functional>
#include <thread>

// Owns the single reader thread. It continuously reads framed messages and dispatches them:
//   - ACK frames release the sender that is waiting in BluetoothWrapper::sendCommand
//   - DATA frames update Headphones state and are acknowledged back
//
// The thread is stopped by closing the socket (BluetoothWrapper::disconnect), which unblocks
// the pending recv; stop()/the destructor then join it.
//
// When the link dies on its own (the headphones powered off, moved out of range, or the
// socket failed) the reader stops and calls the disconnect handler exactly once. That handler
// runs ON THE READER THREAD, so it must be cheap and must not join this Listener - set a flag
// and let the owner tear the session down.
class Listener
{
public:
	Listener(Headphones& headphones, BluetoothWrapper& bt);
	~Listener();

	Listener(const Listener&) = delete;
	Listener& operator=(const Listener&) = delete;

	// Must be set before start().
	void setDisconnectHandler(std::function<void()> handler);

	void start();

	// Announce that the coming read failure is our own doing, so the disconnect handler is
	// not fired for a shutdown the owner initiated. Does not block.
	// Order for a deliberate teardown: requestStop() -> close the socket -> stop().
	void requestStop();

	// Joins the reader thread. The socket must already be closed (or about to be), otherwise
	// the thread is still blocked in recv and this waits for it.
	void stop();

	// True once the reader has decided the link is gone.
	bool linkLost() const { return _linkLost.load(); }

private:
	void _run();
	void _handleMessage(const CommandSerializer::Message& msg);
	void _endSession();

	Headphones& _headphones;
	BluetoothWrapper& _bt;
	std::atomic<bool> _running{ false };
	std::atomic<bool> _linkLost{ false };
	std::function<void()> _onDisconnected;
	std::thread _thread;
};
