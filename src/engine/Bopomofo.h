#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace zy {

// A Bopomofo syllable being typed: one optional symbol per slot, like Microsoft Bopomofo.
// Typing a symbol into an occupied slot replaces it.
struct Syllable {
    wchar_t consonant = 0;  // ㄅ..ㄙ
    wchar_t medial = 0;     // ㄧㄨㄩ
    wchar_t rhyme = 0;      // ㄚ..ㄦ
    wchar_t tone = 0;       // 0 = not yet given; L' ' = 1st tone; ˊˇˋ˙

    bool Empty() const { return !consonant && !medial && !rhyme && !tone; }
    bool HasPhonetic() const { return consonant || medial || rhyme; }
    bool Complete() const { return HasPhonetic() && tone; }

    // Places a phonetic symbol (not a tone) in its slot. Returns false if not a Bopomofo symbol.
    bool Put(wchar_t symbol);
    // Removes the last slot, in reverse slot order (tone, rhyme, medial, consonant).
    void Backspace();
    void Clear() { *this = Syllable{}; }

    // Display form, e.g. "ㄓㄨㄥ" or "ㄏㄠˇ". 1st tone is shown without a mark.
    std::wstring ToString() const;
};

enum class SymbolKind { None, Consonant, Medial, Rhyme };
SymbolKind ClassifySymbol(wchar_t symbol);

// Standard (大千) keyboard layout, keyed by Win32 virtual-key code.
// Returns the phonetic symbol for a key, or nullopt.
std::optional<wchar_t> StandardLayoutSymbol(unsigned vk);
// Returns the tone mark for a tone key ('6' ˊ, '3' ˇ, '4' ˋ, '7' ˙), or nullopt. Space is handled by the caller.
std::optional<wchar_t> StandardLayoutTone(unsigned vk);

// Bopomofo for keys typed on the standard layout, e.g. "cl3" -> "ㄏㄠˇ", "su3cl3" -> "ㄋㄧˇ-ㄏㄠˇ".
// Tone keys and space end a syllable; a symbol whose slot is taken starts a new one (first tone).
// Returns an empty string if a key is not a Bopomofo key.
std::wstring StandardKeysToReading(std::wstring_view keys);

constexpr wchar_t kTone1 = L' ';
constexpr wchar_t kTone2 = 0x02CA;  // ˊ
constexpr wchar_t kTone3 = 0x02C7;  // ˇ
constexpr wchar_t kTone4 = 0x02CB;  // ˋ
constexpr wchar_t kTone5 = 0x02D9;  // ˙

}  // namespace zy
