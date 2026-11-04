// Read-only V1 hardware capture. No SET, alert enable/response, or device-disconnect commands.
#include "windows/WindowsBluetoothConnector.h"
#include "Headphones.h"
#include "Listener.h"
#include "ProtocolTrace.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3)
    {
        std::cerr << "Usage: headset_probe --list | --read MAC | --read-v1 MAC | --read-v2 MAC\n"
                     "Set AURAL_TRACE to a file path to preserve raw replies. Close Aural first.\n";
        return 2;
    }
    const std::string mode = argv[1];
    if (!((mode == "--list" && argc == 2) || ((mode == "--read" || mode == "--read-v1" || mode == "--read-v2") && argc == 3)))
        return 2;
    BluetoothWrapper bt(std::make_unique<WindowsBluetoothConnector>());
    Headphones hp(bt);
    Listener listener(hp, bt);
    auto close = [&] { listener.requestStop(); bt.disconnect(); listener.stop(); };
    try
    {
        const auto devices = bt.getPairedDevices();
        if (mode == "--list")
        {
            for (const auto& d : devices)
                std::cout << d.mac << "\t" << (d.connected ? "connected" : "paired") << "\t" << d.name << '\n';
            return 0;
        }
        const std::string mac = argv[2];
        const auto device = std::find_if(devices.begin(), devices.end(), [&](const auto& d) { return d.mac == mac; });
        // Do not bring an offline headset online during a read-only work session.
        if (device == devices.end() || !device->connected)
            throw std::runtime_error("Read mode requires an already connected device at the specified address");
        const auto identity = identifyDevice(device->name);
        if (identity.protocol == SonyProtocol::V2 && mode != "--read-v2")
            throw std::runtime_error("Use --read-v2 MAC to explicitly capture this V2 device");
        if (mode == "--read" && device->name != "WH-1000XM4")
            throw std::runtime_error("Use --read-v1 MAC to explicitly attempt capability capture on another legacy device");
        std::cout << "Read-only snapshot: " << device->name << '\n';
        bt.connect(mac, mode == "--read-v2" ? SonyProtocol::V2 : SonyProtocol::V1);
        if (mode == "--read-v2" && bt.getProtocol() != SonyProtocol::V2)
            throw std::runtime_error("The advertised service is V1; use --read-v1 for this device");
        listener.start();
        hp.queryState();
        auto collect = [&](std::chrono::seconds duration) {
            const auto until = std::chrono::steady_clock::now() + duration;
            while (std::chrono::steady_clock::now() < until)
            {
                if (listener.linkLost()) throw std::runtime_error("Control connection closed during capture");
                hp.sendPendingQueries();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        };
        collect(std::chrono::seconds(3));
        if (bt.getProtocol() == SonyProtocol::V2) {
            const auto state = hp.getV2Snapshot();
            std::cout << "V2 discovery: " << state.phase << "\nModel: " << hp.reportedModel() << "\nFirmware: " << hp.firmwareVersion() << '\n';
            if (state.protocol) std::cout << "Protocol version: " << std::hex << state.protocol->version << std::dec << '\n';
            for (size_t table = 0; table < state.functions.size(); ++table)
                if (state.functions[table]) std::cout << "Table " << table+1 << " function records: " << state.functions[table]->functions.size() << '\n';
            std::cout << "Battery: " << hp.getBatteryLevel() << "\nEqualizer variants: " << state.equalizers.size() << "\nAmbient variants: " << state.ambient.size() << '\n';
            close();
            return state.phase == "ready" ? 0 : 1;
        }
        if (hp.functionState(FUNCTION_TYPE::PRESET_EQ) == CapabilityState::Supported)
            bt.sendCommand({0x50, 0x01}); // EQ capability
        if (hp.functionState(FUNCTION_TYPE::AUTO_POWER_OFF) == CapabilityState::Supported)
            bt.sendCommand({static_cast<char>(0xF0), 0x04}); // power-off capability
        // Additional read-only evidence for connection preference, indicators and assignments.
        const auto functionsNow = hp.supportedFunctions();
        auto advertised = [&](unsigned char id) {
            return std::find(functionsNow.begin(), functionsNow.end(), id) != functionsNow.end();
        };
        if (advertised(0xE1))
        {
            bt.sendCommand({static_cast<char>(0xE0), 0x01});
            bt.sendCommand({static_cast<char>(0xE6), 0x01});
        }
        if (advertised(0x13)) bt.sendCommand({0x18, 0x00});
        if (advertised(0x12)) bt.sendCommand({0x14, 0x00});
        if (advertised(0xF6))
        {
            bt.sendCommand({static_cast<char>(0xF0), 0x06});
            bt.sendCommand({static_cast<char>(0xF2), 0x06});
            bt.sendCommand({static_cast<char>(0xF6), 0x06});
        }
        collect(std::chrono::seconds(3));
        std::cout << "Reported model: " << hp.reportedModel() << "\nFirmware: " << hp.firmwareVersion() << '\n';
        std::cout << "Capabilities received: " << hp.capabilitiesKnown() << '\n';
        Buffer functions;
        for (auto id : hp.supportedFunctions()) functions.push_back(static_cast<char>(id));
        std::cout << "Functions: " << ProtocolTrace::hex(functions) << '\n';
        std::cout << "Battery: " << hp.getBatteryLevel() << "\nEQ level count: " << hp.getEqualizerLevelCount() << '\n';
        if (auto cap = hp.getEqualizerCapability())
            std::cout << "EQ capability: " << static_cast<int>(cap->bands) << " levels, " <<
                static_cast<int>(cap->levelSteps) << " steps, " << cap->presets.size() << " presets\n";
        if (auto cap = hp.getPowerOffCapability())
            std::cout << "Power-off capability choices: " << cap->size() << '\n';
        if (auto mode = hp.getConnectionMode())
            std::cout << "Connection preference: " << static_cast<int>(*mode) << " (0 quality, 1 stability); writable " << hp.connectionModeReady() << '\n';
        if (auto codec = hp.getCodec()) std::cout << "Codec ID: " << static_cast<int>(*codec) << '\n';
        if (auto effect = hp.getDseeActivity()) std::cout << "DSEE activity status: " << static_cast<int>(effect->status) << '\n';
        if (auto cap = hp.getAssignmentCapability()) std::cout << "Assignable controls: " << cap->keys.size() << '\n';
        auto toggle = [](const char* name, const ToggleSnapshot& t) {
            std::cout << name << ": " << (t.valueKnown ? (t.value ? "on" : "off") : "unknown") << '\n';
        };
        toggle("Touch panel", hp.getTouchPanel());
        toggle("Wearing control", hp.getWearingControl());
        toggle("Multipoint", hp.getMultipointMode());
        close();
        return hp.capabilitiesKnown() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        close();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
