#pragma once

#include "CommandSerializer.h"
#include "EqualizerProtocol.h"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

// Sound Connect 13.3.0 V2 codecs. Only the listed inquiry variants are accepted.
// Evidence: ev_b2611764fd008f57de907f487b95264c478009deb55ff2beeb822bc2f64e6bc8 (versions),
// ev_0d2d01d7cdf128232a752d9af26e5f9e191583e5147084f50c4d2db7c52c4f88 (battery),
// ev_354404f5a730ad6010cdcc2b31f881490bce7364378faa090f43d90a70ce439b (enums),
// plus the EQ/catalog records in PROTOCOL-MULTIMODEL.md.
// These are static-artifact structures, not XM5/XM6 hardware fixtures. All callers
// must route by generation before invoking this namespace (V1 reuses command IDs).
namespace SonyV2 {
using Byte = unsigned char;
using Message = CommandSerializer::Message;
inline Byte u(const Buffer& b, size_t i) { return static_cast<Byte>(b[i]); }
inline bool table1(const Message& m) { return m.dataType == DATA_TYPE::DATA_MDR; }
inline bool tableKnown(DATA_TYPE t) { return t == DATA_TYPE::DATA_MDR || t == DATA_TYPE::DATA_MDR_NO2; }
inline bool eqSelector(Byte s) { return s == 0x00 || s == 0x31; }

struct ProtocolInfo {
    uint32_t version = 0;
    bool table1Enabled = false, table2Enabled = false;
    bool operator==(const ProtocolInfo&) const = default;
};
struct CapabilityInfo {
    Byte revision = 0;
    std::string identifier;
    bool operator==(const CapabilityInfo&) const = default;
};
struct Function {
    Byte id = 0, order = 0;
    bool operator==(const Function&) const = default;
};
struct SupportedFunctions {
    DATA_TYPE table = DATA_TYPE::DATA_MDR;
    std::vector<Function> functions;
    bool supports(Byte id) const {
        return std::any_of(functions.begin(), functions.end(), [id](const auto& f) { return f.id == id; });
    }
    bool operator==(const SupportedFunctions&) const = default;
};
struct DeviceInfo {
    Byte type = 0;
    std::string value;
    bool operator==(const DeviceInfo&) const = default;
};
struct Battery {
    int level = 0;
    bool charging = false;
    bool chargingKnown = true;
    Byte chargingStatus = 0; // 0 not charging, 1 charging, 2 unknown, 3 charged
    bool operator==(const Battery&) const = default;
};
struct EqCapability {
    Byte selector = 0, bands = 0, levelSteps = 0;
    std::vector<SonyEq::Preset> presets;
    bool supports(Byte id) const {
        return std::any_of(presets.begin(), presets.end(), [id](const auto& p) { return p.id == id; });
    }
    bool operator==(const EqCapability&) const = default;
};
struct EqState {
    Byte selector = 0;
    std::optional<Byte> preset;
    std::vector<Byte> levels; // Raw steps. No acoustic dB/band-frequency inference here.
    bool operator==(const EqState&) const = default;
};
struct EqAvailability {
    Byte selector = 0;
    bool available = false;
    bool operator==(const EqAvailability&) const = default;
};
struct EqBandDescriptor {
    Byte kind = 0;
    uint16_t value = 0;
    bool known() const {
        return ((kind == 1 || kind == 2) && value != 0) || (kind == 0x10 && value == 1);
    }
    bool operator==(const EqBandDescriptor&) const = default;
};
struct EqBandMetadata {
    Byte selector = 0;
    std::vector<EqBandDescriptor> bands;
    bool operator==(const EqBandMetadata&) const = default;
};

// vv/e.i whitelist. This is intentionally not a major-version >= comparison.
inline bool protocolVersionSupported(uint32_t version) {
    static constexpr uint32_t known[] = { 0x1000000, 0x2000000, 0x2000001, 0x2001000, 0x3000001, 0x3000000, 0x3000002, 0x3000004, 0x3000005, 0x3000003, 0x3000006, 0x3000007, 0x3000008, 0x3000009, 0x3000010, 0x3000011, 0x3000012, 0x3000013, 0x3000014, 0x3000015, 0x3000016, 0x3000017, 0x3000018, 0x3000019, 0x3000020, 0x3001001, 0x3001002, 0x3001003, 0x3001004, 0x3002000, 0x3002001, 0x3002002, 0x3002003, 0x3002004, 0x3002005, 0x3002006, 0x3002007, 0x3002008, 0x3002009, 0x3002012, 0x3002013, 0x3002014, 0x3002015, 0x3002016, 0x3002017, 0x3002018, 0x3002019, 0x3003000, 0x3003001, 0x3003002, 0x3003003, 0x3003004, 0x3003005, 0x3003006, 0x3003007, 0x3003008, 0x3003009, 0x3003010, 0x3003011, 0x3003012, 0x3003013, 0x3003014, 0x3003015, 0x3003016, 0x3003017, 0x3003018, 0x3003019, 0x3003020, 0x3003021, 0x3003023, 0x3003024, 0x3003025, 0x3003026, 0x3003032, 0x3003033, 0x3003037 };
    return std::find(std::begin(known), std::end(known), version) != std::end(known);
}

inline Buffer queryProtocolInfo() { return {0x00, 0x00}; }
inline Buffer queryCapabilityInfo() { return {0x02, 0x00}; }
inline Buffer querySupportedFunctions() { return {0x06, 0x00}; }
inline Buffer queryDeviceInfo(Byte type) { return {0x04, static_cast<char>(type)}; }
inline Buffer queryBattery() { return {0x22, 0x00}; }
inline Buffer queryEqCapability(Byte selector) { return {0x50, static_cast<char>(selector)}; }
inline Buffer queryEqState(Byte selector) { return {0x56, static_cast<char>(selector)}; }
inline Buffer queryEqAvailability(Byte selector) { return {0x52, static_cast<char>(selector)}; }
inline Buffer queryEqBandMetadata(Byte selector) { return {0x5a, static_cast<char>(selector)}; }

// Strict UTF-8 plus rejection of embedded control characters in UI-facing strings.
inline bool textValid(const Buffer& p, size_t start, size_t length) {
    if (start > p.size() || length > p.size() - start) return false;
    const size_t end = start + length;
    for (size_t i = start; i < end;) {
        const Byte lead = u(p, i++);
        if (lead < 0x20 || lead == 0x7f) return false;
        if (lead < 0x80) continue;
        size_t count = 0; uint32_t cp = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { count = 1; cp = lead & 0x1f; }
        else if (lead >= 0xe0 && lead <= 0xef) { count = 2; cp = lead & 0x0f; }
        else if (lead >= 0xf0 && lead <= 0xf4) { count = 3; cp = lead & 7; }
        else return false;
        if (count > end - i) return false;
        for (size_t n = 0; n < count; ++n) {
            const Byte b = u(p, i++);
            if ((b & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (b & 0x3f);
        }
        if ((count == 2 && cp < 0x800) || (count == 3 && cp < 0x10000) ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return false;
    }
    return true;
}

// in0/k$b and util/e.g: big-endian version, EnableDisable uses 00 = ENABLE.
inline std::optional<ProtocolInfo> parseProtocolInfo(const Message& m) {
    const auto& p = m.messageBytes;
    if (!table1(m) || p.size() != 8 || u(p,0) != 1 || u(p,1) != 0 || u(p,6) > 1 || u(p,7) > 1) return {};
    const auto v = (uint32_t(u(p,2)) << 24) | (uint32_t(u(p,3)) << 16) | (uint32_t(u(p,4)) << 8) | u(p,5);
    return ProtocolInfo{v, u(p,6) == 0, u(p,7) == 0};
}
// in0/e: byte2 is capability revision, byte3 length, then capability identifier.
inline std::optional<CapabilityInfo> parseCapabilityInfo(const Message& m) {
    const auto& p = m.messageBytes;
    if (!table1(m) || p.size() < 5 || u(p,0) != 3 || u(p,1) != 0 ||
        u(p,3) > 128 || p.size() != 4u + u(p,3) || !textValid(p,4,u(p,3))) return {};
    return CapabilityInfo{u(p,2), std::string(p.begin()+4,p.end())};
}
// in0/l$b and V2 table2 counterpart: preserve table and ordering byte.
inline std::optional<SupportedFunctions> parseSupportedFunctions(const Message& m) {
    const auto& p = m.messageBytes;
    if (!tableKnown(m.dataType) || p.size() < 3 || u(p,0) != 7 || u(p,1) != 0 || p.size() != 3u + 2u*u(p,2)) return {};
    SupportedFunctions out; out.table = m.dataType;
    for (size_t i = 3; i < p.size(); i += 2) {
        if (out.supports(u(p,i))) return {};
        out.functions.push_back({u(p,i),u(p,i+1)});
    }
    return out;
}
// in0/i / in0/g: model and firmware are bounded StringData fields.
inline std::optional<DeviceInfo> parseDeviceInfo(const Message& m) {
    const auto& p = m.messageBytes;
    if (!table1(m) || p.size() < 4 || u(p,0) != 5 || (u(p,1) != 1 && u(p,1) != 2) ||
        u(p,2) > 128 || p.size() != 3u+u(p,2) || !textValid(p,3,u(p,2))) return {};
    return DeviceInfo{u(p,1),std::string(p.begin()+3,p.end())};
}
// yn0/k0 (RET), same POWER status layout for NTFY. Only single battery type00.
inline std::optional<Battery> parseBattery(const Message& m) {
    const auto& p = m.messageBytes;
    if (!table1(m) || p.size() != 4 || (u(p,0) != 0x23 && u(p,0) != 0x25) || u(p,1) != 0 ||
        u(p,2) > 100 || u(p,3) > 3) return {};
    return Battery{u(p,2),u(p,3)==1,u(p,3)!=2,u(p,3)};
}

// jn0/z + jn0/w. Unknown inquiry variants are deliberately not interpreted.
inline std::optional<EqCapability> parseEqCapability(const Message& m) {
    const auto& p = m.messageBytes;
    if (!table1(m) || p.size() < 4 || u(p,0) != 0x51 || !eqSelector(u(p,1)) || !u(p,2) || !u(p,3)) return {};
    EqCapability out{u(p,1),u(p,2),u(p,3),{}};
    if (out.selector == 0x31) return p.size() == 4 ? std::optional<EqCapability>(out) : std::nullopt;
    if (p.size() < 5) return {};
    size_t pos = 5;
    for (size_t n = 0; n < u(p,4); ++n) {
        if (p.size() - pos < 2) return {};
        const Byte id = u(p,pos++); const size_t length = u(p,pos++);
        if (out.supports(id) || length > 128 || length > p.size()-pos || !textValid(p,pos,length)) return {};
        out.presets.push_back({id,std::string(p.begin()+pos,p.begin()+pos+length)});
        pos += length;
    }
    if (pos != p.size()) return {};
    return out;
}
inline std::optional<EqAvailability> parseEqAvailability(const Message& m) {
    const auto& p = m.messageBytes;
    if (!table1(m) || p.size() != 3 || (u(p,0)!=0x53 && u(p,0)!=0x55) || !eqSelector(u(p,1)) || u(p,2)>1) return {};
    return EqAvailability{u(p,1),u(p,2)==0};
}
// jn0/j0 + kn0/c (preset), custom uses kn0/a. RET and NTFY carry same inner structure.
inline std::optional<EqState> parseEqState(const Message& m) {
    const auto& p = m.messageBytes;
    if (!table1(m) || p.size() < 3 || (u(p,0)!=0x57 && u(p,0)!=0x59) || !eqSelector(u(p,1))) return {};
    EqState out; out.selector = u(p,1);
    size_t countOffset = 2;
    if (out.selector == 0) { if (p.size()<4) return {}; out.preset=u(p,2); countOffset=3; }
    if (p.size() != countOffset+1+u(p,countOffset)) return {};
    for (size_t i=countOffset+1;i<p.size();++i) out.levels.push_back(u(p,i));
    return out;
}
inline bool eqStateFits(const EqCapability& cap, const EqState& state) {
    if (cap.selector != state.selector || state.levels.size() != cap.bands) return false;
    if (cap.selector == 0 && (!state.preset || (*state.preset != 0xff && !cap.supports(*state.preset)))) return false;
    return std::all_of(state.levels.begin(),state.levels.end(),[&](Byte b){return b<cap.levelSteps;});
}
// jn0/e0 and kn0/b: count followed by {kind, uint16 big-endian value}.
// 01 = Hz, 02 = kHz, 10/0001 = Clear Bass. Preserve unknown descriptors
// for diagnostics, but never use them to enable a labelled curve editor.
inline std::optional<EqBandMetadata> parseEqBandMetadata(const Message& m) {
    const auto& p = m.messageBytes;
    if (!table1(m) || p.size() < 3 || u(p,0) != 0x5b || !eqSelector(u(p,1)) ||
        p.size() != 3u + 3u*u(p,2)) return {};
    EqBandMetadata out{u(p,1),{}};
    for (size_t i = 3; i < p.size(); i += 3)
        out.bands.push_back({u(p,i),static_cast<uint16_t>((uint16_t(u(p,i+1)) << 8) | u(p,i+2))});
    return out;
}
inline bool eqBandMetadataFits(const EqCapability& cap, const EqBandMetadata& metadata) {
    return eqSelector(cap.selector) && cap.bands != 0 && cap.selector == metadata.selector &&
        cap.bands == metadata.bands.size() &&
        std::all_of(metadata.bands.begin(),metadata.bands.end(),[](const auto& band){return band.known();});
}
inline std::optional<Buffer> selectEqPreset(const EqCapability& cap, Byte preset) {
    if (cap.selector != 0 || !cap.bands || !cap.levelSteps || !cap.supports(preset)) return {};
    return Buffer{0x58,0x00,static_cast<char>(preset),0x00};
}
// Raw-step serializer, deliberately separate from UI gain/frequency semantics.
inline std::optional<Buffer> editEqCurve(const EqCapability& cap, const std::vector<Byte>& levels) {
    if (!eqSelector(cap.selector) || !cap.bands || !cap.levelSteps || levels.size()!=cap.bands ||
        !std::all_of(levels.begin(),levels.end(),[&](Byte b){return b<cap.levelSteps;})) return {};
    Buffer out{0x58,static_cast<char>(cap.selector)};
    if (cap.selector == 0) out.push_back(static_cast<char>(0xff));
    out.push_back(static_cast<char>(levels.size()));
    for (Byte b:levels) out.push_back(static_cast<char>(b));
    return out;
}
} // namespace SonyV2
