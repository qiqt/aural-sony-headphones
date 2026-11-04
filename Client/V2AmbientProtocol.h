#pragma once

#include "CommandSerializer.h"
#include <optional>
#include <stdexcept>
#include <vector>

// Sound Connect 13.3.0: sn0/{d0,h0,i1,m1}, un0/{b,l,p}, w30/{r,v}.
// Explicit-mode variants additionally use un0/{f,g,h}, sn0/{c1,d1,e1}.
// Only verified seamless variants are writable. Levels here are raw
// wire levels, not Sony's combined NC/ambient slider positions.
namespace SonyV2Ambient {
inline bool supportsSelector(unsigned char selector) {
    return selector == 0x13 || selector == 0x14 || selector == 0x16 || selector == 0x17 || selector == 0x19;
}
inline bool hasExplicitMode(unsigned char selector) { return selector == 0x16 || selector == 0x17 || selector == 0x19; }
enum class Mode { Off, NoiseCancelling, Ambient };
struct Range {
    unsigned char id = 0, minimum = 0, maximum = 0, step = 0;
    bool contains(int level) const {
        return step != 0 && level >= minimum && level <= maximum && (level - minimum) % step == 0;
    }
    bool operator==(const Range&) const = default;
};
struct Capability {
    unsigned char selector = 0;
    std::vector<Range> ranges;
    const Range* range(unsigned char id) const {
        for (const auto& r : ranges) if (r.id == id) return &r;
        return nullptr;
    }
    bool operator==(const Capability&) const = default;
};
struct State {
    unsigned char selector = 0, changeStatus = 0, effect = 0, ncValue = 0, asmId = 0, level = 0;
    unsigned char wireMode = 0, noiseAdaptive = 0, sensitivity = 0;
    Mode mode() const {
        if (effect == 0) return Mode::Off;
        if (hasExplicitMode(selector)) return wireMode == 0 ? Mode::NoiseCancelling : Mode::Ambient;
        return ncValue != 0 ? Mode::NoiseCancelling : Mode::Ambient;
    }
    bool valid() const {
        return supportsSelector(selector) && changeStatus <= 1 && effect <= 1 &&
            ncValue <= (selector == 0x13 ? 1 : 5) && asmId <= 1 && wireMode <= 1 &&
            noiseAdaptive <= 1 && sensitivity <= 2;
    }
    bool operator==(const State&) const = default;
};
struct Availability {
    unsigned char selector = 0;
    bool available = false;
    bool operator==(const Availability&) const = default;
};
inline Buffer query(unsigned char command, unsigned char selector) {
    if (!supportsSelector(selector)) throw std::invalid_argument("Unverified V2 NC/ASM selector");
    return {static_cast<char>(command), static_cast<char>(selector)};
}
inline Buffer queryCapability(unsigned char selector) { return query(0x60, selector); }
inline Buffer queryStatus(unsigned char selector) { return query(0x62, selector); }
inline Buffer queryState(unsigned char selector) { return query(0x66, selector); }

inline std::optional<Capability> parseCapability(const CommandSerializer::Message& msg) {
    const auto& p = msg.messageBytes;
    auto u = [&](size_t i) { return static_cast<unsigned char>(p[i]); };
    if (msg.dataType != DATA_TYPE::DATA_MDR || p.size() < 3 || u(0) != 0x61 ||
        !supportsSelector(u(1)) || p.size() != 3u + 4u * u(2)) return std::nullopt;
    Capability cap{u(1), {}};
    for (size_t i = 3; i < p.size(); i += 4) {
        Range r{u(i), u(i + 1), u(i + 2), u(i + 3)};
        if (r.id > 1 || cap.range(r.id) || r.minimum == 255 || r.maximum == 0 ||
            r.minimum > r.maximum || r.step == 0) return std::nullopt;
        cap.ranges.push_back(r);
    }
    return cap;
}
inline std::optional<State> parseState(const CommandSerializer::Message& msg) {
    const auto& p = msg.messageBytes;
    auto u = [&](size_t i) { return static_cast<unsigned char>(p[i]); };
    if (msg.dataType != DATA_TYPE::DATA_MDR || p.size() < 7 ||
        (u(0) != 0x67 && u(0) != 0x69)) return std::nullopt;
    const size_t expected = u(1) == 0x16 ? 8 : u(1) == 0x19 ? 9 : 7;
    if (p.size() != expected) return std::nullopt;
    State state{u(1), u(2), u(3), u(4), u(5), u(6)};
    if (hasExplicitMode(state.selector)) {
        state.wireMode = u(4);
        state.ncValue = state.selector == 0x16 ? u(5) : 0;
        state.asmId = u(state.selector == 0x16 ? 6 : 5);
        state.level = u(state.selector == 0x16 ? 7 : 6);
        if (state.selector == 0x19) { state.noiseAdaptive = u(7); state.sensitivity = u(8); }
    }
    return state.valid() ? std::optional<State>{state} : std::nullopt;
}
inline std::optional<Availability> parseAvailability(const CommandSerializer::Message& msg) {
    const auto& p = msg.messageBytes;
    auto u = [&](size_t i) { return static_cast<unsigned char>(p[i]); };
    if (msg.dataType != DATA_TYPE::DATA_MDR || p.size() != 3 ||
        (u(0) != 0x63 && u(0) != 0x65) || !supportsSelector(u(1)) || u(2) > 1) return std::nullopt;
    return Availability{u(1), u(2) == 0}; // EnableDisable.ENABLE is zero.
}
inline std::optional<Buffer> buildModeSet(const Capability& cap, const State& current,
    Mode mode, int rawLevel, unsigned char asmId, bool available) {
    if (!available || !current.valid() || current.selector != cap.selector ||
        current.changeStatus != 1) return std::nullopt;
    auto next = current;
    next.effect = mode == Mode::Off ? 0 : 1;
    if (!cap.range(current.asmId)) return std::nullopt;
    if (mode != Mode::Off) {
        if (!cap.range(asmId)) return std::nullopt;
        next.asmId = asmId;
    }
    switch (mode) {
    case Mode::Off: break; // Keep the complete last selected representation.
    case Mode::NoiseCancelling:
        if (hasExplicitMode(cap.selector)) {
            next.wireMode = 0;
            // Explicit mode variants retain the stored ambient level. The
            // separate NC selection in 16 is also retained without coercion.
            break;
        }
        // These adapters offer ON or SINGLE/DUAL, not an automatic-NC control.
        if (cap.selector == 0x14 && current.ncValue > 2) return std::nullopt;
        next.ncValue = cap.selector == 0x13 ? 1 : current.ncValue == 1 ? 1 : 2;
        next.level = 0;
        break;
    case Mode::Ambient:
        if (!cap.range(asmId)->contains(rawLevel)) return std::nullopt;
        if (hasExplicitMode(cap.selector)) next.wireMode = 1;
        else next.ncValue = 0;
        next.level = static_cast<unsigned char>(rawLevel);
        break;
    default: return std::nullopt;
    }
    if (hasExplicitMode(cap.selector)) {
        Buffer result{0x68, static_cast<char>(cap.selector), 0x01, static_cast<char>(next.effect),
            static_cast<char>(next.wireMode)};
        if (cap.selector == 0x16) result.push_back(static_cast<char>(next.ncValue));
        result.push_back(static_cast<char>(next.asmId));
        result.push_back(static_cast<char>(next.level));
        if (cap.selector == 0x19) {
            result.push_back(static_cast<char>(next.noiseAdaptive));
            result.push_back(static_cast<char>(next.sensitivity));
        }
        return result;
    }
    return Buffer{0x68, static_cast<char>(cap.selector), 0x01, static_cast<char>(next.effect),
        static_cast<char>(next.ncValue), static_cast<char>(next.asmId), static_cast<char>(next.level)};
}
}
