#include "engine/Composer.h"

#include <algorithm>
#include <unordered_set>

#include "engine/Utf.h"

using Formosa::Gramambular2::LanguageModel;
using Formosa::Gramambular2::ReadingGrid;

namespace zy {
namespace {

// Readings the composer adds on top of the dictionary (see ComposerLM).
constexpr char kLiteralPrefix[] = "_zy_lit_";  // + one character (UTF-8), typed as-is
constexpr char kCtrlPrefix[] = "_zy_ctrl_";    // + one ASCII symbol
constexpr double kBopomofoScore = -99.0;          // the Bopomofo itself: always last

// "_zy_lit_" followed by exactly one UTF-8 character. (The grid also asks about readings joined
// with '-', which must not match.)
bool IsLiteralReading(const std::string& reading) {
    const size_t n = sizeof(kLiteralPrefix) - 1;
    if (reading.size() <= n || reading.compare(0, n, kLiteralPrefix) != 0) return false;
    const unsigned char lead = static_cast<unsigned char>(reading[n]);
    const size_t len = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
    return reading.size() == n + len;
}

// One plain Bopomofo syllable such as "ㄏㄠˇ" (not a joined reading, not one of ours).
bool IsSyllableReading(const std::string& reading) {
    if (reading.empty() || reading[0] == '_' || reading.find('-') != std::string::npos) return false;
    for (wchar_t c : Utf8ToWide(reading))
        if (ClassifySymbol(c) == SymbolKind::None && c != kTone2 && c != kTone3 && c != kTone4 && c != kTone5)
            return false;
    return true;
}

bool IsCtrlReading(const std::string& reading) {
    const size_t n = sizeof(kCtrlPrefix) - 1;
    return reading.size() == n + 1 && reading.compare(0, n, kCtrlPrefix) == 0 &&
           Composer::FullWidthSymbol(static_cast<unsigned char>(reading.back())) != 0;
}

// The dictionary plus two kinds of entries of our own, so that they can sit in the composing
// buffer like any character:
//  * literal characters (space, half-width punctuation, Shift+letters typed while composing);
//  * Ctrl+symbol punctuation: Microsoft's full-width symbol first, then whatever else the
//    dictionary offers for that key as further candidates;
//  * the Bopomofo of a syllable itself ("ㄏㄠˇ"), as the very last candidate, so that Bopomofo
//    can be typed (Microsoft Bopomofo lists it at the end too). It never wins a walk.
class ComposerLM : public LanguageModel {
public:
    explicit ComposerLM(std::shared_ptr<LanguageModel> base) : base_(std::move(base)) {}

    bool hasUnigrams(const std::string& reading) override {
        if (IsLiteralReading(reading) || IsCtrlReading(reading)) return true;
        return base_->hasUnigrams(reading);
    }

    std::vector<Unigram> getUnigrams(const std::string& reading) override {
        if (IsLiteralReading(reading)) return {Unigram(reading.substr(sizeof(kLiteralPrefix) - 1), 0)};
        if (!IsCtrlReading(reading)) {
            auto out = base_->getUnigrams(reading);
            if (!out.empty() && IsSyllableReading(reading)) {
                // Last, like Microsoft Bopomofo, even where the dictionary has it as a character of
                // its own (ㄓ is listed under ㄓ, ranked among 支 之 知...).
                out.erase(std::remove_if(out.begin(), out.end(),
                                         [&](const Unigram& u) { return u.value() == reading; }),
                          out.end());
                out.emplace_back(reading, kBopomofoScore);
            }
            return out;
        }

        const char ascii = reading.back();
        std::vector<Unigram> out;
        std::unordered_set<std::string> seen;
        const wchar_t full = Composer::FullWidthSymbol(static_cast<unsigned char>(ascii));
        out.emplace_back(WideToUtf8(std::wstring(1, full)), 0.0);
        seen.insert(out.back().value());
        for (const char* prefix : {"_ctrl_punctuation_", "_punctuation_Standard_", "_punctuation_"}) {
            auto more = base_->getUnigrams(std::string(prefix) + ascii);
            std::stable_sort(more.begin(), more.end(),
                             [](const Unigram& a, const Unigram& b) { return a.score() > b.score(); });
            // Scores only fix the order here: ours first, then the alternatives as found.
            for (const auto& u : more)
                if (seen.insert(u.value()).second) out.emplace_back(u.value(), -static_cast<double>(out.size()));
        }
        return out;
    }

private:
    std::shared_ptr<LanguageModel> base_;
};

struct SymbolPage {
    const wchar_t* title;
    const wchar_t* symbols;  // one per key of Composer::kSymbolKeys; space = none
};
const SymbolPage kSymbolPages[] = {
#include "engine/SymbolTable.inc"
};
constexpr size_t kTablePages = sizeof(kSymbolPages) / sizeof(kSymbolPages[0]);
// The last page is Bopomofo, each symbol on its own key (standard layout), built on the fly.

unsigned VirtualKeyOf(wchar_t key) {
    if (key >= L'a' && key <= L'z') return key - 32;
    switch (key) {
        case L',': return 0xBC;
        case L'.': return 0xBE;
        case L';': return 0xBA;
        case L'/': return 0xBF;
        case L'-': return 0xBD;
        default: return key;
    }
}

}  // namespace

size_t Composer::SymbolPageCount() { return kTablePages + 1; }

std::wstring Composer::SymbolPageTitle(size_t page) {
    return page < kTablePages ? kSymbolPages[page].title : L"注音符號";
}

std::wstring Composer::SymbolOnKey(size_t page, size_t key) {
    if (key >= kSymbolKeyCount) return {};
    if (page < kTablePages) {
        const wchar_t c = kSymbolPages[page].symbols[key];
        return c == L' ' ? std::wstring() : std::wstring(1, c);
    }
    const unsigned vk = VirtualKeyOf(kSymbolKeys[key]);
    if (auto s = StandardLayoutSymbol(vk)) return std::wstring(1, *s);
    if (auto t = StandardLayoutTone(vk)) return std::wstring(1, *t);
    return {};
}

Composer::Composer(std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm, UserModel* userModel)
    : lm_(std::make_shared<ComposerLM>(std::move(lm))), userModel_(userModel), grid_(lm_) {}

wchar_t Composer::FullWidthSymbol(wchar_t ascii) {
    switch (ascii) {
        case L'.': return L'。';
        case L'\'': return L'、';
        case L'[': return L'「';
        case L']': return L'」';
        case L'{': return L'『';
        case L'}': return L'』';
        default: break;
    }
    if (ascii < 0x21 || ascii > 0x7E) return 0;
    const bool alnum = (ascii >= L'0' && ascii <= L'9') || (ascii >= L'A' && ascii <= L'Z') ||
                       (ascii >= L'a' && ascii <= L'z');
    if (alnum) return 0;
    return static_cast<wchar_t>(ascii - 0x21 + 0xFF01);  // ，；：？！（）…
}

wchar_t Composer::FullWidthChar(wchar_t ascii) {
    if (ascii == L' ') return L'\u3000';
    if (ascii < 0x21 || ascii > 0x7E) return 0;
    return static_cast<wchar_t>(ascii - 0x21 + 0xFF01);
}

void Composer::Clear() {
    grid_.clear();
    walk_ = {};
    reading_.Clear();
    prefix_ = Prefix::None;
    code_.clear();
    symbolKeyboard_ = false;
    cycling_ = false;
    candidatesOpen_ = false;
    expanded_ = false;
    candidates_.clear();
}

void Composer::EnterEnglishMode() {
    reading_.Clear();
    prefix_ = Prefix::None;
    code_.clear();
    symbolKeyboard_ = false;
    cycling_ = false;
}

std::wstring Composer::TakeCommitText() {
    std::wstring text = WalkText();
    if (userModel_ && grid_.length() > 0) userModel_->Commit(walk_);
    Clear();
    return text;
}

size_t Composer::SymbolKeyIndex(const Key& key) {
    switch (key.kind) {
        case KeyKind::Symbol:
        case KeyKind::Tone:
        case KeyKind::Punctuation:
        case KeyKind::Literal:
        case KeyKind::Digit:
            break;
        default:
            return std::wstring::npos;  // Space, Enter, Ctrl+symbol... are not symbol keys
    }
    wchar_t a = key.ascii;
    if (a >= L'A' && a <= L'Z') a += 32;
    if (a == 0) return std::wstring::npos;
    for (size_t i = 0; i < kSymbolKeyCount; ++i)
        if (kSymbolKeys[i] == a) return i;
    return std::wstring::npos;
}

bool Composer::WouldHandle(const Key& key) const {
    if (symbolKeyboard_) {
        // The symbol keyboard takes only its own keys. Everything else edits the sentence being
        // composed, or — with nothing composed — goes to the application as usual (Backspace,
        // Enter, arrows, Space, Ctrl+C...).
        switch (key.kind) {
            case KeyKind::SymbolKeyboard:
            case KeyKind::Escape:
            case KeyKind::PageUp:
            case KeyKind::PageDown:
                return true;
            default:
                if (SymbolKeyIndex(key) != std::wstring::npos) return true;
                break;
        }
    }
    if (candidatesOpen_ || Composing()) return true;  // while composing, every key is ours
    switch (key.kind) {
        case KeyKind::Symbol:
        case KeyKind::Tone:  // a tone key alone types the tone mark, like Microsoft Bopomofo
        case KeyKind::SymbolPrefix:
            return true;
        case KeyKind::CtrlPunctuation: return FullWidthSymbol(key.ch) != 0;
        case KeyKind::SymbolKeyboard: return true;
        // Half-width: punctuation, Shift+letters and space are not ours on an empty buffer, so the
        // application gets the key as typed. Full-width: we type the full-width form.
        case KeyKind::Punctuation:
        case KeyKind::Literal:
            return fullWidth_ && FullWidthChar(key.ch) != 0;
        case KeyKind::Space: return fullWidth_;
        // Digits etc. pass through on an empty buffer so they type normally.
        default: return false;
    }
}

ComposeResult Composer::Feed(const Key& key) {
    ComposeResult r;
    if (!WouldHandle(key)) return r;
    r.handled = true;
    if (key.kind != KeyKind::Up) cycling_ = false;
    if (key.kind == KeyKind::SymbolKeyboard) {
        // Ctrl+Alt+, opens the symbol keyboard, and closes it again.
        candidatesOpen_ = false;
        expanded_ = false;
        reading_.Clear();
        prefix_ = Prefix::None;
        symbolKeyboard_ = !symbolKeyboard_;
    } else if (symbolKeyboard_) {
        FeedSymbolKeyboard(key, &r);
    } else if (candidatesOpen_) {
        FeedCandidates(key, &r);
    } else {
        FeedComposing(key, &r);
    }
    PopOverflow(&r);
    return r;
}

void Composer::FeedComposing(const Key& key, ComposeResult* r) {
    if (prefix_ != Prefix::None) {
        FeedPrefix(key, r);
        return;
    }

    const bool hasReading = !reading_.Empty();
    switch (key.kind) {
        case KeyKind::Symbol:
            // After a rejected syllable (it keeps its tone), a symbol replaces its own slot and
            // the rest stays: ㄗㄨㄛ˙ then ㄓ gives ㄓㄨㄛ, ready for a tone again.
            if (reading_.Complete()) reading_.tone = 0;
            reading_.Put(key.ch);
            break;

        case KeyKind::SymbolPrefix:
            if (!hasReading) prefix_ = Prefix::Symbol;
            break;

        case KeyKind::Tone:
        case KeyKind::Space:
            if (reading_.HasPhonetic()) {
                reading_.tone = key.kind == KeyKind::Space ? kTone1 : key.ch;
                if (InsertReading(WideToUtf8(reading_.ToString()))) {
                    reading_.Clear();
                } else {
                    // No such syllable (ㄏㄧㄠˇ): like Microsoft Bopomofo, keep it as typed and beep,
                    // so it can be fixed (Backspace, another tone, or retyping the symbols).
                    r->beep = true;
                }
            } else if (key.kind == KeyKind::Tone) {
                // A tone key alone types the tone mark itself (ˊˇˋ˙), into the sentence.
                InsertReading(std::string(kLiteralPrefix) + WideToUtf8(std::wstring(1, key.ch)));
            } else if (key.kind == KeyKind::Space && !hasReading) {
                // Space types a space (kept in the buffer); only Down opens the candidate list.
                TypeLiteral(L' ', r);
            }
            break;

        case KeyKind::Down:
            if (!hasReading && grid_.length() > 0) OpenCandidates();
            break;

        case KeyKind::Up:
            // Like Microsoft Bopomofo: change to the next candidate without opening the list.
            if (!hasReading && grid_.length() > 0) CycleCandidate();
            break;

        case KeyKind::Enter:
            r->commit = WalkText();
            // Option (default on): Bopomofo typed on its own, with nothing else in the buffer,
            // goes out as Bopomofo (ㄓ Enter types ㄓ).
            if (enterCommitsReading_ && grid_.length() == 0 && hasReading) r->commit = reading_.ToString();
            if (userModel_ && grid_.length() > 0) userModel_->Commit(walk_);
            Clear();
            break;

        case KeyKind::Escape:
            Clear();
            break;

        case KeyKind::Backspace:
            if (hasReading) {
                reading_.Backspace();
            } else if (grid_.deleteReadingBeforeCursor()) {
                Walk();
            }
            break;

        case KeyKind::Delete:
            if (!hasReading && grid_.deleteReadingAfterCursor()) Walk();
            break;

        case KeyKind::Left:
            if (!hasReading && grid_.cursor() > 0) grid_.setCursor(grid_.cursor() - 1);
            break;
        case KeyKind::Right:
            if (!hasReading && grid_.cursor() < grid_.length()) grid_.setCursor(grid_.cursor() + 1);
            break;
        case KeyKind::Home:
            if (!hasReading) grid_.setCursor(0);
            break;
        case KeyKind::End:
            if (!hasReading) grid_.setCursor(grid_.length());
            break;

        case KeyKind::Punctuation:
        case KeyKind::Literal:
            if (!hasReading) TypeLiteral(key.ch, r);
            break;

        case KeyKind::CtrlPunctuation:
            if (!hasReading && FullWidthSymbol(key.ch) != 0)
                InsertReading(std::string(kCtrlPrefix) + static_cast<char>(key.ch));
            break;

        default:
            break;  // swallowed while composing, like Microsoft Bopomofo
    }
}

// Types a character as-is (full-width in 全形 mode). While composing it joins the buffer, so the
// sentence stays editable; otherwise it goes straight to the document.
void Composer::TypeLiteral(wchar_t ch, ComposeResult* r) {
    if (fullWidth_)
        if (wchar_t full = FullWidthChar(ch)) ch = full;
    if (ch != 0) TypeChars(std::wstring(1, ch), r);
}

void Composer::TypeChars(const std::wstring& text, ComposeResult* r) {
    if (text.empty()) return;
    if (grid_.length() == 0) {
        r->commit += text;
        return;
    }
    InsertReading(std::string(kLiteralPrefix) + WideToUtf8(text));
}

// After ` : a symbol key types its full-width symbol, U starts a Unicode code, B a Big5 code.
void Composer::FeedPrefix(const Key& key, ComposeResult* r) {
    const wchar_t a = key.ascii;
    if (prefix_ == Prefix::Symbol) {
        prefix_ = Prefix::None;  // any other key just cancels the prefix
        if (a == L'u' || a == L'U') prefix_ = Prefix::Unicode;
        else if (a == L'b' || a == L'B') prefix_ = Prefix::Big5;
        else if (FullWidthSymbol(a) != 0) InsertReading(std::string(kCtrlPrefix) + static_cast<char>(a));
        code_.clear();
        return;
    }
    const bool hex = (a >= L'0' && a <= L'9') || (a >= L'a' && a <= L'f') || (a >= L'A' && a <= L'F');
    switch (key.kind) {
        case KeyKind::Escape:
            prefix_ = Prefix::None;
            code_.clear();
            return;
        case KeyKind::Backspace:
            if (code_.empty()) prefix_ = Prefix::Symbol;  // back to just `
            else code_.pop_back();
            return;
        default:
            break;
    }
    if (prefix_ == Prefix::Unicode) {
        // ` U 4e00 Space -> 一
        if (hex && key.kind != KeyKind::Space && code_.size() < 6) {
            code_ += static_cast<wchar_t>(a >= L'a' ? a - 32 : a);
        } else if ((key.kind == KeyKind::Space || key.kind == KeyKind::Enter) && !code_.empty()) {
            const std::wstring text = CodePointToWide(std::wcstoul(code_.c_str(), nullptr, 16));
            prefix_ = Prefix::None;
            code_.clear();
            TypeChars(text, r);
        }
        return;
    }
    // ` B A440 -> 一: the fourth digit completes the code.
    if (!hex) return;
    code_ += static_cast<wchar_t>(a >= L'a' ? a - 32 : a);
    if (code_.size() < 4) return;
    const std::wstring text = Big5ToWide(static_cast<unsigned>(std::wcstoul(code_.c_str(), nullptr, 16)));
    prefix_ = Prefix::None;
    code_.clear();
    TypeChars(text, r);
}

void Composer::FeedSymbolKeyboard(const Key& key, ComposeResult* r) {
    const size_t pages = SymbolPageCount();
    switch (key.kind) {
        case KeyKind::Escape:
            symbolKeyboard_ = false;  // the sentence being composed stays
            return;
        case KeyKind::PageDown:
            symbolPage_ = (symbolPage_ + 1) % pages;
            return;
        case KeyKind::PageUp:
            symbolPage_ = (symbolPage_ + pages - 1) % pages;
            return;
        default:
            break;
    }
    // The symbol on the key; the keyboard stays open for more.
    if (const size_t i = SymbolKeyIndex(key); i != std::wstring::npos) {
        TypeChars(SymbolOnKey(symbolPage_, i), r);
        return;
    }
    // Any other key edits the sentence as usual (Backspace, arrows, Enter...).
    FeedComposing(key, r);
}

void Composer::FeedCandidates(const Key& key, ComposeResult* r) {
    const size_t count = candidates_.size();
    const size_t per = kCandidatesPerPage;
    const size_t page = highlighted_ / per;  // in table view, a page is a column
    const size_t pageCount = (count + per - 1) / per;
    // Space/PageDown turn one page in list view, one screenful of columns in table view.
    const size_t step = expanded_ ? kExpandedColumns : 1;

    switch (key.kind) {
        case KeyKind::Digit:
            // Number keys 1-9 pick from the current page/column. (The TIP sends top-row digits as
            // Digit while the list is open, even though they are Bopomofo/tone keys otherwise.)
            if (key.ch >= L'1' && key.ch <= L'9') {
                size_t index = page * per + static_cast<size_t>(key.ch - L'1');
                if (index < count) SelectCandidate(index);
            }
            break;
        case KeyKind::Symbol:
        case KeyKind::Tone:
        case KeyKind::Punctuation:
        case KeyKind::CtrlPunctuation:
        case KeyKind::SymbolPrefix:
        case KeyKind::Literal:
            // Typing on closes the list without changing the selection and keeps typing.
            candidatesOpen_ = false;
            expanded_ = false;
            FeedComposing(key, r);
            break;
        case KeyKind::Down:
            // Moving past the last row continues on the next page: this is how pages turn.
            highlighted_ = (highlighted_ + 1) % count;
            break;
        case KeyKind::Up:
            highlighted_ = (highlighted_ + count - 1) % count;
            break;
        case KeyKind::Space:
        case KeyKind::PageDown: {
            const size_t next = (page / step + 1) * step;
            highlighted_ = next < pageCount ? next * per : 0;
            break;
        }
        case KeyKind::PageUp: {
            const size_t screen = page / step;
            const size_t screens = (pageCount + step - 1) / step;
            highlighted_ = (screen > 0 ? screen - 1 : screens - 1) * step * per;
            break;
        }
        case KeyKind::Tab:
            expanded_ = !expanded_;
            break;
        case KeyKind::Right:
            // First press expands to table view; after that it moves across columns.
            if (!expanded_) expanded_ = true;
            else if (page + 1 < pageCount) highlighted_ = std::min(highlighted_ + per, count - 1);
            break;
        case KeyKind::Left:
            if (!expanded_) break;
            if (page > 0) highlighted_ -= per;
            else expanded_ = false;  // leaving the first column collapses the table
            break;
        case KeyKind::Enter:
            SelectCandidate(highlighted_);
            break;
        case KeyKind::Forget:
            // Drop what was learned about the highlighted candidate; the list closes as feedback.
            if (userModel_) userModel_->Forget(candidates_[highlighted_].reading, candidates_[highlighted_].value);
            candidatesOpen_ = false;
            expanded_ = false;
            break;
        case KeyKind::Escape:
        case KeyKind::Backspace:
            candidatesOpen_ = false;
            expanded_ = false;
            break;
        default:
            break;
    }
}

bool Composer::InsertReading(const std::string& reading) {
    if (!lm_->hasUnigrams(reading) || !grid_.insertReading(reading)) return false;
    Walk();
    if (userModel_) {
        // Apply what the user picked before in this context (user override model).
        const size_t loc = grid_.cursor() - 1;
        auto suggestion = userModel_->Suggest(walk_, loc);
        if (!suggestion.empty()) {
            auto type = suggestion.forceHighScoreOverride
                            ? ReadingGrid::Node::OverrideType::kOverrideValueWithHighScore
                            : ReadingGrid::Node::OverrideType::kOverrideValueWithScoreFromTopUnigram;
            if (grid_.overrideCandidate(loc, suggestion.candidate, type)) Walk();
        }
    }
    return true;
}

bool Composer::CollectCandidates(std::vector<CandidateItem>* out, bool* outAtEnd) {
    // Like Microsoft Bopomofo: candidates are for what follows the caret (single characters and
    // phrases starting there); with the caret at the end, for the last character.
    const size_t len = grid_.length();
    if (len == 0) return false;
    const size_t cursor = grid_.cursor();
    const bool atEnd = cursor >= len;
    const size_t start = atEnd ? len - 1 : cursor;
    const auto& span = grid_.spans()[start];
    const size_t maxLength = atEnd ? 1 : std::min(span.maxLength(), len - start);

    std::vector<CandidateItem> items;
    std::unordered_set<std::string> seen;
    for (size_t n = maxLength; n >= 1; --n) {  // phrases first, longest first
        const auto& node = span.nodeOf(n);
        if (!node) continue;
        for (const auto& u : node->unigrams()) {
            if (!seen.insert(u.value()).second) continue;
            items.push_back({node->reading(), u.value(), Utf8ToWide(u.value()), start, n});
        }
    }
    if (items.empty()) return false;

    // Like Microsoft Bopomofo, the first candidate is always what the sentence shows there now;
    // the rest keep their order (longer phrases first, each length by score, learning included).
    std::string currentValue;
    size_t currentLength = 0;
    size_t nodeStart = 0;
    for (const auto& node : walk_.nodes) {
        const size_t n = node->spanningLength();
        if (start < nodeStart + n) {
            if (nodeStart == start && n <= maxLength) {
                currentValue = node->value();
                currentLength = n;
            } else {
                // The caret is inside a word: the character shown at this position, if the word
                // has one character per reading.
                const std::wstring text = Utf8ToWide(node->value());
                if (CodePointCount(text) == n) {
                    const size_t from = OffsetOfCodePoint(text, start - nodeStart);
                    const size_t to = OffsetOfCodePoint(text, start - nodeStart + 1);
                    currentValue = WideToUtf8(text.substr(from, to - from));
                    currentLength = 1;
                }
            }
            break;
        }
        nodeStart += n;
    }
    auto current = std::find_if(items.begin(), items.end(), [&](const CandidateItem& item) {
        return item.length == currentLength && item.value == currentValue;
    });
    if (current != items.end()) std::rotate(items.begin(), current, current + 1);
    *out = std::move(items);
    *outAtEnd = atEnd;
    return true;
}

void Composer::OpenCandidates() {
    std::vector<CandidateItem> items;
    bool atEnd = false;
    if (!CollectCandidates(&items, &atEnd)) return;
    // With the caret at the end, the caret steps back in front of the last character.
    if (atEnd) grid_.setCursor(items.front().start);
    candidates_ = std::move(items);
    highlighted_ = 0;
    expanded_ = false;
    candidatesOpen_ = true;
}

void Composer::ApplyCandidate(const CandidateItem& item) {
    auto before = walk_.copyWithFixedNodes();
    if (grid_.overrideCandidate(item.start, ReadingGrid::Candidate(item.reading, item.value))) {
        Walk();
        if (userModel_) userModel_->Observe(before, walk_, item.start);
    }
}

void Composer::CycleCandidate() {
    // The list is taken once; further presses walk through it (the caret stays where it is).
    if (!cycling_) {
        bool atEnd = false;
        cycle_.clear();
        if (!CollectCandidates(&cycle_, &atEnd) || cycle_.size() < 2) return;
        cycleIndex_ = 0;
        cycling_ = true;
    }
    cycleIndex_ = (cycleIndex_ + 1) % cycle_.size();
    ApplyCandidate(cycle_[cycleIndex_]);
}

void Composer::SelectCandidate(size_t index) {
    if (index >= candidates_.size()) return;
    const CandidateItem item = candidates_[index];
    ApplyCandidate(item);
    // The caret steps over what was just chosen, ready for the next correction.
    grid_.setCursor(std::min(item.start + item.length, grid_.length()));
    candidatesOpen_ = false;
    expanded_ = false;
}

void Composer::PopOverflow(ComposeResult* r) {
    if (grid_.length() <= kMaxReadings) return;
    std::wstring committed;
    while (grid_.length() > kMaxReadings && !walk_.nodes.empty()) {
        const auto& head = walk_.nodes.front();
        const size_t n = head->spanningLength();
        committed += Utf8ToWide(head->value());
        const size_t cursor = grid_.cursor();
        grid_.setCursor(n);
        for (size_t i = 0; i < n; ++i) grid_.deleteReadingBeforeCursor();
        grid_.setCursor(cursor > n ? cursor - n : 0);
        Walk();
    }
    r->commit += committed;
}

void Composer::Walk() { walk_ = grid_.walk(); }

std::wstring Composer::WalkText() const {
    std::wstring s;
    for (const auto& node : walk_.nodes) s += Utf8ToWide(node->value());
    return s;
}

void Composer::Layout(std::wstring* text, size_t* caret) const {
    std::wstring readingText = reading_.ToString();
    switch (prefix_) {
        case Prefix::Symbol: readingText = L"｀"; break;
        case Prefix::Unicode: readingText = L"｀U" + code_; break;
        case Prefix::Big5: readingText = L"｀B" + code_; break;
        default: break;
    }
    const size_t cursor = grid_.cursor();
    std::wstring out;
    size_t caretPos = std::wstring::npos;
    size_t seen = 0;
    for (const auto& node : walk_.nodes) {
        std::wstring v = Utf8ToWide(node->value());
        const size_t len = node->spanningLength();
        if (caretPos == std::wstring::npos && cursor >= seen && cursor < seen + len) {
            // Cursor inside (or right before) this node. If each reading maps to one character,
            // put the caret between characters; otherwise keep the node intact.
            const size_t within = cursor - seen;
            const size_t cut = CodePointCount(v) == len ? OffsetOfCodePoint(v, within) : (within == 0 ? 0 : v.size());
            out += v.substr(0, cut);
            out += readingText;
            caretPos = out.size();
            out += v.substr(cut);
        } else {
            out += v;
        }
        seen += len;
    }
    if (caretPos == std::wstring::npos) {
        out += readingText;
        caretPos = out.size();
    }
    if (text) *text = std::move(out);
    if (caret) *caret = caretPos;
}

std::wstring Composer::DisplayText() const {
    std::wstring s;
    Layout(&s, nullptr);
    return s;
}

size_t Composer::CaretOffset() const {
    size_t caret = 0;
    Layout(nullptr, &caret);
    return caret;
}

}  // namespace zy
