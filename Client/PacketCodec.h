#pragma once

#include "Constants.h"
#include "Exceptions.h"
#include <cstdint>
#include <limits>
#include <span>

namespace SonyFrame
{
inline unsigned char checksum(std::span<const char> bytes)
{
    unsigned total = 0;
    for (unsigned char byte : bytes) total += byte;
    return static_cast<unsigned char>(total);
}

inline Buffer escape(std::span<const char> bytes)
{
    Buffer encoded;
    encoded.reserve(bytes.size());
    for (unsigned char byte : bytes)
    {
        const bool reserved = byte >= 0x3c && byte <= 0x3e;
        if (reserved) encoded.push_back(0x3d);
        encoded.push_back(static_cast<char>(reserved ? byte ^ 0x10 : byte));
    }
    return encoded;
}

inline Buffer unescape(std::span<const char> bytes)
{
    Buffer decoded;
    decoded.reserve(bytes.size());
    bool escaped = false;
    for (unsigned char byte : bytes)
    {
        if (escaped)
        {
            if (byte < 0x2c || byte > 0x2e)
                throw std::runtime_error("Invalid frame escape sequence");
            decoded.push_back(static_cast<char>(byte ^ 0x10));
            escaped = false;
        }
        else if (byte == 0x3d) escaped = true;
        else decoded.push_back(static_cast<char>(byte));
    }
    if (escaped) throw std::runtime_error("Incomplete frame escape sequence");
    return decoded;
}

inline Buffer encode(std::span<const char> payload, DATA_TYPE type, unsigned char sequence)
{
    if (payload.size() > MAX_BLUETOOTH_MESSAGE_SIZE)
        throw std::length_error("Bluetooth payload is too large");

    const auto size = static_cast<std::uint32_t>(payload.size());
    Buffer body{static_cast<char>(type), static_cast<char>(sequence),
        static_cast<char>(size >> 24), static_cast<char>(size >> 16),
        static_cast<char>(size >> 8), static_cast<char>(size)};
    body.insert(body.end(), payload.begin(), payload.end());
    body.push_back(static_cast<char>(checksum(body)));
    auto packet = escape(body);
    if (packet.size() + 2 > MAX_BLUETOOTH_MESSAGE_SIZE)
        throw std::runtime_error("Encoded Bluetooth frame is too large");
    packet.insert(packet.begin(), START_MARKER);
    packet.push_back(END_MARKER);
    return packet;
}

struct Decoded
{
    DATA_TYPE type;
    unsigned char sequence;
    Buffer payload;
};

// The stream reader removes the outer 0x3e/0x3c markers before calling this.
inline Decoded decode(std::span<const char> escaped)
{
    auto body = unescape(escaped);
    if (body.size() < 7) throw std::runtime_error("Bluetooth frame is shorter than its header");
    if (checksum(std::span<const char>(body).first(body.size() - 1)) != static_cast<unsigned char>(body.back()))
        throw RecoverableException("Bluetooth frame checksum mismatch", true);
    std::uint32_t size = 0;
    for (std::size_t i = 2; i != 6; ++i)
        size = (size << 8) | static_cast<unsigned char>(body[i]);
    if (size != body.size() - 7)
        throw RecoverableException("Bluetooth frame length mismatch", true);
    return {static_cast<DATA_TYPE>(body[0]), static_cast<unsigned char>(body[1]),
        Buffer(body.begin() + 6, body.end() - 1)};
}
}
