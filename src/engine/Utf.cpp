#include "engine/Utf.h"

#include <windows.h>

namespace zy {

std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring CodePointToWide(unsigned long cp) {
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0) || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) return {};
    if (cp < 0x10000) return std::wstring(1, static_cast<wchar_t>(cp));
    cp -= 0x10000;
    return {static_cast<wchar_t>(0xD800 + (cp >> 10)), static_cast<wchar_t>(0xDC00 + (cp & 0x3FF))};
}

std::wstring Big5ToWide(unsigned code) {
    const unsigned lead = code >> 8, trail = code & 0xFF;
    if (lead < 0x81 || lead > 0xFE || !((trail >= 0x40 && trail <= 0x7E) || (trail >= 0xA1 && trail <= 0xFE))) return {};
    const char bytes[2] = {static_cast<char>(lead), static_cast<char>(trail)};
    wchar_t out[2] = {};
    const int n = MultiByteToWideChar(950, MB_ERR_INVALID_CHARS, bytes, 2, out, 2);
    if (n != 1 || out[0] == 0 || out[0] == 0x30FB || (out[0] >= 0xE000 && out[0] <= 0xF8FF)) return {};  // unmapped / private use
    return std::wstring(1, out[0]);
}

size_t CodePointCount(std::wstring_view s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (IS_HIGH_SURROGATE(s[i]) && i + 1 < s.size() && IS_LOW_SURROGATE(s[i + 1])) ++i;
        ++count;
    }
    return count;
}

size_t OffsetOfCodePoint(std::wstring_view s, size_t n) {
    size_t i = 0;
    while (n > 0 && i < s.size()) {
        if (IS_HIGH_SURROGATE(s[i]) && i + 1 < s.size() && IS_LOW_SURROGATE(s[i + 1])) ++i;
        ++i;
        --n;
    }
    return i;
}

}  // namespace zy
