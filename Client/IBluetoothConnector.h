#pragma once

#include "Constants.h"
#include "Exceptions.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

inline constexpr auto NO_BLUETOOTH_DEVICES_ERROR = "No Bluetooth adapter found. Check that Bluetooth is enabled.";

struct BluetoothDevice
{
    std::string name; // UTF-8 display name
    std::string mac;
    bool connected = true;
};

// One control connection. Send and receive may run concurrently. Disconnect
// must interrupt pending I/O, and connection state must be safe to read from
// another thread. A closed or failed read clears that state before returning.
class IBluetoothConnector
{
public:
    IBluetoothConnector() = default;
    virtual ~IBluetoothConnector() = default;
    IBluetoothConnector(const IBluetoothConnector&) = delete;
    IBluetoothConnector& operator=(const IBluetoothConnector&) = delete;

    virtual void connect(const std::string& address, SonyProtocol protocol) = 0;
    virtual void disconnect() noexcept = 0;
    virtual bool isConnected() noexcept = 0;
    virtual int send(char* bytes, std::size_t size) = 0;
    virtual int recv(char* bytes, std::size_t size) = 0;
    virtual std::vector<BluetoothDevice> getConnectedDevices() = 0;

    // Service lookup is read-only. An empty result is inconclusive, so the
    // caller may use its chosen fallback. Prefer V2 when both are available.
    virtual std::optional<SonyProtocol> discoverProtocol(const std::string&) { return std::nullopt; }
    virtual std::vector<BluetoothDevice> getPairedDevices() { return getConnectedDevices(); }
    virtual int getDeviceBatteryLevel(const std::string&) { return -1; }
};
