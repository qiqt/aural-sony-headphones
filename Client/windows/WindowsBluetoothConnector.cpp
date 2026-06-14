#include "SocketChannel.h"
#include "WindowsBluetoothConnector.h"
#include "BluetoothAddress.h"

#include <ws2bth.h>
#include <BluetoothAPIs.h>
#include <rpc.h>
#include <setupapi.h>
#include <devpropdef.h>
#include <algorithm>
#include <utility>

namespace
{
class WinsockRuntime
{
public:
    WinsockRuntime()
    {
        WSADATA data{};
        const auto error = ::WSAStartup(MAKEWORD(2, 2), &data);
        if (error) throw std::runtime_error("Cannot start Windows networking (" + std::to_string(error) + ")");
    }
    ~WinsockRuntime() { ::WSACleanup(); }
};

template<class Handle, auto Release>
class OwnedHandle
{
public:
    explicit OwnedHandle(Handle value) : _value(value) {}
    ~OwnedHandle() { if (_value) Release(_value); }
    OwnedHandle(const OwnedHandle&) = delete;
    OwnedHandle& operator=(const OwnedHandle&) = delete;
    Handle get() const { return _value; }
private:
    Handle _value;
};

std::string utf8(const wchar_t* text)
{
    const int size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("Device name is not valid UTF-16");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), size, nullptr, nullptr) != size)
        throw std::runtime_error("Could not read the device name");
    result.pop_back();
    return result;
}

std::runtime_error windowsError(const char* action, DWORD error)
{
    return std::runtime_error(std::string(action) + " failed (Windows error " + std::to_string(error) + ")");
}

// Windows PnP property identifiers.
const DEVPROPKEY PKEY_DeviceName = {{0xb725f130,0x47ef,0x101a,{0xa5,0xf1,0x02,0x60,0x8c,0x9e,0xeb,0xac}},10};
const DEVPROPKEY PKEY_FriendlyName = {{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};
const DEVPROPKEY PKEY_BluetoothBattery = {{0x104ea319,0x6ee2,0x4701,{0xbd,0x47,0x8d,0xdb,0xf4,0x25,0xbb,0xe5}},2};

bool readStringProperty(HDEVINFO devices, SP_DEVINFO_DATA* device, const DEVPROPKEY& key, std::wstring& value)
{
    DEVPROPTYPE type{};
    DWORD size{};
    ::SetupDiGetDevicePropertyW(devices, device, &key, &type, nullptr, 0, &size, 0);
    if (size < sizeof(wchar_t) || size > 65536 || size % sizeof(wchar_t)) return false;
    std::vector<wchar_t> text(size / sizeof(wchar_t), L'\0');
    if (!::SetupDiGetDevicePropertyW(devices, device, &key, &type,
            reinterpret_cast<BYTE*>(text.data()), size, nullptr, 0) || type != DEVPROP_TYPE_STRING)
        return false;
    if (text.back() != L'\0') return false;
    value.assign(text.data());
    return true;
}
}

WindowsBluetoothConnector::WindowsBluetoothConnector()
{
    static WinsockRuntime runtime;
}

WindowsBluetoothConnector::~WindowsBluetoothConnector() { disconnect(); }

std::shared_ptr<SocketChannel> WindowsBluetoothConnector::channel()
{
    std::lock_guard guard(_channelMutex);
    return _channel;
}

void WindowsBluetoothConnector::connect(const std::string& address, SonyProtocol protocol)
{
    std::shared_ptr<SocketChannel> previous;
    unsigned long long generation;
    {
        std::lock_guard guard(_channelMutex);
        generation = ++_generation;
        previous = std::exchange(_channel, {});
    }
    if (previous) previous->stop();
    SOCKADDR_BTH destination{};
    destination.addressFamily = AF_BTH;
    destination.btAddr = BluetoothAddress::parse(address);
    const auto uuidError = ::UuidFromStringA(reinterpret_cast<RPC_CSTR>(const_cast<char*>(serviceUuidString(protocol))), &destination.serviceClassId);
    if (uuidError != RPC_S_OK) throw windowsError("Read Bluetooth service identifier", uuidError);

    const auto socket = ::socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (socket == INVALID_SOCKET) throw windowsError("Create Bluetooth socket", ::WSAGetLastError());
    auto next = std::make_shared<SocketChannel>(socket);
    const ULONG enabled = TRUE;
    for (const int option : {static_cast<int>(SO_BTH_AUTHENTICATE), static_cast<int>(SO_BTH_ENCRYPT)})
        if (::setsockopt(socket, SOL_RFCOMM, option, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == SOCKET_ERROR)
            throw windowsError("Configure Bluetooth socket", ::WSAGetLastError());
    {
        std::lock_guard guard(_channelMutex);
        if (_generation != generation) throw ConnectionClosedException("Connection was cancelled");
        _channel = next;
    }
    try
    {
        if (::connect(socket, reinterpret_cast<const sockaddr*>(&destination), sizeof(destination)) == SOCKET_ERROR)
            throw RecoverableException("Cannot connect to headphones (Windows error " + std::to_string(::WSAGetLastError()) + ")", true);
        next->connectedSuccessfully();
    }
    catch (...)
    {
        next->stop();
        std::lock_guard guard(_channelMutex);
        if (_channel == next) _channel.reset();
        throw;
    }
}

int WindowsBluetoothConnector::send(char* bytes, size_t size)
{
    auto active = channel();
    if (!active) throw ConnectionClosedException("Headphones are disconnected");
    return active->write(bytes, size);
}

int WindowsBluetoothConnector::recv(char* bytes, size_t size)
{
    auto active = channel();
    if (!active) throw ConnectionClosedException("Headphones are disconnected");
    return active->read(bytes, size);
}

void WindowsBluetoothConnector::disconnect() noexcept
{
    std::shared_ptr<SocketChannel> previous;
    {
        std::lock_guard guard(_channelMutex);
        ++_generation;
        previous = std::exchange(_channel, {});
    }
    if (previous) previous->stop();
}

bool WindowsBluetoothConnector::isConnected() noexcept
{
    auto active = channel();
    return active && active->connected();
}

std::vector<BluetoothDevice> WindowsBluetoothConnector::getConnectedDevices() { return enumerate(false); }
std::vector<BluetoothDevice> WindowsBluetoothConnector::getPairedDevices() { return enumerate(true); }

std::vector<BluetoothDevice> WindowsBluetoothConnector::enumerate(bool paired)
{
    BLUETOOTH_FIND_RADIO_PARAMS parameters{sizeof(parameters)};
    HANDLE rawRadio{};
    OwnedHandle<HBLUETOOTH_RADIO_FIND, BluetoothFindRadioClose> radios(::BluetoothFindFirstRadio(&parameters, &rawRadio));
    if (!radios.get())
    {
        const auto error = ::GetLastError();
        if (error == ERROR_NO_MORE_ITEMS) throw RecoverableException(NO_BLUETOOTH_DEVICES_ERROR);
        throw windowsError("List Bluetooth radios", error);
    }
    std::vector<BluetoothDevice> devices;
    do
    {
        OwnedHandle<HANDLE, CloseHandle> radio(rawRadio);
        BLUETOOTH_DEVICE_SEARCH_PARAMS query{};
        query.dwSize = sizeof(query);
        query.fReturnConnected = TRUE;
        query.fReturnRemembered = paired;
        query.hRadio = radio.get();
        BLUETOOTH_DEVICE_INFO info{};
        info.dwSize = sizeof(info);
        OwnedHandle<HBLUETOOTH_DEVICE_FIND, BluetoothFindDeviceClose> found(::BluetoothFindFirstDevice(&query, &info));
        if (!found.get())
        {
            const auto error = ::GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) throw windowsError("List Bluetooth devices", error);
        }
        else
        {
            do
            {
                const auto address = BluetoothAddress::format(info.Address.ullLong);
                auto existing = std::find_if(devices.begin(), devices.end(), [&](const auto& device) { return device.mac == address; });
                if (existing == devices.end()) devices.push_back({utf8(info.szName), address, info.fConnected != FALSE});
                else existing->connected = existing->connected || info.fConnected != FALSE;
                info.dwSize = sizeof(info);
            } while (::BluetoothFindNextDevice(found.get(), &info));
            if (::GetLastError() != ERROR_NO_MORE_ITEMS) throw windowsError("Continue Bluetooth device listing", ::GetLastError());
        }
    } while (::BluetoothFindNextRadio(radios.get(), &rawRadio));
    if (::GetLastError() != ERROR_NO_MORE_ITEMS) throw windowsError("Continue Bluetooth radio listing", ::GetLastError());
    return devices;
}

std::optional<SonyProtocol> WindowsBluetoothConnector::discoverProtocol(const std::string& addr)
{
    // This API enumerates enabled services known to Windows. An absent Sony UUID
    // is inconclusive: custom RFCOMM services may not appear in this cache.
    // https://learn.microsoft.com/windows/win32/api/bluetoothapis/nf-bluetoothapis-bluetoothenumerateinstalledservices
    BLUETOOTH_DEVICE_INFO info = {};
    info.dwSize = sizeof(info);
    info.Address.ullLong = BluetoothAddress::parse(addr);
    DWORD count = 0;
    DWORD error = BluetoothEnumerateInstalledServices(nullptr, &info, &count, nullptr);
    if ((error != ERROR_SUCCESS && error != ERROR_MORE_DATA) || count == 0 || count > 4096)
    {
        return std::nullopt;
    }

    std::vector<GUID> services;
    // The service list can change between the size query and read. Retry only the
    // bounded read; do not flush caches, enable a service or initiate device inquiry.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (count == 0 || count > 4096) return std::nullopt;
        services.resize(count);
        error = BluetoothEnumerateInstalledServices(nullptr, &info, &count, services.data());
        if (error == ERROR_SUCCESS && count <= services.size())
        {
            services.resize(count);
            const GUID v1 = {0x96cc203e, 0x5068, 0x46ad, {0xb3, 0x2d, 0xe3, 0x16, 0xf5, 0xe0, 0x69, 0xba}};
            const GUID v2 = {0x956c7b26, 0xd49a, 0x4ba8, {0xb0, 0x3f, 0xb1, 0x7d, 0x39, 0x3c, 0xb6, 0xe2}};
            const auto has = [&](const GUID& target) {
                return std::any_of(services.begin(), services.end(), [&](const GUID& id) { return IsEqualGUID(id, target); });
            };
            if (has(v2)) return SonyProtocol::V2;
            if (has(v1)) return SonyProtocol::V1;
            return std::nullopt;
        }
        if (error != ERROR_MORE_DATA) break;
    }
    return std::nullopt;
}

int WindowsBluetoothConnector::getDeviceBatteryLevel(const std::string& deviceName)
{
	if (deviceName.empty())
		return -1;

	// Device names ("WH-1000XM4") are ASCII; widen for the W-form property APIs.
	std::wstring wantName(deviceName.begin(), deviceName.end());

	HDEVINFO devInfoSet = SetupDiGetClassDevs(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
	if (devInfoSet == INVALID_HANDLE_VALUE)
		return -1;

	int battery = -1;
	SP_DEVINFO_DATA devInfo = { sizeof(SP_DEVINFO_DATA) };
	for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfoSet, i, &devInfo); i++)
	{
		// Match the PnP node whose name carries the headphone's product name. Several nodes
		// (Hands-Free, AVRCP, ...) share that name; only the base device exposes the battery,
		// so keep scanning until we find one that has the property.
		std::wstring name;
		if (!readStringProperty(devInfoSet, &devInfo, PKEY_DeviceName, name) &&
			!readStringProperty(devInfoSet, &devInfo, PKEY_FriendlyName, name))
			continue;

		if (name.find(wantName) == std::wstring::npos)
			continue;

		DEVPROPTYPE propType = 0;
		DWORD requiredSize = 0;
		BYTE level = 0;
		if (SetupDiGetDevicePropertyW(devInfoSet, &devInfo, &PKEY_BluetoothBattery, &propType,
			&level, sizeof(level), &requiredSize, 0) && propType == DEVPROP_TYPE_BYTE)
		{
			battery = level;
			break;
		}
	}

	SetupDiDestroyDeviceInfoList(devInfoSet);
	return battery;
}
