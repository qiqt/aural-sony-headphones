#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace BluetoothAddress
{
inline std::uint64_t parse(std::string_view text)
{
    if (text.size() != 17 || (text[2] != ':' && text[2] != '-'))
        throw std::invalid_argument("Bluetooth address must contain six hexadecimal pairs");

    const auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::invalid_argument("Invalid hexadecimal digit in Bluetooth address");
    };
    std::uint64_t address = 0;
    for (std::size_t pair = 0; pair != 6; ++pair)
    {
        const auto offset = pair * 3;
        if (pair != 0 && text[offset - 1] != text[2])
            throw std::invalid_argument("Inconsistent Bluetooth address separator");
        address = (address << 8) | (digit(text[offset]) << 4) | digit(text[offset + 1]);
    }
    return address;
}

inline std::string format(std::uint64_t address)
{
    if (address >> 48) throw std::invalid_argument("Bluetooth address exceeds 48 bits");
    constexpr char hex[] = "0123456789abcdef";
    std::string text(17, ':');
    for (unsigned pair = 0; pair != 6; ++pair)
    {
        const auto byte = static_cast<unsigned char>(address >> ((5 - pair) * 8));
        text[pair * 3] = hex[byte >> 4];
        text[pair * 3 + 1] = hex[byte & 15];
    }
    return text;
}
}
