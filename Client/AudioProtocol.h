#pragma once

#include "CommandSerializer.h"
#include <optional>
#include <stdexcept>

// Sony V1 table-1 audio schemas. Recovered from Sound Connect 13.3.0:
// tm0/p1$c (capability), tm0/s1,r0 (availability), tm0/r1,q0 + vm0/k (mode),
// tm0/q1,p0 (codec), tm0/y2,l1 (upscaling effect). See PROTOCOL.md for evidence.
namespace SonyAudio
{
enum class ConnectionMode : unsigned char { SoundQuality = 0, ConnectionStability = 1 };
struct ConnectionCapability { unsigned char settingType = 0; };
struct DseeActivity
{
    unsigned char type = 0;   // 00 DSEE HX, 01 DSEE, 02 DSEE HX AI
    unsigned char status = 0; // 00 OFF, 01 VALID, 02 INVALID; independent of E7/E9 setting
    bool operator==(const DseeActivity&) const = default;
};

inline unsigned char byte(const Buffer& p, size_t i) { return static_cast<unsigned char>(p[i]); }
inline bool shape(const CommandSerializer::Message& m, size_t size, unsigned char a, unsigned char b)
{
    return m.dataType == DATA_TYPE::DATA_MDR && m.messageBytes.size() == size &&
        (byte(m.messageBytes, 0) == a || byte(m.messageBytes, 0) == b);
}

inline Buffer connectionCapabilityQuery() { return { static_cast<char>(0xE0), 0x01 }; }
inline Buffer connectionAvailabilityQuery() { return { static_cast<char>(0xE2), 0x01 }; }
inline Buffer connectionModeQuery() { return { static_cast<char>(0xE6), 0x01 }; }
inline Buffer codecQuery() { return { 0x18, 0x00 }; }
inline Buffer dseeActivityQuery() { return { 0x14, 0x00 }; }
inline Buffer setConnectionMode(ConnectionMode mode)
{
    const auto value = static_cast<unsigned char>(mode);
    if (value > 1) throw std::invalid_argument("Unsupported connection preference");
    return { static_cast<char>(0xE8), 0x01, 0x00, static_cast<char>(value) };
}

inline std::optional<ConnectionCapability> parseConnectionCapability(const CommandSerializer::Message& m)
{
    if (!shape(m, 3, 0xE1, 0xE1) || byte(m.messageBytes, 1) != 1 || byte(m.messageBytes, 2) != 0)
        return std::nullopt;
    return ConnectionCapability{};
}

inline std::optional<bool> parseConnectionAvailability(const CommandSerializer::Message& m)
{
    if (!shape(m, 3, 0xE3, 0xE5) || byte(m.messageBytes, 1) != 1 || byte(m.messageBytes, 2) > 1)
        return std::nullopt;
    return byte(m.messageBytes, 2) == 0;
}

inline std::optional<ConnectionMode> parseConnectionMode(const CommandSerializer::Message& m)
{
    if (!shape(m, 4, 0xE7, 0xE9) || byte(m.messageBytes, 1) != 1 ||
        byte(m.messageBytes, 2) != 0 || byte(m.messageBytes, 3) > 1)
        return std::nullopt;
    return static_cast<ConnectionMode>(byte(m.messageBytes, 3));
}

inline std::optional<unsigned char> parseCodec(const CommandSerializer::Message& m)
{
    if (!shape(m, 3, 0x19, 0x1B) || byte(m.messageBytes, 1) != 0) return std::nullopt;
    const auto code = byte(m.messageBytes, 2);
    switch (code)
    {
    case 0x00: case 0x01: case 0x02: case 0x10: case 0x20: case 0x21: case 0xFF: return code;
    default: return std::nullopt;
    }
}

inline std::optional<DseeActivity> parseDseeActivity(const CommandSerializer::Message& m)
{
    if (!shape(m, 4, 0x15, 0x17) || byte(m.messageBytes, 1) != 0 ||
        byte(m.messageBytes, 2) > 2 || byte(m.messageBytes, 3) > 2) return std::nullopt;
    return DseeActivity{ byte(m.messageBytes, 2), byte(m.messageBytes, 3) };
}
}
