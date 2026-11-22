#pragma once
// Ctrl + punctuation key types the full-width symbol (Microsoft Bopomofo). Each key can be turned
// off, so that editors keep their shortcuts (Ctrl+/ comments in VS Code...). Shared by the IME and
// the settings app.
#include <bitset>
#include <cwchar>
#include <string>

namespace zy {

struct CtrlSymbolKey {
    wchar_t ascii;  // what the key types on a US keyboard (with Shift if shift)
    wchar_t key;    // the key itself, as labelled (unshifted)
    bool shift;
    const wchar_t* name;
};

inline constexpr CtrlSymbolKey kCtrlSymbolKeys[] = {
    {L',', L',', false, L"逗號"},       {L'.', L'.', false, L"句號"},
    {L';', L';', false, L"分號"},       {L'\'', L'\'', false, L"單引號（頓號）"},
    {L'/', L'/', false, L"斜線"},       {L'\\', L'\\', false, L"反斜線"},
    {L'[', L'[', false, L"左方括號"},   {L']', L']', false, L"右方括號"},
    {L'-', L'-', false, L"減號"},       {L'=', L'=', false, L"等號"},
    {L'`', L'`', false, L"反引號"},
    {L'<', L',', true, L"小於"},        {L'>', L'.', true, L"大於"},
    {L':', L';', true, L"冒號"},        {L'"', L'\'', true, L"雙引號"},
    {L'?', L'/', true, L"問號"},        {L'|', L'\\', true, L"直線"},
    {L'{', L'[', true, L"左大括號"},    {L'}', L']', true, L"右大括號"},
    {L'_', L'-', true, L"底線"},        {L'+', L'=', true, L"加號"},
    {L'~', L'`', true, L"波浪號"},
    {L'!', L'1', true, L"驚嘆號"},      {L'@', L'2', true, L"小老鼠"},
    {L'#', L'3', true, L"井號"},        {L'$', L'4', true, L"錢號"},
    {L'%', L'5', true, L"百分比"},      {L'^', L'6', true, L"插入符號"},
    {L'&', L'7', true, L"和號"},        {L'*', L'8', true, L"星號"},
    {L'(', L'9', true, L"左括號"},      {L')', L'0', true, L"右括號"},
};

// settings.ini [Options] CtrlSymbolsOff = the keys turned off, as hex ASCII codes ("2F,3F").
// Missing: the default (Ctrl+/ off, the comment shortcut of most editors). Empty: all on.
inline constexpr wchar_t kCtrlSymbolsOffKey[] = L"CtrlSymbolsOff";
inline constexpr wchar_t kCtrlSymbolsOffDefault[] = L"2F";

using CtrlSymbolSet = std::bitset<128>;  // indexed by ASCII code

inline CtrlSymbolSet ParseCtrlSymbolSet(const std::wstring& text) {
    CtrlSymbolSet set;
    size_t i = 0;
    while (i < text.size()) {
        const size_t comma = text.find(L',', i);
        const std::wstring item = text.substr(i, comma == std::wstring::npos ? std::wstring::npos : comma - i);
        const unsigned long code = std::wcstoul(item.c_str(), nullptr, 16);
        if (code > 0x20 && code < 0x7F) set.set(code);
        if (comma == std::wstring::npos) break;
        i = comma + 1;
    }
    return set;
}

inline std::wstring FormatCtrlSymbolSet(const CtrlSymbolSet& set) {
    std::wstring out;
    for (const auto& k : kCtrlSymbolKeys) {
        if (!set.test(static_cast<size_t>(k.ascii))) continue;
        wchar_t hex[8];
        std::swprintf(hex, 8, L"%02X", static_cast<unsigned>(k.ascii));
        if (!out.empty()) out += L',';
        out += hex;
    }
    return out;
}

// "Ctrl+/" or "Ctrl+Shift+/".
inline std::wstring CtrlSymbolShortcut(const CtrlSymbolKey& k) {
    return std::wstring(k.shift ? L"Ctrl+Shift+" : L"Ctrl+") + k.key;
}

}  // namespace zy
