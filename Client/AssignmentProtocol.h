#pragma once

// Sony V1 assignment schema, recovered from Sound Connect 13.3.0:
// wm0/c.c -> wm0/b.i -> wm0/d.g -> wm0/a.g (capability);
// vm0/d.c/a (preset list); vm0/e.c/a (availability list); l00/b.f/p (key order).
// REA evidence: ev_0e8af1c0458bae5e8a50d00819ac91cf456c4171b9140850cd97577e3a0b5836,
// ev_718b7c42944f9042c6b0264269556cac843e5b8715a2cd68638931b1029129ad,
// ev_f46fabdad2fbea3b9b4e054782b64d08e4d0841b74286c7523165d3aa1c37747.
// Writes choose a whole preset for each advertised key; individual gestures are descriptive.
#include "CommandSerializer.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace SonyAssignment
{
using Byte = std::uint8_t;
struct Action { Byte action{}, function{}; bool operator==(const Action&) const = default; };
struct Preset { Byte id{}; std::vector<Action> actions; bool operator==(const Preset&) const = default; };
struct Key { Byte id{}, type{}, defaultPreset{}; std::vector<Preset> presets; bool operator==(const Key&) const = default; };
struct Capability { std::vector<Key> keys; bool operator==(const Capability&) const = default; };

inline bool validPreset(Byte p)
{
    return p == 0 || p == 0x10 || p == 0x20 || (p >= 0x30 && p <= 0x33) || p == 0xff;
}
inline bool validAction(Byte a) { return a <= 2 || a == 0x10 || a == 0x11 || a == 0x21 || a == 0x22; }
inline bool validFunction(Byte f) { return f <= 2 || (f >= 0x10 && f <= 0x12) || (f >= 0x20 && f <= 0x22) || (f >= 0x30 && f <= 0x36); }
inline bool containsPreset(const Key& key, Byte preset)
{
    return std::any_of(key.presets.begin(), key.presets.end(), [&](const Preset& p) { return p.id == preset; });
}
inline Buffer queryCapability() { return {static_cast<char>(0xf0), 0x06}; }
inline Buffer queryState() { return {static_cast<char>(0xf6), 0x06}; }
inline Buffer queryAvailability() { return {static_cast<char>(0xf2), 0x06}; }

inline std::optional<Capability> parseCapability(const CommandSerializer::Message& msg)
{
    const auto& p = msg.messageBytes;
    auto u = [&](size_t i) { return static_cast<Byte>(p[i]); };
    if (msg.dataType != DATA_TYPE::DATA_MDR || p.size() < 3 || u(0) != 0xf1 || u(1) != 6 || u(2) > 4)
        return std::nullopt;
    Capability result;
    size_t pos = 3;
    for (unsigned n = 0; n < u(2); ++n)
    {
        if (p.size() - pos < 4) return std::nullopt;
        Key key{u(pos), u(pos + 1), u(pos + 2), {}};
        const unsigned presets = u(pos + 3);
        pos += 4;
        if (key.id > 3 || key.type > 1 || !validPreset(key.defaultPreset) || presets == 0 || presets > 8)
            return std::nullopt;
        if (std::any_of(result.keys.begin(), result.keys.end(), [&](const Key& k) { return k.id == key.id; }))
            return std::nullopt;
        for (unsigned i = 0; i < presets; ++i)
        {
            if (p.size() - pos < 2) return std::nullopt;
            Preset preset{u(pos), {}};
            const unsigned actions = u(pos + 1);
            pos += 2;
            if (!validPreset(preset.id) || containsPreset(key, preset.id) || actions > 7 || p.size() - pos < actions * 2)
                return std::nullopt;
            for (unsigned j = 0; j < actions; ++j)
            {
                Action action{u(pos), u(pos + 1)};
                pos += 2;
                if (!validAction(action.action) || !validFunction(action.function) ||
                    std::any_of(preset.actions.begin(), preset.actions.end(), [&](const Action& a) { return a.action == action.action; }))
                    return std::nullopt;
                preset.actions.push_back(action);
            }
            key.presets.push_back(std::move(preset));
        }
        if (!containsPreset(key, key.defaultPreset)) return std::nullopt;
        result.keys.push_back(std::move(key));
    }
    if (pos != p.size()) return std::nullopt;
    return result;
}

inline std::optional<std::vector<Byte>> parseState(const CommandSerializer::Message& msg)
{
    const auto& p = msg.messageBytes;
    if (msg.dataType != DATA_TYPE::DATA_MDR || p.size() < 3) return std::nullopt;
    const auto cmd = static_cast<Byte>(p[0]);
    const auto count = static_cast<Byte>(p[2]);
    if ((cmd != 0xf7 && cmd != 0xf9) || p[1] != 6 || count > 4 || p.size() != size_t(3 + count))
        return std::nullopt;
    std::vector<Byte> result;
    for (size_t i = 3; i < p.size(); ++i)
    {
        const auto preset = static_cast<Byte>(p[i]);
        if (!validPreset(preset)) return std::nullopt;
        result.push_back(preset);
    }
    return result;
}

inline std::optional<std::vector<bool>> parseAvailability(const CommandSerializer::Message& msg)
{
    const auto& p = msg.messageBytes;
    if (msg.dataType != DATA_TYPE::DATA_MDR || p.size() < 3) return std::nullopt;
    const auto cmd = static_cast<Byte>(p[0]);
    const auto count = static_cast<Byte>(p[2]);
    if ((cmd != 0xf3 && cmd != 0xf5) || p[1] != 6 || count > 4 || p.size() != size_t(3 + count))
        return std::nullopt;
    std::vector<bool> result;
    for (size_t i = 3; i < p.size(); ++i)
    {
        const auto status = static_cast<Byte>(p[i]);
        if (status > 1) return std::nullopt;
        result.push_back(status == 0); // CommonStatus.ENABLE=00
    }
    return result;
}

inline std::optional<Buffer> buildSet(const Capability& cap, const std::vector<Byte>& current, size_t index, Byte preset)
{
    if (cap.keys.empty() || cap.keys.size() > 4 || current.size() != cap.keys.size() || index >= current.size() ||
        !containsPreset(cap.keys[index], preset)) return std::nullopt;
    // Validate every preserved value: an unknown current assignment is not safe to replay.
    for (size_t i = 0; i < current.size(); ++i)
        if (!containsPreset(cap.keys[i], current[i])) return std::nullopt;
    Buffer out{static_cast<char>(0xf8), 6, static_cast<char>(current.size())};
    for (size_t i = 0; i < current.size(); ++i)
        out.push_back(static_cast<char>(i == index ? preset : current[i]));
    return out;
}

inline const char* keyName(Byte key)
{
    switch (key) { case 0: return "Left control"; case 1: return "Right control"; case 2: return "Custom button"; case 3: return "C button"; default: return "Unknown control"; }
}
inline const char* presetName(Byte preset)
{
    switch (preset) { case 0: return "Ambient sound control"; case 0x10: return "Volume control"; case 0x20: return "Playback control"; case 0x30: return "Voice assistant"; case 0x31: return "Google Assistant"; case 0x32: return "Amazon Alexa"; case 0x33: return "Tencent Xiaowei"; case 0xff: return "No function"; default: return "Unknown preset"; }
}
inline const char* actionName(Byte action)
{
    switch (action) { case 0: return "Single press/tap"; case 1: return "Double press/tap"; case 2: return "Triple press/tap"; case 0x10: return "Press/tap and hold"; case 0x11: return "Double press/tap and hold"; case 0x21: return "After long press"; case 0x22: return "While holding"; default: return "Unknown action"; }
}
inline const char* functionName(Byte function)
{
    switch (function) { case 0: return "No function"; case 1: return "Cycle noise cancelling / ambient / off"; case 2: return "Noise cancelling optimizer"; case 0x10: return "Quick attention"; case 0x11: return "Volume up"; case 0x12: return "Volume down"; case 0x20: return "Play/pause"; case 0x21: return "Next track"; case 0x22: return "Previous track"; case 0x30: return "Voice assistant"; case 0x31: return "Read notifications"; case 0x32: return "Talk to Google Assistant"; case 0x33: return "Stop Google Assistant"; case 0x34: return "Voice input / cancel Alexa"; case 0x35: return "Talk to Tencent Xiaowei"; case 0x36: return "Cancel voice assistant"; default: return "Unknown function"; }
}
}
