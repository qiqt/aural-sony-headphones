#pragma once

#include "CommandSerializer.h"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

// Recovered from Sound Connect 13.3.0 tm0/z1$c, tm0/t2$c and h20/c.
// See the retained task-eq evidence in the reverse-engineering lab. These are
// protocol level steps; no claim is made that one step is one acoustic dB.
namespace SonyEq {
struct Preset {
    unsigned char id = 0;
    std::string name;
    bool operator==(const Preset&) const = default;
};
struct Capability {
    unsigned char bands = 0;
    unsigned char levelSteps = 0;
    std::vector<Preset> presets;
    bool supports(unsigned char id) const {
        return std::any_of(presets.begin(), presets.end(), [id](const auto& p) { return p.id == id; });
    }
    int maximum() const { return (static_cast<int>(levelSteps) - 1) / 2; }
    int minimum() const { return -maximum(); }
    bool operator==(const Capability&) const = default;
};

inline Buffer queryCapability() { return {0x50, 0x01}; }
inline Buffer queryPowerOffCapability() { return {static_cast<char>(0xF0), 0x04}; }

inline std::optional<Capability> parseCapability(const CommandSerializer::Message& msg)
{
    const auto& p = msg.messageBytes;
    auto u = [&](size_t i) { return static_cast<unsigned char>(p[i]); };
    if (msg.dataType != DATA_TYPE::DATA_MDR || p.size() < 5 || u(0) != 0x51 || u(1) != 1)
        return std::nullopt;
    Capability result;
    result.bands = u(2);
    result.levelSteps = u(3);
    if (!result.bands || !result.levelSteps) return std::nullopt;
    const size_t count = u(4);
    size_t pos = 5;
    for (size_t n = 0; n < count; ++n) {
        if (p.size() - pos < 2) return std::nullopt;
        const auto id = u(pos++);
        const size_t length = u(pos++);
        if (length > 128 || length > p.size() - pos || result.supports(id)) return std::nullopt;
        result.presets.push_back({id, std::string(p.begin() + pos, p.begin() + pos + length)});
        pos += length;
    }
    if (pos != p.size()) return std::nullopt;
    return result;
}

inline std::optional<std::vector<unsigned char>> parsePowerOffCapability(const CommandSerializer::Message& msg)
{
    const auto& p = msg.messageBytes;
    if (msg.dataType != DATA_TYPE::DATA_MDR || p.size() < 3 ||
        static_cast<unsigned char>(p[0]) != 0xF1 || p[1] != 4 ||
        p.size() != 3u + static_cast<unsigned char>(p[2])) return std::nullopt;
    std::vector<unsigned char> result;
    for (size_t i = 3; i < p.size(); ++i) {
        const auto id = static_cast<unsigned char>(p[i]);
        if (!v1::isAutoPowerOffElement(id) || std::find(result.begin(), result.end(), id) != result.end())
            return std::nullopt;
        result.push_back(id);
    }
    return result;
}

inline std::optional<std::vector<int>> decodeLevels(const Capability& cap, const std::vector<unsigned char>& raw)
{
    if (!cap.levelSteps || raw.size() != cap.bands) return std::nullopt;
    std::vector<int> result;
    for (auto value : raw) {
        if (value >= cap.levelSteps) return std::nullopt;
        result.push_back(static_cast<int>(value) - cap.maximum());
    }
    return result;
}

// Sony's V1 band-edit sender uses UNSPECIFIED regardless of the preset argument.
// This edits the current curve; do not substitute A1/A2 as a guessed save opcode.
inline std::optional<Buffer> editCurve(const Capability& cap, const std::vector<int>& levels)
{
    if (!cap.levelSteps || levels.size() != cap.bands) return std::nullopt;
    Buffer result{0x58, 0x01, static_cast<char>(0xFF), static_cast<char>(cap.bands)};
    for (int value : levels) {
        if (value < cap.minimum() || value > cap.maximum()) return std::nullopt;
        result.push_back(static_cast<char>(value + cap.maximum()));
    }
    return result;
}
}
