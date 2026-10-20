#include "Listener.h"

namespace {
	// A transport that keeps failing without ever reporting itself disconnected would spin
	// this loop forever. Give up after this many consecutive failures instead.
	constexpr int MAX_CONSECUTIVE_READ_ERRORS = 16;
}

Listener::Listener(Headphones& headphones, BluetoothWrapper& bt) :
	_headphones(headphones),
	_bt(bt)
{
}

Listener::~Listener()
{
	this->stop();
}

void Listener::setDisconnectHandler(std::function<void()> handler)
{
	this->_onDisconnected = std::move(handler);
}

void Listener::start()
{
	if (this->_running.exchange(true))
		return; // already running
	this->_linkLost.store(false);
	this->_thread = std::thread(&Listener::_run, this);
}

void Listener::requestStop()
{
	this->_running = false;
}

void Listener::stop()
{
	this->_running = false;
	// The caller is expected to have closed the socket (disconnect) so the recv below unblocks.
	if (this->_thread.joinable())
		this->_thread.join();
}

void Listener::_run()
{
	int consecutiveErrors = 0;

	while (this->_running)
	{
		try
		{
			auto msg = this->_bt.readMessage();
			consecutiveErrors = 0;
			this->_handleMessage(msg);
		}
		catch (const ConnectionClosedException&)
		{
			// The link is gone. Nothing to retry.
			this->_endSession();
			return;
		}
		catch (const std::exception&)
		{
			// A single corrupt frame is transient: keep listening while the transport still
			// says it is connected, but never loop indefinitely on a failing read.
			if (!this->_bt.isConnected() || ++consecutiveErrors >= MAX_CONSECUTIVE_READ_ERRORS)
			{
				this->_endSession();
				return;
			}
		}
	}
}

void Listener::_endSession()
{
	// If stop() already cleared _running the read failed because *we* closed the socket, so
	// the owner already knows: mark the link lost but don't report it back.
	const bool wasRunning = this->_running.exchange(false);

	if (this->_linkLost.exchange(true))
		return; // already reported

	// Release anything blocked waiting for an ACK that will never come.
	this->_bt.notifyLinkLost();

	// Runs on the reader thread: the handler must not join this Listener.
	if (wasRunning && this->_onDisconnected)
		this->_onDisconnected();
}

void Listener::_handleMessage(const CommandSerializer::Message& msg)
{
	switch (msg.dataType)
	{
	case DATA_TYPE::ACK:
		this->_bt.postAck(msg.seqNumber);
		break;

	case DATA_TYPE::DATA_MDR:
	case DATA_TYPE::DATA_MDR_NO2:
		this->_headphones.setStateFromReply(msg);
		this->_bt.sendAck(msg.seqNumber);
		break;

	default:
		break;
	}
}
