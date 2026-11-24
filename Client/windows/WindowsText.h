#pragma once

#include <windows.h>
#include <limits>
#include <string>

inline std::wstring windowsUtf16(const std::string& text)
{
    if (text.empty() || text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return {};
    const int length = static_cast<int>(text.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(count, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, result.data(), count) != count) return {};
    return result;
}
