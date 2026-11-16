#pragma once
#include <memory>
#include <string>
#include <vector>

#include "UserOverrideModel.h"
#include "engine/Bopomofo.h"
#include "gramambular2/reading_grid.h"

namespace zy {

// Keys the composer understands, already decoded from Win32 virtual keys by the TIP.
enum class KeyKind {
    Symbol,       // ch = Bopomofo symbol
    Tone,         // ch = tone mark
    Space,
    Enter,
    Escape,
    Backspace,
    Delete,
    Left,
    Right,
    Home,
    End,
    Up,
    Down,
    PageUp,
    PageDown,
    Digit,        // ch = L'0'..L'9' (top row with Shift, or used for candidate selection)
    Punctuation,  // ch = ASCII punctuation as typed (e.g. L'?' for Shift+/); typed as-is
    SymbolPrefix,  // the ` key: the next symbol key types its full-width symbol
    CtrlPunctuation,  // Ctrl + symbol key; ch = the ASCII symbol (L',' for Ctrl+,  L'?' for Ctrl+Shift+/)
    Tab,
    Forget,       // Ctrl+Delete in the candidate list: forget what was learned about the candidate
    SymbolKeyboard,  // Ctrl+Alt+, : open/close the symbol keyboard
    Literal,      // ch = character to type as-is (Shift+letter, numpad)
    Other,
};

struct Key {
    KeyKind kind = KeyKind::Other;
    wchar_t ch = 0;
    wchar_t ascii = 0;  // what the key types on a US keyboard, whatever its kind (0 = not printable)
};

struct ComposeResult {
    bool handled = false;  // false = the key is not ours; let the app have it
    std::wstring commit;   // text to commit to the document before showing the new composition
    bool beep = false;     // the key was a mistake (no such syllable): the TIP beeps, like Microsoft Bopomofo
};

// Learning hook. The TIP plugs in a process-wide, thread-safe implementation.
class UserModel {
public:
    virtual ~UserModel() = default;
    virtual McBopomofo::UserOverrideModel::Suggestion Suggest(
        const Formosa::Gramambular2::ReadingGrid::WalkResult& walk, size_t cursor) = 0;
    virtual void Observe(const Formosa::Gramambular2::ReadingGrid::WalkResult& before,
                         const Formosa::Gramambular2::ReadingGrid::WalkResult& after, size_t cursor) = 0;
    // The sentence was committed to the document as walked.
    virtual void Commit(const Formosa::Gramambular2::ReadingGrid::WalkResult&) {}
    // The user asked to forget what was learned about this candidate.
    virtual void Forget(const std::string& /*reading*/, const std::string& /*value*/) {}
};

// The composing buffer: Bopomofo syllables on a reading grid, walked by the language model to pick
// the most likely sentence (整句選字), plus the candidate list state.
class Composer {
public:
    // Microsoft Bopomofo has no practical limit; this only guards against runaway input (a stuck
    // key). Longer buffers auto-commit their head.
    static constexpr size_t kMaxReadings = 200;
    static constexpr size_t kCandidatesPerPage = 9;
    static constexpr size_t kExpandedColumns = 8;  // pages shown side by side in table view

    // Full-width symbol Microsoft Bopomofo inserts for Ctrl + <ASCII symbol>; 0 if none.
    static wchar_t FullWidthSymbol(wchar_t ascii);
    // Full-width form of any printable ASCII character or space (全形 mode); 0 if none.
    static wchar_t FullWidthChar(wchar_t ascii);

    // Symbol keyboard (Ctrl+Alt+,): pages of symbols, one per key, in this key order.
    static constexpr wchar_t kSymbolKeys[] = L"1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./";
    static constexpr size_t kSymbolKeyCount = sizeof(kSymbolKeys) / sizeof(wchar_t) - 1;
    static size_t SymbolPageCount();
    static std::wstring SymbolPageTitle(size_t page);
    static std::wstring SymbolOnKey(size_t page, size_t key);  // empty if the key has none

    // 全形 mode (Shift+Space): spaces, punctuation and letters are typed full-width.
    void SetFullWidth(bool on) { fullWidth_ = on; }
    bool FullWidth() const { return fullWidth_; }

    explicit Composer(std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm, UserModel* userModel = nullptr);

    ComposeResult Feed(const Key& key);
    // Whether Feed(key) would consume the key. Side-effect free (used by OnTestKeyDown).
    bool WouldHandle(const Key& key) const;

    bool Empty() const { return !Composing() && !symbolKeyboard_; }
    // Text is being composed (the symbol keyboard alone does not count).
    bool Composing() const { return grid_.length() != 0 || !reading_.Empty() || prefix_ != Prefix::None; }
    void Clear();
    // Switched to English mode while composing: the sentence stays (keys then type English into
    // it, see the TIP), but a half-typed syllable, a ` prefix and the symbol keyboard are dropped.
    void EnterEnglishMode();
    // Returns the converted text (without an unfinished syllable) and clears the buffer.
    std::wstring TakeCommitText();

    std::wstring DisplayText() const;
    size_t CaretOffset() const;  // UTF-16 offset of the caret within DisplayText()

    // Candidate list
    bool CandidatesOpen() const { return candidatesOpen_; }
    size_t CandidateCount() const { return candidates_.size(); }
    const std::wstring& CandidateText(size_t i) const { return candidates_[i].text; }
    size_t HighlightedCandidate() const { return highlighted_; }
    bool CandidatesExpanded() const { return expanded_; }  // table view (Right arrow / Tab)
    void HighlightCandidate(size_t i) {  // e.g. chosen in a game's own candidate UI (UILess)
        if (i < candidates_.size()) highlighted_ = i;
    }

    // Symbol keyboard
    bool SymbolKeyboardOpen() const { return symbolKeyboard_; }
    size_t SymbolKeyboardPage() const { return symbolPage_; }

private:
    struct CandidateItem {
        std::string reading;
        std::string value;
        std::wstring text;
        size_t start = 0;   // first reading the candidate covers
        size_t length = 0;  // number of readings it covers
    };

    enum class Prefix {
        None,
        Symbol,   // ` pressed: a symbol key, U or B follows
        Unicode,  // ` U: hex digits, then Space
        Big5,     // ` B: four hex digits
    };

    void FeedComposing(const Key& key, ComposeResult* r);
    void FeedCandidates(const Key& key, ComposeResult* r);
    void FeedPrefix(const Key& key, ComposeResult* r);
    void FeedSymbolKeyboard(const Key& key, ComposeResult* r);
    // Index into kSymbolKeys of a key that types a symbol on the keyboard; npos if none.
    static size_t SymbolKeyIndex(const Key& key);
    bool InsertReading(const std::string& reading);
    // Candidates for the caret position (see OpenCandidates), current one first.
    bool CollectCandidates(std::vector<CandidateItem>* items, bool* atEnd);
    void OpenCandidates();
    void ApplyCandidate(const CandidateItem& item);
    void SelectCandidate(size_t index);
    void CycleCandidate();  // Up with the list closed
    void PopOverflow(ComposeResult* r);
    void Walk();
    std::wstring WalkText() const;
    void TypeLiteral(wchar_t ch, ComposeResult* r);
    void TypeChars(const std::wstring& text, ComposeResult* r);  // one character, as-is
    void Layout(std::wstring* text, size_t* caret) const;

    std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm_;
    UserModel* userModel_;
    Formosa::Gramambular2::ReadingGrid grid_;
    Formosa::Gramambular2::ReadingGrid::WalkResult walk_;
    Syllable reading_;
    Prefix prefix_ = Prefix::None;
    std::wstring code_;  // hex digits typed after ` U / ` B
    bool symbolKeyboard_ = false;
    size_t symbolPage_ = 0;  // remembered between openings
    bool fullWidth_ = false;

    bool candidatesOpen_ = false;
    std::vector<CandidateItem> candidates_;
    bool expanded_ = false;
    size_t highlighted_ = 0;

    // Up pressed repeatedly with the list closed walks through these.
    bool cycling_ = false;
    std::vector<CandidateItem> cycle_;
    size_t cycleIndex_ = 0;
};

}  // namespace zy
