#pragma once

#include "IBluetoothConnector.h"
#include <memory>
#include <mutex>

class SocketChannel;

class WindowsBluetoothConnector final : public IBluetoothConnector
{
public:
    WindowsBluetoothConnector();
    ~WindowsBluetoothConnector() override;

    void connect(const std::string& address, SonyProtocol protocol) override;
    std::optional<SonyProtocol> discoverProtocol(const std::string& address) override;
    int send(char* bytes, size_t size) override;
    int recv(char* bytes, size_t size) override;
    void disconnect() noexcept override;
    bool isConnected() noexcept override;
    std::vector<BluetoothDevice> getConnectedDevices() override;
    std::vector<BluetoothDevice> getPairedDevices() override;
    int getDeviceBatteryLevel(const std::string& name) override;

private:
    std::shared_ptr<SocketChannel> channel();
    std::vector<BluetoothDevice> enumerate(bool paired);
    std::mutex _channelMutex;
    std::shared_ptr<SocketChannel> _channel;
    unsigned long long _generation = 0;
};
