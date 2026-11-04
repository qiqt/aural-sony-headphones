#pragma once

#include "CommandSerializer.h"
#include <algorithm>
#include <optional>
#include <vector>

// V1 combined NC/ASM capabilities. Sources: tm0/d2$c, vm0/a0 and
// v30/c/v30/e in Sound Connect 13.3.0; REA task-v1ambient-* evidence.
// Ambient levels are raw wire levels, not the combined Sony NC/ASM slider.
namespace SonyAmbient {
struct Entry {
    unsigned char id = 0;
    unsigned char steps = 0;
    bool operator==(const Entry&) const = default;
};
struct Capability {
    unsigned char ncType = 0;
    unsigned char ncSteps = 0;
    unsigned char asmType = 0;
    std::vector<Entry> entries;
    bool supports(unsigned char id) const {
        return std::any_of(entries.begin(), entries.end(), [id](const Entry& e) { return e.id == id; });
    }
    // Sony's v30/c and v30/e use 1..steps inclusive for active ambient.
    // On/off ASM carries 0/1 regardless of its unused step field.
    int maximum(unsigned char id) const {
        for (const auto& e : entries) if (e.id == id) return asmType == 0 ? 1 : e.steps;
        return 0;
    }
    bool modeWritesSupported() const { return ncType == 0 || ncType == 2; }
    bool operator==(const Capability&) const = default;
};

inline Buffer queryCapability() { return {0x60, 0x02}; }
inline Buffer queryState() { return {0x66, 0x02}; }

inline std::optional<Capability> parseCapability(const CommandSerializer::Message& msg)
{
    const auto& p = msg.messageBytes;
    auto u = [&](size_t i) { return static_cast<unsigned char>(p[i]); };
    if (msg.dataType != DATA_TYPE::DATA_MDR || p.size() < 6 || u(0) != 0x61 || u(1) != 2 ||
        u(2) > 2 || u(4) > 1 || p.size() != 6u + 2u * u(5)) return std::nullopt;
    Capability cap{u(2), u(3), u(4), {}};
    for (size_t i = 6; i < p.size(); i += 2) {
        const auto id = u(i), steps = u(i + 1);
        if (id > 1 || cap.supports(id) || (cap.asmType == 1 && steps == 0)) return std::nullopt;
        cap.entries.push_back({id, steps});
    }
    return cap;
}

enum class Mode { Off, NoiseCancelling, Ambient };
struct RawEdit {
    std::optional<unsigned char> effect;
    std::optional<unsigned char> ncValue;
    std::optional<unsigned char> asmId;
    std::optional<unsigned char> asmLevel;
};

inline bool effectValid(unsigned char effect)
{
    return effect == 0 || effect == 1 || effect == 0x10 || effect == 0x11;
}

// A narrowly scoped edit must never replace an on/off or dual/single encoding
// with the old hardcoded LEVEL_ADJUSTMENT encoding. Untouched fields survive.
// LEVEL_ADJUSTMENT capabilities are retained for diagnostics, but changing their
// NC level is withheld: its mode-to-level semantics are not established here.
inline std::optional<Buffer> buildSet(const Capability& cap,
    const CommandSerializer::NcAsmReport& current, const RawEdit& edit)
{
    if (current.ncSettingType != cap.ncType || current.asmSettingType != cap.asmType ||
        cap.ncType > 2 || cap.asmType > 1) return std::nullopt;
    auto value = current;
    if (edit.effect) value.effect = *edit.effect;
    if (edit.ncValue) value.ncValue = *edit.ncValue;
    if (edit.asmId) value.asmId = *edit.asmId;
    if (edit.asmLevel) value.asmLevel = *edit.asmLevel;
    if (!effectValid(value.effect) || !cap.supports(value.asmId)) return std::nullopt;
    if ((cap.ncType == 0 && value.ncValue > 1) || (cap.ncType == 2 && value.ncValue > 2) ||
        (cap.ncType == 1 && edit.ncValue && value.ncValue != current.ncValue)) return std::nullopt;
    // 0 means ambient is off; FF is a legacy off sentinel, preserved only while
    // the whole effect is off. Active level adjustment is one-based.
    if (value.asmLevel == 0xff) {
        if (value.effect != 0) return std::nullopt;
    } else if (value.asmLevel > cap.maximum(value.asmId)) return std::nullopt;
    return Buffer{0x68, 0x02, static_cast<char>(value.effect),
        static_cast<char>(value.ncSettingType), static_cast<char>(value.ncValue),
        static_cast<char>(value.asmSettingType), static_cast<char>(value.asmId),
        static_cast<char>(value.asmLevel)};
}

// The existing app separates NC/ambient/off instead of showing Sony's combined
// slider. ambientLevel is therefore a raw positive ambient level. For an on/off
// device the only active level is 1. Preserve a currently selected SINGLE NC mode.
inline std::optional<Buffer> buildModeSet(const Capability& cap,
    const CommandSerializer::NcAsmReport& current, Mode mode, int ambientLevel,
    unsigned char asmId)
{
    if (!cap.modeWritesSupported()) return std::nullopt;
    RawEdit edit;
    edit.effect = mode == Mode::Off ? 0 : 0x11;
    edit.asmId = asmId;
    switch (mode) {
    case Mode::Off:
        break;
    case Mode::NoiseCancelling:
        edit.ncValue = cap.ncType == 0 ? 1 : (current.ncValue == 1 ? 1 : 2);
        edit.asmLevel = 0;
        break;
    case Mode::Ambient:
        if (ambientLevel < 1 || ambientLevel > cap.maximum(asmId)) return std::nullopt;
        edit.ncValue = 0;
        edit.asmLevel = static_cast<unsigned char>(ambientLevel);
        break;
    default:
        return std::nullopt;
    }
    return buildSet(cap, current, edit);
}
}
