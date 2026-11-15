#pragma once
#include <string>
#include <string_view>

namespace zy {

std::wstring Utf8ToWide(std::string_view s);
std::string WideToUtf8(std::wstring_view s);
// The character for a Unicode code point (surrogate pair above U+FFFF); empty if invalid.
std::wstring CodePointToWide(unsigned long codePoint);
// The character for a Big5 code such as 0xA440 (一); empty if the code is not a Big5 character.
std::wstring Big5ToWide(unsigned code);
// Number of Unicode code points in a UTF-16 string (surrogate pairs count once).
size_t CodePointCount(std::wstring_view s);
// UTF-16 offset of the n-th code point (clamped to the string length).
size_t OffsetOfCodePoint(std::wstring_view s, size_t n);

}  // namespace zy
