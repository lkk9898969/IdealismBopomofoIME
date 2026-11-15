#include "engine/Dictionary.h"

#include <windows.h>

#include <algorithm>
#include <limits>

#include "ParselessLM.h"
#include "ParselessPhraseDB.h"
#include "engine/Bopomofo.h"
#include "engine/Files.h"
#include "engine/Utf.h"

namespace zy {
namespace {

using Unigram = Formosa::Gramambular2::LanguageModel::Unigram;

// ㄉㄜ˙ (的): any usable base pack has it.
constexpr char kProbeReading[] = "\xE3\x84\x89\xE3\x84\x9C\xCB\x99";

std::vector<std::string_view> SplitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        pos = eol + 1;
    }
    return lines;
}

std::vector<std::string> SplitTabs(std::string_view line) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        const size_t tab = line.find('\t', pos);
        out.emplace_back(line.substr(pos, tab == std::string_view::npos ? std::string_view::npos : tab - pos));
        if (tab == std::string_view::npos) break;
        pos = tab + 1;
    }
    return out;
}

bool IsBopomofo(wchar_t c) { return (c >= 0x3105 && c <= 0x312F) || (c >= 0x31A0 && c <= 0x31BF); }

// Splits a line into whitespace-separated tokens (ASCII space/tab and the ideographic space).
std::vector<std::wstring> Tokens(const std::wstring& line) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : line) {
        if (c == L' ' || c == L'\t' || c == L'　') {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

// Readings of one character in a pack, most common first (single syllables only).
std::vector<std::string> ReadingsWith(McBopomofo::ParselessLM& base, std::string_view character) {
    std::vector<std::string> out;
    if (character.empty()) return out;
    auto found = base.getReadings(std::string(character));
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    for (const auto& r : found) {
        if (r.reading.empty() || r.reading[0] == '_' || r.reading.find('-') != std::string::npos) continue;
        if (std::find(out.begin(), out.end(), r.reading) == out.end()) out.push_back(r.reading);
    }
    return out;
}

// The most common reading of each character, joined; empty if a character has none.
std::string GuessReadingWith(McBopomofo::ParselessLM& base, std::string_view value) {
    const std::wstring w = Utf8ToWide(value);
    std::string reading;
    for (size_t i = 0; i < w.size(); ++i) {
        const size_t n = (IS_HIGH_SURROGATE(w[i]) && i + 1 < w.size()) ? 2 : 1;
        const auto readings = ReadingsWith(base, WideToUtf8(w.substr(i, n)));
        i += n - 1;
        if (readings.empty()) return {};
        if (!reading.empty()) reading += '-';
        reading += readings.front();
    }
    return reading;
}

bool HasValidHeader(const std::wstring& path) {
    const std::string head = ReadWholeFile(path).substr(0, McBopomofo::SORTED_PRAGMA_HEADER.size());
    return head == McBopomofo::SORTED_PRAGMA_HEADER;
}

}  // namespace

// ---- Phrase files ----

namespace phrase_file {

std::string NormalizeReading(std::string_view reading) {
    // Syllables are cut at separators, after a tone mark, and where a symbol cannot follow the
    // previous one in the same syllable (ㄒㄧㄠㄇㄞ -> ㄒㄧㄠ ㄇㄞ). The ideographic space is padding,
    // not a separator: Microsoft Bopomofo exports each syllable as four fixed slots,
    // e.g. "　ㄨ　ˇ ㄗ　ㄤˋ" for 武藏.
    const std::wstring in = Utf8ToWide(reading);
    std::wstring out, cur;
    int lastSlot = 0;  // 1 consonant, 2 medial, 3 rhyme
    auto flush = [&] {
        if (cur.empty()) return;
        if (!out.empty()) out += L'-';
        out += cur;
        cur.clear();
        lastSlot = 0;
    };
    auto isTone = [](wchar_t c) { return c == kTone2 || c == kTone3 || c == kTone4 || c == kTone5; };
    for (wchar_t c : in) {
        if (c == L'　' || c == L'ˉ') continue;  // padding; ˉ = first tone, written without a mark
        const bool separator = c == L' ' || c == L'\t' || c == L'-' || c == L',' || c == L'，' ||
                               c == L'、' || c == L'/' || c == L'\'';
        if (separator) {
            flush();
        } else if (isTone(c)) {
            if (!cur.empty()) {
                cur += c;
                flush();
            } else if (!out.empty() && !isTone(out.back()) && out.back() != L'-') {
                out += c;  // "ㄒㄧㄠ ˇ": the tone of the syllable before
            }
        } else {
            const SymbolKind kind = ClassifySymbol(c);
            const int slot = kind == SymbolKind::Consonant ? 1 : kind == SymbolKind::Medial ? 2
                           : kind == SymbolKind::Rhyme     ? 3 : 0;
            if (slot != 0 && slot <= lastSlot) flush();
            cur += c;  // anything else is kept, so that ValidReading rejects it
            if (slot != 0) lastSlot = slot;
        }
    }
    flush();
    return WideToUtf8(out);
}

bool IsNumber(const std::wstring& s) {
    size_t i = (!s.empty() && (s[0] == L'-' || s[0] == L'+')) ? 1 : 0;
    return i < s.size() && iswdigit(s[i]);
}

std::vector<PhraseEntry> Parse(std::string_view text) {
    std::vector<PhraseEntry> entries;
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    for (std::string_view line : SplitLines(text)) {
        const std::wstring wide = Utf8ToWide(line);
        const std::vector<std::wstring> tokens = Tokens(wide);
        if (tokens.empty() || tokens[0][0] == L'#') continue;
        // "詞 ㄘˊ" is the format; "ㄘˊ 詞" is accepted too.
        std::wstring value, rest;
        if (tokens.size() >= 2 && IsBopomofo(tokens[0][0]) && !IsBopomofo(tokens.back()[0]) && !IsNumber(tokens.back())) {
            value = tokens.back();
            rest = wide.substr(0, wide.rfind(value));
        } else {
            value = tokens[0];
            rest = wide.substr(wide.find(value) + value.size());
        }
        // A trailing number (a score from another IME's export) is not part of the reading.
        const wchar_t* ws = L" \t　";
        rest.erase(rest.find_last_not_of(ws) + 1);
        const size_t space = rest.find_last_of(L" \t");
        if (space != std::wstring::npos && IsNumber(rest.substr(space + 1)) &&
            rest.substr(0, space).find_first_not_of(ws) != std::wstring::npos)
            rest.erase(space);
        PhraseEntry e;
        e.value = WideToUtf8(value);
        e.reading = NormalizeReading(WideToUtf8(rest));
        entries.push_back(std::move(e));
    }
    return entries;
}

std::string Format(const std::vector<PhraseEntry>& entries, const std::vector<std::string>& comments) {
    std::string out;
    for (const auto& c : comments) out += "# " + c + "\n";
    for (const auto& e : entries) {
        out += e.value;
        if (!e.reading.empty()) out += " " + e.reading;
        out += "\n";
    }
    return out;
}

}  // namespace phrase_file

// ---- Dictionary ----

struct Dictionary::Snapshot {
    struct Pack {
        PackInfo info;
        std::string stamp;
        std::shared_ptr<McBopomofo::ParselessLM> lm;  // null when turned off or missing
    };
    std::vector<Pack> packs;
    std::shared_ptr<McBopomofo::ParselessLM> base;  // the required pack, for reading lookups
    std::unordered_map<std::string, std::vector<std::string>> user;  // reading -> values
    std::vector<PhraseEntry> userEntries;
    std::vector<PhraseEntry> excludedEntries;
    std::unordered_set<std::string> excludedPairs;     // reading + '\t' + value
    std::unordered_set<std::string> excludedValues;    // under every reading
    std::unordered_set<std::string> excludedReadings;  // readings with an excluded pair
    bool loaded = false;

    bool Excluded(const std::string& reading, const std::string& value) const {
        return excludedValues.count(value) != 0 || excludedPairs.count(reading + '\t' + value) != 0;
    }
};

Dictionary::Dictionary(std::wstring dataDir, std::wstring userDir)
    : dataDir_(std::move(dataDir)), userDir_(std::move(userDir)) {}

Dictionary::~Dictionary() = default;

std::shared_ptr<Dictionary::Snapshot> Dictionary::Current() {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool Dictionary::Loaded() {
    auto s = Current();
    return s && s->loaded;
}

bool Dictionary::Reload() {
    std::lock_guard<std::mutex> reload(reloadMutex_);
    const std::wstring manifestPath = dataDir_ + L"\\" + kManifestFile;
    const std::wstring settingsPath = userDir_.empty() ? std::wstring() : userDir_ + L"\\" + kSettingsFile;
    const std::wstring userPath = userDir_.empty() ? std::wstring() : userDir_ + L"\\" + kUserPhrasesFile;
    const std::wstring excludedPath = userDir_.empty() ? std::wstring() : userDir_ + L"\\" + kExcludedPhrasesFile;

    // Manifest: id, file, required, name, description, rows (tab-separated).
    std::vector<PackInfo> infos;
    const std::string manifest = ReadWholeFile(manifestPath);  // the lines below point into it
    for (std::string_view line : SplitLines(manifest)) {
        if (line.empty() || line[0] == '#') continue;
        const auto f = SplitTabs(line);
        if (f.size() < 2 || f[0].empty() || f[1].empty()) continue;
        PackInfo info;
        info.id = f[0];
        info.file = Utf8ToWide(f[1]);
        info.required = f.size() > 2 && f[2] == "1";
        info.name = f.size() > 3 ? f[3] : f[0];
        info.description = f.size() > 4 ? f[4] : std::string();
        info.rows = f.size() > 5 ? static_cast<size_t>(std::strtoull(f[5].c_str(), nullptr, 10)) : 0;
        if (info.file.find_first_of(L"\\/:") != std::wstring::npos) continue;  // stays in the data folder
        infos.push_back(std::move(info));
    }

    std::string stamp = FileStamp(manifestPath);
    if (!userDir_.empty()) stamp += FileStamp(settingsPath) + FileStamp(userPath) + FileStamp(excludedPath);
    std::vector<std::string> packStamps;
    for (const auto& info : infos) {
        packStamps.push_back(FileStamp(dataDir_ + L"\\" + info.file));
        stamp += packStamps.back();
    }
    auto old = Current();
    if (old && stamp == stamp_) return false;

    auto s = std::make_shared<Snapshot>();
    for (size_t i = 0; i < infos.size(); ++i) {
        Snapshot::Pack pack;
        pack.info = infos[i];
        pack.stamp = packStamps[i];
        const std::wstring path = dataDir_ + L"\\" + pack.info.file;
        if (!pack.info.required && !settingsPath.empty())
            pack.info.enabled =
                GetPrivateProfileIntW(kPacksSection, Utf8ToWide(pack.info.id).c_str(), 1, settingsPath.c_str()) != 0;
        pack.info.present = pack.stamp != "-" && HasValidHeader(path);
        if (pack.info.present && pack.info.enabled) {
            // Unchanged packs are reused rather than mapped again.
            if (old)
                for (const auto& p : old->packs)
                    if (p.lm && p.info.id == pack.info.id && p.stamp == pack.stamp) pack.lm = p.lm;
            if (!pack.lm) {
                auto lm = std::make_shared<McBopomofo::ParselessLM>();
                if (lm->open(WideToUtf8(path).c_str())) pack.lm = std::move(lm);
            }
        }
        if (pack.info.required && pack.lm && !s->base) s->base = pack.lm;
        s->packs.push_back(std::move(pack));
    }
    s->loaded = s->base && s->base->hasUnigrams(kProbeReading);

    if (!userDir_.empty()) {
        for (auto& e : phrase_file::Parse(DecodeText(ReadWholeFile(userPath)))) {
            // No reading given: use the usual one. (The settings app always writes one.)
            if (e.reading.empty() && s->base) e.reading = GuessReadingWith(*s->base, e.value);
            if (e.value.empty() || e.reading.empty()) continue;
            auto& values = s->user[e.reading];
            if (std::find(values.begin(), values.end(), e.value) == values.end()) values.push_back(e.value);
            s->userEntries.push_back(std::move(e));
        }
        for (auto& e : phrase_file::Parse(DecodeText(ReadWholeFile(excludedPath)))) {
            if (e.value.empty()) continue;
            if (e.reading.empty()) {
                s->excludedValues.insert(e.value);
            } else {
                s->excludedPairs.insert(e.reading + '\t' + e.value);
                s->excludedReadings.insert(e.reading);
            }
            s->excludedEntries.push_back(std::move(e));
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = std::move(s);
    }
    stamp_ = stamp;
    return true;
}

std::vector<Unigram> Dictionary::getUnigrams(const std::string& reading) {
    auto s = Current();
    std::vector<Unigram> out;
    if (!s) return out;
    std::unordered_map<std::string, size_t> index;
    auto add = [&](const std::string& value, double score) {
        auto it = index.find(value);
        if (it == index.end()) {
            index.emplace(value, out.size());
            out.emplace_back(value, score);
        } else if (out[it->second].score() < score) {
            out[it->second] = Unigram(value, score);
        }
    };
    for (const auto& pack : s->packs) {
        if (!pack.lm) continue;
        try {
            for (const auto& u : pack.lm->getUnigrams(reading)) add(u.value(), u.score());
        } catch (...) {
            // A malformed score in a pack must not take the host app down.
        }
    }
    if (auto it = s->user.find(reading); it != s->user.end()) {
        // Like McBopomofo: a user phrase of several syllables gets the top score (0), so it wins;
        // a single character only just beats the best dictionary candidate, or it would break up
        // every longer word starting with that syllable.
        double top = -std::numeric_limits<double>::infinity();
        for (const auto& u : out) top = std::max(top, u.score());
        const bool multi = reading.find('-') != std::string::npos;
        const double score = (multi || out.empty()) ? 0.0 : top + 1e-6;
        for (const auto& value : it->second) add(value, score);
    }
    if (!s->excludedValues.empty() || s->excludedReadings.count(reading)) {
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&](const Unigram& u) { return s->Excluded(reading, u.value()); }),
                  out.end());
    }
    return out;
}

bool Dictionary::hasUnigrams(const std::string& reading) {
    auto s = Current();
    if (!s) return false;
    if (!s->excludedValues.empty() || s->excludedReadings.count(reading)) return !getUnigrams(reading).empty();
    if (s->user.count(reading)) return true;
    for (const auto& pack : s->packs)
        if (pack.lm && pack.lm->hasUnigrams(reading)) return true;
    return false;
}

bool Dictionary::IsExcluded(const std::string& reading, const std::string& value) {
    auto s = Current();
    return s && s->Excluded(reading, value);
}

std::vector<PackInfo> Dictionary::Packs() {
    std::vector<PackInfo> out;
    if (auto s = Current())
        for (const auto& p : s->packs) out.push_back(p.info);
    return out;
}

std::vector<PhraseEntry> Dictionary::UserPhrases() {
    auto s = Current();
    return s ? s->userEntries : std::vector<PhraseEntry>();
}

std::vector<PhraseEntry> Dictionary::ExcludedPhrases() {
    auto s = Current();
    return s ? s->excludedEntries : std::vector<PhraseEntry>();
}

std::vector<std::string> Dictionary::ReadingsOf(std::string_view character) {
    auto s = Current();
    return s && s->base ? ReadingsWith(*s->base, character) : std::vector<std::string>();
}

std::string Dictionary::GuessReading(std::string_view value) {
    auto s = Current();
    return s && s->base ? GuessReadingWith(*s->base, value) : std::string();
}

bool Dictionary::ValidReading(std::string_view reading) {
    auto s = Current();
    if (!s || !s->base || reading.empty() || reading[0] == '_') return false;
    size_t pos = 0;
    while (pos <= reading.size()) {
        size_t dash = reading.find('-', pos);
        if (dash == std::string_view::npos) dash = reading.size();
        const std::string syllable(reading.substr(pos, dash - pos));
        if (syllable.empty() || !s->base->hasUnigrams(syllable)) return false;
        pos = dash + 1;
    }
    return true;
}

}  // namespace zy
