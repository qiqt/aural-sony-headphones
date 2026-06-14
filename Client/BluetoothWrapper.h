#pragma once
#include "IBluetoothConnector.h"
#include "CommandSerializer.h"
#include <memory>
#include <mutex>
class BluetoothWrapper
{
public:
    explicit BluetoothWrapper(std::unique_ptr<IBluetoothConnector> connector);
    int sendCommand(const Buffer& bytes);
    CommandSerializer::Message readMessage();
    void sendAck(unsigned char sequence);
    bool isConnected() noexcept;
    void connect(const std::string& address, SonyProtocol protocol);
    void disconnect() noexcept;
    std::vector<BluetoothDevice> getConnectedDevices();
    SonyProtocol getProtocol() const noexcept { return _protocol; }
private:
    CommandSerializer::Message readFrame();
    int writeFrame(Buffer frame);
    std::unique_ptr<IBluetoothConnector> _connector;
    std::mutex _mutex;
    unsigned char _sequence = 0;
    SonyProtocol _protocol = SonyProtocol::V1;
};
