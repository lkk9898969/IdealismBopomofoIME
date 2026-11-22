#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/CtrlSymbols.h"
#include "gramambular2/language_model.h"

namespace McBopomofo {
class ParselessLM;
}

namespace zy {

// File names, shared by the IME and the settings app.
inline constexpr wchar_t kManifestFile[] = L"packs.txt";              // in the data folder
inline constexpr wchar_t kSettingsFile[] = L"settings.ini";           // in %APPDATA%\IdealismIME
inline constexpr wchar_t kUserPhrasesFile[] = L"user-phrases.txt";    //   "
inline constexpr wchar_t kExcludedPhrasesFile[] = L"excluded-phrases.txt";
inline constexpr wchar_t kPacksSection[] = L"Packs";                  // settings.ini: <pack id>=0|1
inline constexpr wchar_t kOptionsSection[] = L"Options";              // settings.ini: options below
inline constexpr wchar_t kEnterCommitsBopomofoKey[] = L"EnterCommitsBopomofo";  // 1 (default) / 0

// Options from settings.ini [Options].
struct ImeOptions {
    bool enterCommitsBopomofo = true;  // Enter with only a syllable typed types its Bopomofo
    CtrlSymbolSet ctrlSymbolsOff = ParseCtrlSymbolSet(kCtrlSymbolsOffDefault);  // Ctrl+symbol keys left to the app
};

// One line of a phrase file: "詞 ㄘˊ" (value, then the reading; syllables joined by '-' or
// spaces). In the exclusion file the reading may be left out: the word is then excluded under
// every reading. Lines starting with '#' are comments.
struct PhraseEntry {
    std::string value;
    std::string reading;  // normalized: syllables joined by '-'; may be empty
};

namespace phrase_file {
std::vector<PhraseEntry> Parse(std::string_view text);
// Writes entries one per line after the given comment lines (each without the leading "# ").
std::string Format(const std::vector<PhraseEntry>& entries, const std::vector<std::string>& comments);
// "ㄒㄧㄠˇ ㄇㄞˋ" / "ㄒㄧㄠˇ-ㄇㄞˋ" / "ㄒㄧㄠˇ，ㄇㄞˋ" -> "ㄒㄧㄠˇ-ㄇㄞˋ"; drops the first-tone mark ˉ.
std::string NormalizeReading(std::string_view reading);
}  // namespace phrase_file

// A phrase pack as listed in the manifest (packs.txt in the data folder).
struct PackInfo {
    std::string id;
    std::wstring file;
    std::string name;
    std::string description;
    bool required = false;  // cannot be turned off (the base characters)
    size_t rows = 0;
    bool enabled = true;    // per the user's settings.ini
    bool present = false;   // the file exists and has a valid header
};

// The dictionary the IME types with, in layers:
//
//   excluded phrases   never offered            (excluded-phrases.txt)
//   user phrases       always offered, on top   (user-phrases.txt)
//   phrase packs       each can be turned off   (packs.txt + one sorted file per pack)
//
// Packs are memory-mapped and binary-searched, so loading is cheap and lookups don't parse.
// Reload() runs off the UI thread and swaps in a new snapshot; lookups never wait on file I/O.
class Dictionary final : public Formosa::Gramambular2::LanguageModel {
public:
    // dataDir: folder with packs.txt and the pack files. userDir: %APPDATA%\IdealismIME (may be empty).
    Dictionary(std::wstring dataDir, std::wstring userDir);
    ~Dictionary() override;

    // (Re)loads whatever changed on disk since the last call. Blocking file I/O.
    // Returns true if a new snapshot was installed.
    bool Reload();
    // The base pack is there and usable.
    bool Loaded();

    std::vector<Unigram> getUnigrams(const std::string& reading) override;
    bool hasUnigrams(const std::string& reading) override;

    bool IsExcluded(const std::string& reading, const std::string& value);
    ImeOptions Options();
    std::vector<PackInfo> Packs();
    std::vector<PhraseEntry> UserPhrases();
    std::vector<PhraseEntry> ExcludedPhrases();

    // The usual reading of a word, character by character from the base pack (the most common
    // reading of each character). Empty if a character has none. Slow-ish (scans the pack).
    std::string GuessReading(std::string_view value);
    // Every reading the base pack knows for one character, most common first.
    std::vector<std::string> ReadingsOf(std::string_view character);
    // Every syllable of the reading exists in the base pack.
    bool ValidReading(std::string_view reading);

private:
    struct Snapshot;
    std::shared_ptr<Snapshot> Current();

    std::wstring dataDir_;
    std::wstring userDir_;
    std::mutex reloadMutex_;  // one Reload at a time
    std::mutex mutex_;        // guards snapshot_ (held only to copy the pointer)
    std::shared_ptr<Snapshot> snapshot_;
    std::string stamp_;       // what the files looked like at the last reload
};

}  // namespace zy
