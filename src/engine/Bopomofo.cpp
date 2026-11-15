#include "engine/Bopomofo.h"

namespace zy {

SymbolKind ClassifySymbol(wchar_t s) {
    if (s >= 0x3105 && s <= 0x3119) return SymbolKind::Consonant;  // ㄅ..ㄙ
    if (s >= 0x3127 && s <= 0x3129) return SymbolKind::Medial;     // ㄧㄨㄩ
    if (s >= 0x311A && s <= 0x3126) return SymbolKind::Rhyme;      // ㄚ..ㄦ
    return SymbolKind::None;
}

bool Syllable::Put(wchar_t symbol) {
    switch (ClassifySymbol(symbol)) {
        case SymbolKind::Consonant: consonant = symbol; return true;
        case SymbolKind::Medial: medial = symbol; return true;
        case SymbolKind::Rhyme: rhyme = symbol; return true;
        default: return false;
    }
}

void Syllable::Backspace() {
    if (tone) tone = 0;
    else if (rhyme) rhyme = 0;
    else if (medial) medial = 0;
    else consonant = 0;
}

std::wstring Syllable::ToString() const {
    std::wstring s;
    if (consonant) s += consonant;
    if (medial) s += medial;
    if (rhyme) s += rhyme;
    if (tone && tone != kTone1) s += tone;
    return s;
}

std::optional<wchar_t> StandardLayoutSymbol(unsigned vk) {
    switch (vk) {
        case '1': return L'ㄅ'; case 'Q': return L'ㄆ'; case 'A': return L'ㄇ'; case 'Z': return L'ㄈ';
        case '2': return L'ㄉ'; case 'W': return L'ㄊ'; case 'S': return L'ㄋ'; case 'X': return L'ㄌ';
        case 'E': return L'ㄍ'; case 'D': return L'ㄎ'; case 'C': return L'ㄏ';
        case 'R': return L'ㄐ'; case 'F': return L'ㄑ'; case 'V': return L'ㄒ';
        case '5': return L'ㄓ'; case 'T': return L'ㄔ'; case 'G': return L'ㄕ'; case 'B': return L'ㄖ';
        case 'Y': return L'ㄗ'; case 'H': return L'ㄘ'; case 'N': return L'ㄙ';
        case 'U': return L'ㄧ'; case 'J': return L'ㄨ'; case 'M': return L'ㄩ';
        case '8': return L'ㄚ'; case 'I': return L'ㄛ'; case 'K': return L'ㄜ';
        case 0xBC /*VK_OEM_COMMA*/: return L'ㄝ';
        case '9': return L'ㄞ'; case 'O': return L'ㄟ'; case 'L': return L'ㄠ';
        case 0xBE /*VK_OEM_PERIOD*/: return L'ㄡ';
        case '0': return L'ㄢ'; case 'P': return L'ㄣ';
        case 0xBA /*VK_OEM_1 ;*/: return L'ㄤ';
        case 0xBF /*VK_OEM_2 /*/: return L'ㄥ';
        case 0xBD /*VK_OEM_MINUS*/: return L'ㄦ';
        default: return std::nullopt;
    }
}

std::optional<wchar_t> StandardLayoutTone(unsigned vk) {
    switch (vk) {
        case '6': return kTone2;
        case '3': return kTone3;
        case '4': return kTone4;
        case '7': return kTone5;
        default: return std::nullopt;
    }
}

std::wstring StandardKeysToReading(std::wstring_view keys) {
    std::wstring out;
    Syllable s;
    auto flush = [&](wchar_t tone) {
        if (!s.HasPhonetic()) return;
        s.tone = tone;
        if (!out.empty()) out += L'-';
        out += s.ToString();
        s.Clear();
    };
    for (wchar_t c : keys) {
        if (c == L' ') {
            flush(kTone1);
            continue;
        }
        unsigned vk = c;
        if (c >= L'a' && c <= L'z') vk = c - 32;
        switch (c) {
            case L',': vk = 0xBC; break;
            case L'.': vk = 0xBE; break;
            case L';': vk = 0xBA; break;
            case L'/': vk = 0xBF; break;
            case L'-': vk = 0xBD; break;
            default: break;
        }
        if (auto tone = StandardLayoutTone(vk)) {
            if (!s.HasPhonetic()) return {};
            flush(*tone);
        } else if (auto symbol = StandardLayoutSymbol(vk)) {
            const SymbolKind kind = ClassifySymbol(*symbol);
            const bool taken = (kind == SymbolKind::Consonant && s.HasPhonetic()) ||
                               (kind == SymbolKind::Medial && (s.medial || s.rhyme)) ||
                               (kind == SymbolKind::Rhyme && s.rhyme);
            if (taken) flush(kTone1);
            s.Put(*symbol);
        } else {
            return {};
        }
    }
    flush(kTone1);
    return out;
}

}  // namespace zy
