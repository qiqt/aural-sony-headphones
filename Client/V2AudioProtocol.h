#pragma once

#include "V2Protocol.h"

// Sound Connect 13.3.0 static evidence: fn0 (AUDIO), hn0 (COMMON),
// V2 FunctionType and audio/common param enums. See lab task-v2audio-*.txt.
// Only connection preference 00 and auto/off upscaling 01 are implemented.
namespace SonyV2 {
inline constexpr Byte ConnectionPreferenceFunction = 0xe1;
inline constexpr Byte UpscalingFunction = 0xe2;
inline constexpr Byte CodecIndicatorFunction = 0x12;
inline constexpr Byte UpscalingIndicatorFunction = 0x13;

struct AudioCapability {
    Byte selector = 0;
    std::optional<Byte> upscalingType; // 0 HX, 1 DSEE, 2 HX AI, 3 Ultimate
    bool operator==(const AudioCapability&) const = default;
};
struct AudioAvailability {
    Byte selector = 0;
    bool available = false;
    bool operator==(const AudioAvailability&) const = default;
};
struct AudioState {
    Byte selector = 0, value = 0; // 00: sound/stability; 01: off/auto
    bool operator==(const AudioState&) const = default;
};
struct CodecStatus {
    Byte codec = 0;
    bool operator==(const CodecStatus&) const = default;
};
struct UpscalingEffect {
    Byte type = 0, status = 0; // status: off, valid, invalid (not availability)
    bool operator==(const UpscalingEffect&) const = default;
};

inline bool audioSelector(Byte selector) { return selector == 0 || selector == 1; }
inline bool audioCapabilityValid(const AudioCapability& cap) {
    return (cap.selector == 0 && !cap.upscalingType) ||
        (cap.selector == 1 && cap.upscalingType && *cap.upscalingType <= 3);
}
// Connection selector00 has no AUDIO_RET_CAPABILITY subtype. Its two choices
// are declared by the table1 function, not by a fabricated capability reply.
inline std::optional<AudioCapability> connectionAudioCapability(const SupportedFunctions& functions) {
    if (functions.table != DATA_TYPE::DATA_MDR || !functions.supports(ConnectionPreferenceFunction)) return {};
    return AudioCapability{0,{}};
}
inline std::optional<Buffer> queryAudioCapability(Byte selector) {
    if (selector != 1) return {};
    return Buffer{static_cast<char>(0xe0),1};
}
inline Buffer queryAudioAvailability(Byte selector) { return {static_cast<char>(0xe2),static_cast<char>(selector)}; }
inline Buffer queryAudioState(Byte selector) { return {static_cast<char>(0xe6),static_cast<char>(selector)}; }
inline Buffer queryCodecStatus() { return {0x12,0x02}; }
inline Buffer queryUpscalingEffect() { return {0x12,0x03}; }

inline std::optional<AudioCapability> parseAudioCapability(const Message& m) {
    const auto& p=m.messageBytes;
    if (!table1(m) || p.size()!=3 || u(p,0)!=0xe1 || u(p,1)!=1 || u(p,2)>3) return {};
    return AudioCapability{1,u(p,2)};
}
inline std::optional<AudioAvailability> parseAudioAvailability(const Message& m) {
    const auto& p=m.messageBytes;
    if (!table1(m) || p.size()!=3 || (u(p,0)!=0xe3 && u(p,0)!=0xe5) ||
        !audioSelector(u(p,1)) || u(p,2)>1) return {};
    return AudioAvailability{u(p,1),u(p,2)==0};
}
inline std::optional<AudioState> parseAudioState(const Message& m) {
    const auto& p=m.messageBytes;
    if (!table1(m) || p.size()!=3 || (u(p,0)!=0xe7 && u(p,0)!=0xe9) ||
        !audioSelector(u(p,1)) || u(p,2)>1) return {};
    return AudioState{u(p,1),u(p,2)};
}
inline bool audioStateFits(const AudioCapability& cap,const AudioState& state) {
    return audioCapabilityValid(cap) && cap.selector==state.selector && state.value<=1;
}
// Caller must additionally require current availability and confirm by GET_PARAM.
inline std::optional<Buffer> setAudioState(const AudioCapability& cap,Byte value) {
    if (!audioStateFits(cap,AudioState{cap.selector,value})) return {};
    return Buffer{static_cast<char>(0xe8),static_cast<char>(cap.selector),static_cast<char>(value)};
}
inline std::optional<CodecStatus> parseCodecStatus(const Message& m) {
    const auto& p=m.messageBytes;
    if (!table1(m) || p.size()!=3 || (u(p,0)!=0x13 && u(p,0)!=0x15) || u(p,1)!=2) return {};
    const Byte codec=u(p,2);
    if (codec!=0 && codec!=1 && codec!=2 && codec!=0x10 && codec!=0x20 && codec!=0x21 && codec!=0x30 && codec!=0xff) return {};
    return CodecStatus{codec};
}
inline std::optional<UpscalingEffect> parseUpscalingEffect(const Message& m) {
    const auto& p=m.messageBytes;
    if (!table1(m) || p.size()!=4 || (u(p,0)!=0x13 && u(p,0)!=0x15) || u(p,1)!=3 ||
        u(p,2)>3 || u(p,3)>2) return {};
    return UpscalingEffect{u(p,2),u(p,3)};
}
} // namespace SonyV2
