#include "Listener.h"

Listener::Listener(Headphones& headphones, BluetoothWrapper& bt) :
	_headphones(headphones),
	_bt(bt)
{
}

Listener::~Listener()
{
	this->stop();
}

void Listener::start()
{
	if (this->_running.exchange(true))
		return; // already running
	this->_thread = std::thread(&Listener::_run, this);
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
	while (this->_running)
	{
		try
		{
			auto msg = this->_bt.readMessage();
			this->_handleMessage(msg);
		}
		catch (const std::exception&)
		{
			// A closed connection means we're done. Anything else (e.g. a single corrupt
			// frame) is transient: keep listening as long as we're still connected.
			if (!this->_bt.isConnected())
				break;
		}
	}
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
