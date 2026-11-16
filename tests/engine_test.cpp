// Engine tests: no framework, exits non-zero on failure.
// Usage: engine_test [path/to/data.txt]   (dictionary tests are skipped without it)
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "engine/Dictionary.h"
#include "engine/Files.h"
#include "engine/Bopomofo.h"
#include "engine/Composer.h"
#include "engine/EchoLM.h"
#include "engine/Learner.h"
#include "engine/LearningModel.h"
#include "engine/Utf.h"

using namespace zy;

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static void CheckText(const std::wstring& got, const std::wstring& want, const char* what) {
    ++g_checks;
    if (got != want) {
        std::printf("FAIL: %s\n  got:  %s\n  want: %s\n", what, WideToUtf8(got).c_str(), WideToUtf8(want).c_str());
        ++g_failures;
    }
}

// Types keys as on a standard-layout keyboard: letters/digits/,./;- as keys, ' ' space,
// '\n' Enter, '\b' Backspace, '\x1b' Esc.
static ComposeResult Type(Composer& c, const char* keys) {
    ComposeResult last;
    for (const char* p = keys; *p; ++p) {
        unsigned vk = static_cast<unsigned char>(*p);
        Key key;
        switch (vk) {
            case ' ': key = {KeyKind::Space}; break;
            case '\n': key = {KeyKind::Enter}; break;
            case '\b': key = {KeyKind::Backspace}; break;
            case 0x1b: key = {KeyKind::Escape}; break;
            default: {
                if (vk >= 'a' && vk <= 'z') vk -= 32;
                if (vk == ',') vk = 0xBC;
                else if (vk == '.') vk = 0xBE;
                else if (vk == ';') vk = 0xBA;
                else if (vk == '/') vk = 0xBF;
                else if (vk == '-') vk = 0xBD;
                if (auto s = StandardLayoutSymbol(vk)) key = {KeyKind::Symbol, *s};
                else if (auto t = StandardLayoutTone(vk)) key = {KeyKind::Tone, *t};
            }
        }
        ComposeResult r = c.Feed(key);
        if (!r.commit.empty()) last.commit += r.commit;
        last.handled = r.handled;
        last.beep = r.beep;  // of the last key
    }
    return last;
}

static ComposeResult Press(Composer& c, KeyKind kind, wchar_t ch = 0) { return c.Feed({kind, ch}); }

// Whether the open candidate list contains the text.
static bool Has(const Composer& c, const wchar_t* text) {
    for (size_t i = 0; i < c.CandidateCount(); ++i)
        if (c.CandidateText(i) == text) return true;
    return false;
}

// Picks a candidate from the open list by walking down to it and pressing Enter.
static bool Pick(Composer& c, const wchar_t* text) {
    for (size_t i = 0; i < c.CandidateCount(); ++i)
        if (c.CandidateText(i) == text) {
            for (size_t k = 0; k < i; ++k) Press(c, KeyKind::Down);
            Press(c, KeyKind::Enter);
            return true;
        }
    return false;
}

// Test double for the learning hook: a plain UserOverrideModel with a fixed clock.
class TestUserModel : public UserModel {
public:
    McBopomofo::UserOverrideModel::Suggestion Suggest(const Formosa::Gramambular2::ReadingGrid::WalkResult& w,
                                                      size_t cursor) override {
        return uom.suggest(w, cursor, 1000.0);
    }
    void Observe(const Formosa::Gramambular2::ReadingGrid::WalkResult& before,
                 const Formosa::Gramambular2::ReadingGrid::WalkResult& after, size_t cursor) override {
        uom.observe(before, after, cursor, 1000.0);
    }
    McBopomofo::UserOverrideModel uom{500, 5400.0};
};

static void TestSyllable() {
    Syllable s;
    s.Put(L'ㄅ');
    s.Put(L'ㄆ');
    CheckText(s.ToString(), L"ㄆ", "same slot replaces");
    s.Put(L'ㄧ');
    s.Put(L'ㄇ');
    CheckText(s.ToString(), L"ㄇㄧ", "consonant slot independent of order");
    s.tone = kTone3;
    CheckText(s.ToString(), L"ㄇㄧˇ", "tone mark at end");
    s.Backspace();
    CheckText(s.ToString(), L"ㄇㄧ", "backspace removes tone first");

    CheckText(StandardKeysToReading(L"cl3"), L"ㄏㄠˇ", "keys to reading");
    CheckText(StandardKeysToReading(L"su3cl3"), L"ㄋㄧˇ-ㄏㄠˇ", "keys to reading, two syllables");
    CheckText(StandardKeysToReading(L"vul3a945j4up"), L"ㄒㄧㄠˇ-ㄇㄞˋ-ㄓㄨˋ-ㄧㄣ", "last syllable in first tone");
    CheckText(StandardKeysToReading(L"clcl"), L"ㄏㄠ-ㄏㄠ", "a taken slot starts a new syllable");
    CheckText(StandardKeysToReading(L"ji3 ap6"), L"ㄨㄛˇ-ㄇㄣˊ", "space between syllables");
    CheckText(StandardKeysToReading(L"cl#"), L"", "not a Bopomofo key");
}

static void TestEcho() {
    // Without a dictionary the composer still works and shows Bopomofo (M1 behaviour).
    Composer c(std::make_shared<EchoLM>());
    Type(c, "5j/ cl3");
    CheckText(c.DisplayText(), L"ㄓㄨㄥㄏㄠˇ", "echo: syllables shown");
    auto r = Type(c, "\n");
    CheckText(r.commit, L"ㄓㄨㄥㄏㄠˇ", "echo: enter commits");
    Check(c.Empty(), "echo: empty after commit");

    Check(c.WouldHandle({KeyKind::Tone, kTone3}), "tone key alone types the tone mark");
    Check(!c.WouldHandle({KeyKind::Space}), "space passes through when empty");
    Check(!c.WouldHandle({KeyKind::Literal, L'A'}), "Shift+letter passes through when empty");
    Check(!c.WouldHandle({KeyKind::Punctuation, L'?'}), "Shift+/ passes through when empty (half-width ?)");
    Check(c.WouldHandle({KeyKind::SymbolPrefix}), "` is handled when empty");
    Check(c.WouldHandle({KeyKind::Symbol, L'ㄅ'}), "symbol starts composition");
    Check(c.WouldHandle({KeyKind::CtrlPunctuation, L'.'}), "Ctrl+. works without a dictionary");
    CheckText(std::wstring(1, Composer::FullWidthSymbol(L'<')), L"＜", "generic full-width mapping");
    Check(Composer::FullWidthSymbol(L'5') == 0 && Composer::FullWidthSymbol(L' ') == 0, "no mapping for digits/space");

    Type(c, "su3cl");
    CheckText(c.DisplayText(), L"ㄋㄧˇㄏㄠ", "echo: pending reading shown after syllable");
    Type(c, "\b");
    CheckText(c.DisplayText(), L"ㄋㄧˇㄏ", "backspace edits the pending reading");
    Type(c, "\b\b");
    Check(c.Empty(), "backspace removes reading then syllable");

    Type(c, "ji");
    Type(c, "\x1b");
    Check(c.Empty(), "escape cancels");
}

// The phrase packs in the data folder, without user files.
static std::shared_ptr<Dictionary> LoadPacks(const std::wstring& dataDir) {
    auto lm = std::make_shared<Dictionary>(dataDir, L"");
    lm->Reload();
    return lm->Loaded() ? lm : nullptr;
}

static void TestDictionary(const std::wstring& dataDir) {
    auto lm = LoadPacks(dataDir);
    if (!lm) {
        std::printf("SKIP dictionary tests: cannot load the packs\n");
        return;
    }

    {
        Composer c(lm);
        Type(c, "su3cl3");
        CheckText(c.DisplayText(), L"你好", "ㄋㄧˇㄏㄠˇ -> 你好");
        Type(c, "ji3ap6");
        CheckText(c.DisplayText(), L"你好我們", "sentence keeps growing");
        auto r = Type(c, "\n");
        CheckText(r.commit, L"你好我們", "enter commits converted text");
    }
    {
        Composer c(lm);
        Type(c, "su3cl3");
        Press(c, KeyKind::Left);
        Check(c.CaretOffset() == 1, "left moves caret between characters");
        Press(c, KeyKind::Home);
        Check(c.CaretOffset() == 0, "home moves caret to start");
        Press(c, KeyKind::End);
        Check(c.CaretOffset() == 2, "end moves caret to end");
    }
    {
        // Candidate selection with Down and digits; Esc closes without changing anything.
        Composer c(lm);
        Type(c, "su3");
        Press(c, KeyKind::Down);
        Check(c.CandidatesOpen() && c.CandidateCount() > 1, "down opens candidates");
        Press(c, KeyKind::Escape);
        Check(!c.CandidatesOpen(), "esc closes candidates");
        CheckText(c.DisplayText(), L"你", "esc keeps text");
        Check(c.CaretOffset() == 0, "the caret stayed in front of the character");
        Press(c, KeyKind::End);

        Type(c, " ");  // space types a space; it does not open the list
        Check(!c.CandidatesOpen(), "space does not open candidates");
        CheckText(c.DisplayText(), L"你 ", "space is typed into the buffer");
        Type(c, "\b");
        CheckText(c.DisplayText(), L"你", "backspace removes the space");

        Press(c, KeyKind::Down);
        size_t want = SIZE_MAX;
        for (size_t i = 0; i < c.CandidateCount() && i < 9; ++i)
            if (c.CandidateText(i) == L"妳") want = i;
        Check(want != SIZE_MAX, "妳 is on the first page for ㄋㄧˇ");
        if (want != SIZE_MAX) {
            Press(c, KeyKind::Digit, static_cast<wchar_t>(L'1' + want));
            Check(!c.CandidatesOpen(), "digit selects and closes");
            CheckText(c.DisplayText(), L"妳", "selected candidate replaces text");
        }
    }
    {
        // Candidates are for what follows the caret; picking moves the caret past the choice.
        Composer c(lm);
        Type(c, "su3cl3");  // 你好
        Press(c, KeyKind::Home);  // |你好
        Press(c, KeyKind::Down);
        Check(Has(c, L"妳") && !Has(c, L"郝"), "caret at start: candidates for the first character");
        Check(Pick(c, L"妳"), "pick 妳");
        CheckText(c.DisplayText(), L"妳好", "picking at the start keeps the rest");
        Check(c.CaretOffset() == 1, "caret moves one character forward after picking");

        Press(c, KeyKind::Down);  // 妳|好
        Check(Has(c, L"郝") && !Has(c, L"妳"), "caret in the middle: candidates for the next character");
        Check(Pick(c, L"郝"), "pick 郝");
        CheckText(c.DisplayText(), L"妳郝", "second pick");
        Check(c.CaretOffset() == 2, "caret is now at the end");
    }
    {
        // A two-character phrase moves the caret two characters forward.
        Composer c(lm);
        Type(c, "su3cl3ji3");  // 你好我
        Press(c, KeyKind::Home);
        Press(c, KeyKind::Down);
        Check(Has(c, L"你好"), "phrases starting at the caret are offered");
        CheckText(c.CandidateText(0), L"你好", "phrases come before single characters");
        Check(Pick(c, L"你好"), "pick the phrase");
        Check(c.CaretOffset() == 2, "caret moves past the two-character phrase");
        CheckText(c.DisplayText(), L"你好我", "text unchanged");
    }
    {
        // With the caret at the end, candidates are for the last character and the caret stays.
        Composer c(lm);
        Type(c, "su3cl3");
        Press(c, KeyKind::Down);
        Check(Has(c, L"郝") && !Has(c, L"妳") && !Has(c, L"你好"), "caret at end: last character only");
        Check(c.CaretOffset() == 1, "opening the list at the end moves the caret in front of the last character");
        Check(Pick(c, L"郝"), "pick 郝 at the end");
        CheckText(c.DisplayText(), L"你郝", "last character replaced");
        Check(c.CaretOffset() == 2, "after picking, the caret is at the end again");
        Press(c, KeyKind::Down);
        Press(c, KeyKind::Escape);
        Check(c.CaretOffset() == 1, "esc leaves the caret on the last character");
        Press(c, KeyKind::End);
        Press(c, KeyKind::Down);
        CheckText(c.CandidateText(c.CandidateCount() - 1), L"ㄏㄠˇ", "the Bopomofo itself is the last candidate");
        Check(Pick(c, L"ㄏㄠˇ"), "pick the Bopomofo");
        CheckText(c.DisplayText(), L"你ㄏㄠˇ", "Bopomofo can be typed");
        CheckText(Type(c, "\n").commit, L"你ㄏㄠˇ", "and committed");
        // Also for a first-tone syllable typed alone (支: ㄓ).
        Type(c, "5 ");
        Press(c, KeyKind::Down);
        CheckText(c.CandidateText(c.CandidateCount() - 1), L"ㄓ", "ㄓ is the last candidate of 支");
        Press(c, KeyKind::Escape);
        Type(c, "\x1b");
        Type(c, "su3cl3");
        CheckText(c.DisplayText(), L"你好", "the Bopomofo never wins on its own");
    }
    {
        // The first candidate is always what the sentence shows there now, like Microsoft Bopomofo.
        Composer c(lm);
        Type(c, "su3cl3");  // 你好 (one phrase)
        Press(c, KeyKind::Down);
        CheckText(c.CandidateText(0), L"好", "caret at end inside a phrase: its last character first");
        Check(c.HighlightedCandidate() == 0, "the current character is highlighted");
        Press(c, KeyKind::Escape);
        Press(c, KeyKind::Home);
        Press(c, KeyKind::Down);
        CheckText(c.CandidateText(0), L"你好", "caret at the start of a phrase: the phrase first");
        Check(Pick(c, L"妳"), "pick 妳");  // 妳|好
        Press(c, KeyKind::Home);
        Press(c, KeyKind::Down);
        CheckText(c.CandidateText(0), L"妳", "a picked character comes first next time");
        Check(Has(c, L"你") && c.CandidateText(1) != L"妳", "the others follow, without duplicates");
        Press(c, KeyKind::Escape);
        Press(c, KeyKind::End);
        Type(c, "u ");  // 妳好一
        Press(c, KeyKind::Down);
        CheckText(c.CandidateText(0), c.DisplayText().substr(2), "caret at end: the last character first");
    }
    {
        // List view: Down past the last row turns the page; Space/PageDown/PageUp page too.
        Composer c(lm);
        Type(c, "u ");  // ㄧ has many candidates
        Press(c, KeyKind::Down);
        const size_t per = Composer::kCandidatesPerPage;
        const size_t count = c.CandidateCount();
        Check(c.CandidatesOpen() && count > per * 3, "ㄧ has several pages");
        for (size_t i = 0; i < per; ++i) Press(c, KeyKind::Down);
        Check(c.HighlightedCandidate() == per, "down past the last row goes to the next page");
        Press(c, KeyKind::Up);
        Check(c.HighlightedCandidate() == per - 1, "up goes back to the previous page");
        Type(c, " ");
        Check(c.CandidatesOpen() && c.HighlightedCandidate() == per, "space pages down inside the list");
        Press(c, KeyKind::PageUp);
        Check(c.HighlightedCandidate() == 0, "page up goes back");
        Press(c, KeyKind::Left);
        Check(c.CandidatesOpen() && c.HighlightedCandidate() == 0 && !c.CandidatesExpanded(),
              "left does nothing in list view");

        // Table view: Right expands, then moves across columns; Left from the first column collapses.
        Press(c, KeyKind::Right);
        Check(c.CandidatesExpanded() && c.HighlightedCandidate() == 0, "right expands to table view");
        Press(c, KeyKind::Down);
        Press(c, KeyKind::Right);
        Check(c.HighlightedCandidate() == per + 1, "right moves to the same row of the next column");
        Press(c, KeyKind::Digit, L'3');
        Check(!c.CandidatesOpen() && !c.CandidatesExpanded(), "digit picks from the focused column");

        Press(c, KeyKind::Down);
        Check(!c.CandidatesExpanded(), "list opens collapsed again");
        Press(c, KeyKind::Tab);
        Check(c.CandidatesExpanded(), "tab expands too");
        Press(c, KeyKind::PageDown);
        const size_t pages = (count + per - 1) / per;
        const size_t screen = Composer::kExpandedColumns;
        Check(c.HighlightedCandidate() == (screen < pages ? screen * per : 0),
              "page down moves a screenful of columns");
        Press(c, KeyKind::PageUp);
        Press(c, KeyKind::Left);
        Check(!c.CandidatesExpanded() && c.CandidatesOpen(), "left from the first column collapses");
        Press(c, KeyKind::Escape);
        Check(!c.CandidatesOpen(), "esc closes");
    }
    {
        // Learning: after picking 妳 for ㄋㄧˇ, typing ㄋㄧˇ again suggests 妳.
        TestUserModel user;
        Composer c(lm, &user);
        Type(c, "su3");
        CheckText(c.DisplayText(), L"你", "before learning: 你");
        Press(c, KeyKind::Down);
        for (size_t i = 0; i < c.CandidateCount(); ++i)
            if (c.CandidateText(i) == L"妳") {
                for (size_t k = 0; k < i; ++k) Press(c, KeyKind::Down);
                Press(c, KeyKind::Enter);
                break;
            }
        CheckText(c.DisplayText(), L"妳", "enter selects highlighted candidate");
        Type(c, "\n");
        Type(c, "su3");
        CheckText(c.DisplayText(), L"妳", "after learning: 妳 is suggested");
    }
    {
        // Up with the list closed: the next candidate in place, the caret stays.
        Composer c(lm);
        Type(c, "su3");  // 你
        Press(c, KeyKind::Down);
        const std::wstring second = c.CandidateText(1), third = c.CandidateText(2);
        Press(c, KeyKind::Escape);
        Press(c, KeyKind::End);
        Press(c, KeyKind::Up);
        Check(!c.CandidatesOpen(), "up does not open the list");
        CheckText(c.DisplayText(), second, "up changes to the next candidate");
        Check(c.CaretOffset() == 1, "the caret stays at the end");
        Press(c, KeyKind::Up);
        CheckText(c.DisplayText(), third, "up again: the one after");
        Type(c, "cl3");
        CheckText(c.DisplayText().substr(0, 1), third, "typing on keeps the choice");
    }
    {
        // ` U <hex> Space and ` B <four hex digits>, typed on Bopomofo keys (key.ascii).
        Composer c(lm);
        auto key = [&](wchar_t ascii) {
            const unsigned vk = (ascii >= L'a' && ascii <= L'z') ? ascii - 32 : ascii;
            Key k{KeyKind::Other, 0, ascii};
            if (auto sym = StandardLayoutSymbol(vk)) k = {KeyKind::Symbol, *sym, ascii};
            else if (auto tone = StandardLayoutTone(vk)) k = {KeyKind::Tone, *tone, ascii};
            return c.Feed(k);
        };
        Press(c, KeyKind::SymbolPrefix);
        key(L'u');
        CheckText(c.DisplayText(), L"｀U", "` U shows the prefix");
        for (wchar_t ch : std::wstring(L"4e00")) key(ch);
        CheckText(c.DisplayText(), L"｀U4E00", "hex digits are shown");
        auto r = Press(c, KeyKind::Space);
        CheckText(r.commit, L"一", "` U 4e00 Space -> 一 (committed straight away on an empty buffer)");
        Check(c.Empty(), "nothing left");

        Type(c, "su3");
        Press(c, KeyKind::SymbolPrefix);
        key(L'u');
        for (wchar_t ch : std::wstring(L"20000")) key(ch);
        Press(c, KeyKind::Space);
        CheckText(c.DisplayText(), L"你\U00020000", "a code point above U+FFFF joins the buffer");

        Press(c, KeyKind::SymbolPrefix);
        key(L'b');
        for (wchar_t ch : std::wstring(L"a44")) key(ch);
        CheckText(c.DisplayText(), L"你\U00020000｀BA44", "` B shows the digits so far");
        key(L'0');
        CheckText(c.DisplayText(), L"你\U00020000一", "` B A440 -> 一 on the fourth digit");

        Press(c, KeyKind::SymbolPrefix);
        key(L'u');
        key(L'4');
        Press(c, KeyKind::Backspace);
        Press(c, KeyKind::Backspace);
        CheckText(c.DisplayText(), L"你\U00020000一｀", "backspace removes digits, then the U");
        Press(c, KeyKind::Escape);
        CheckText(c.DisplayText(), L"你\U00020000一", "esc cancels");
        Press(c, KeyKind::SymbolPrefix);
        key(L'b');
        for (wchar_t ch : std::wstring(L"0000")) key(ch);
        CheckText(c.DisplayText(), L"你\U00020000一", "an invalid Big5 code types nothing");
    }
    {
        // Symbol keyboard (Ctrl+Alt+,).
        Composer c(lm);
        Check(c.WouldHandle({KeyKind::SymbolKeyboard}), "Ctrl+Alt+, is ours on an empty buffer");
        Press(c, KeyKind::SymbolKeyboard);
        Check(c.SymbolKeyboardOpen() && !c.Empty(), "symbol keyboard opens");
        CheckText(c.DisplayText(), L"", "nothing is put into the text");
        Check(!c.WouldHandle({KeyKind::Backspace}) && !c.WouldHandle({KeyKind::Enter}) &&
                  !c.WouldHandle({KeyKind::Space}) && !c.WouldHandle({KeyKind::Left}),
              "with nothing composed, Backspace/Enter/Space/arrows go to the application");
        CheckText(Composer::SymbolPageTitle(0), L"標點", "first page");
        auto r = c.Feed({KeyKind::Symbol, L'ㄅ', L'1'});
        CheckText(r.commit, Composer::SymbolOnKey(0, 0), "a key types its symbol");
        Check(c.SymbolKeyboardOpen(), "and the keyboard stays open");
        Press(c, KeyKind::PageDown);
        Check(c.SymbolKeyboardPage() == 1, "page down");
        Press(c, KeyKind::PageUp);
        Press(c, KeyKind::PageUp);
        Check(c.SymbolKeyboardPage() == Composer::SymbolPageCount() - 1, "pages wrap around");
        CheckText(Composer::SymbolPageTitle(c.SymbolKeyboardPage()), L"注音符號", "last page is Bopomofo");
        CheckText(Composer::SymbolOnKey(c.SymbolKeyboardPage(), 0), L"ㄅ", "Bopomofo on its own key (1 = ㄅ)");
        CheckText(Composer::SymbolOnKey(c.SymbolKeyboardPage(), 2), L"ˇ", "tone on its key (3 = ˇ)");
        Press(c, KeyKind::Escape);
        Check(!c.SymbolKeyboardOpen() && c.Empty(), "esc closes it");

        Type(c, "su3");
        Press(c, KeyKind::SymbolKeyboard);
        c.Feed({KeyKind::Symbol, L'ㄆ', L'q'});
        CheckText(c.DisplayText(), L"你" + Composer::SymbolOnKey(c.SymbolKeyboardPage(), 12), "while composing, symbols join the buffer");
        Check(c.WouldHandle({KeyKind::Backspace}), "while composing, Backspace edits the sentence");
        Press(c, KeyKind::Backspace);
        CheckText(c.DisplayText(), L"你", "backspace removes the symbol");
        Check(c.SymbolKeyboardOpen(), "and the keyboard stays open");
        Press(c, KeyKind::SymbolKeyboard);
        Check(!c.SymbolKeyboardOpen(), "Ctrl+Alt+, again closes it");
        for (size_t page = 0; page < Composer::SymbolPageCount(); ++page) {
            size_t n = 0;
            for (size_t k = 0; k < Composer::kSymbolKeyCount; ++k) n += !Composer::SymbolOnKey(page, k).empty();
            Check(n >= 30, "every page has symbols");
        }
    }
    {
        // Shift+symbol and other punctuation keys type half-width, as on the key.
        Composer c(lm);
        Check(!c.WouldHandle({KeyKind::Punctuation, L'<'}), "Shift+, is not ours on an empty buffer");
        Type(c, "su3cl3");
        Press(c, KeyKind::Punctuation, L'?');
        Press(c, KeyKind::Punctuation, L'[');
        Press(c, KeyKind::Literal, L'A');
        CheckText(c.DisplayText(), L"你好?[A", "half-width punctuation and letters join the buffer");
        Type(c, "\b\b");
        CheckText(c.DisplayText(), L"你好?", "they can be deleted like any character");
        auto r = Type(c, "\n");
        CheckText(r.commit, L"你好?", "and commit with the sentence");
    }
    {
        // ` + symbol key: full-width symbol, also for keys that are Bopomofo keys (, . / ; -).
        Composer c(lm);
        Press(c, KeyKind::SymbolPrefix);
        Check(!c.Empty(), "` starts composing");
        CheckText(c.DisplayText(), L"｀", "` is shown while waiting for the symbol key");
        c.Feed({KeyKind::Symbol, L'ㄝ', L','});
        CheckText(c.DisplayText(), L"，", "` , -> ，");
        Press(c, KeyKind::SymbolPrefix);
        c.Feed({KeyKind::Punctuation, L'?', L'?'});
        CheckText(c.DisplayText(), L"，？", "` Shift+/ -> ？");
        Press(c, KeyKind::SymbolPrefix);
        c.Feed({KeyKind::Symbol, L'ㄅ', L'1'});
        CheckText(c.DisplayText(), L"，？", "` + non-symbol key cancels the prefix");
        Press(c, KeyKind::SymbolPrefix);
        Press(c, KeyKind::Escape);
        CheckText(c.DisplayText(), L"，？", "esc cancels the prefix only");
        Type(c, "\b\b");
        Press(c, KeyKind::SymbolPrefix);
        Press(c, KeyKind::Backspace);
        Check(c.Empty(), "cancelling the prefix on an empty buffer leaves nothing");
    }
    {
        // 全形 mode: space, punctuation and letters are typed full-width, composing or not.
        Composer c(lm);
        c.SetFullWidth(true);
        Check(c.WouldHandle({KeyKind::Space}) && c.WouldHandle({KeyKind::Punctuation, L'?'}) &&
                  c.WouldHandle({KeyKind::Literal, L'A'}),
              "full-width: space/punctuation/letters handled on empty buffer");
        auto r = Press(c, KeyKind::Punctuation, L'?');
        CheckText(r.commit, L"？", "full-width ? typed directly");
        r = Type(c, " ");
        CheckText(r.commit, L"\u3000", "full-width space typed directly");
        Check(c.Empty(), "nothing left composing");
        Type(c, "su3 ");
        Press(c, KeyKind::Literal, L'A');
        CheckText(c.DisplayText(), L"你\u3000Ａ", "full-width inside the buffer");
        c.SetFullWidth(false);
        Type(c, " ");
        CheckText(c.DisplayText(), L"你\u3000Ａ ", "back to half-width");
    }
    {
        // Ctrl + symbol: Microsoft Bopomofo's full-width punctuation.
        Composer c(lm);
        Check(c.WouldHandle({KeyKind::CtrlPunctuation, L','}), "Ctrl+, handled on empty buffer");
        Check(!c.WouldHandle({KeyKind::CtrlPunctuation, L'c'}), "Ctrl+letter is not punctuation");
        Type(c, "su3cl3");
        for (wchar_t ch : {L',', L'.', L'\'', L';', L':', L'?', L'!', L'[', L']', L'(', L')'})
            Press(c, KeyKind::CtrlPunctuation, ch);
        CheckText(c.DisplayText(), L"你好，。、；：？！「」（）", "Ctrl+symbol punctuation");
        Press(c, KeyKind::Down);
        Check(c.CandidatesOpen() && c.CandidateText(0) == L"）", "punctuation has candidates too");
        Press(c, KeyKind::Escape);
        auto r = Type(c, "\n");
        CheckText(r.commit, L"你好，。、；：？！「」（）", "punctuation commits with the sentence");
    }
    {
        // A letter between two characters keeps the sentence conversion on both sides.
        Composer c(lm);
        Type(c, "su3");
        Press(c, KeyKind::Literal, L'B');
        Type(c, "cl3");
        auto r = Type(c, "\n");
        CheckText(r.commit, L"你B好", "literal in the middle of a sentence");
        Check(c.Empty(), "empty after commit");
    }
    {
        // No such syllable: kept as typed with a beep (like Microsoft Bopomofo), not dropped.
        Composer c(lm);
        Type(c, "284ru8 ");  // 大家
        CheckText(c.DisplayText(), L"大家", "before the mistake");
        auto r = Type(c, "cul3");  // ㄏㄧㄠˇ does not exist
        Check(r.beep, "invalid syllable beeps");
        CheckText(c.DisplayText(), L"大家ㄏㄧㄠˇ", "invalid syllable stays, and so does the sentence");
        Check(r.commit.empty(), "nothing is committed");
        // Fixed by typing the wrong symbol again: it replaces its slot, the rest stays.
        r = Type(c, "v");  // ㄒ
        CheckText(c.DisplayText(), L"大家ㄒㄧㄠ", "ㄏ replaced by ㄒ, ㄧㄠ kept, tone cleared");
        r = Type(c, "3");
        Check(!r.beep, "corrected syllable is accepted");
        CheckText(c.DisplayText(), L"大家小", "corrected syllable replaces the mistake");
        CheckText(Type(c, "\n").commit, L"大家小", "commit after the correction");
    }
    {
        // Enter with only Bopomofo typed types it (option, on by default).
        Composer c(lm);
        CheckText(Type(c, "5\n").commit, L"ㄓ", "ㄓ Enter types ㄓ");
        Check(c.Empty(), "and the buffer is empty");
        CheckText(Type(c, "cul\n").commit, L"ㄏㄧㄠ", "any Bopomofo, even one that is no syllable");
        CheckText(Type(c, "cl35\n").commit, L"好", "not with other text in the buffer: the syllable is dropped");
        c.SetEnterCommitsReading(false);
        CheckText(Type(c, "5\n").commit, L"", "option off: nothing is typed");
        Check(c.Empty(), "option off: buffer cleared");
    }
    {
        // Fixed with Backspace (removes the tone first) or with another tone.
        Composer c(lm);
        Check(Type(c, "1j/ ").beep, "ㄅㄨㄥ beeps");
        CheckText(c.DisplayText(), L"ㄅㄨㄥ", "kept (1st tone has no mark)");
        Type(c, "\b\b");
        CheckText(c.DisplayText(), L"ㄅㄨ", "Backspace removes the tone, then ㄥ");
        Check(!Type(c, "3").beep, "ㄅㄨˇ exists");
        CheckText(c.DisplayText(), L"補", "now a character");
        Check(Type(c, "su7").beep, "ㄋㄧ˙ beeps");
        Check(!Type(c, "3").beep, "another tone retries");
        CheckText(c.DisplayText(), L"補你", "ㄋㄧˇ");
        Type(c, "cul3\x1b");
        Check(c.Empty(), "Esc clears everything, the mistake included");
    }
    {
        // A tone key alone types the tone mark (also on an empty buffer).
        Composer c(lm);
        Check(c.WouldHandle({KeyKind::Tone, kTone3}), "tone key is ours on an empty buffer");
        Type(c, "3");
        CheckText(c.DisplayText(), L"ˇ", "tone mark alone");
        Type(c, "cl3467");
        CheckText(c.DisplayText(), L"ˇ好ˋˊ˙", "tone marks after a syllable");
        CheckText(Type(c, "\n").commit, L"ˇ好ˋˊ˙", "tone marks are committed");
    }
    {
        // Long sentences are not cut short (only a runaway buffer is).
        Composer c(lm);
        std::wstring committed;
        for (int i = 0; i < 60; ++i) committed += Type(c, "ji3").commit;
        Check(committed.empty(), "60 syllables stay in the buffer");
        CheckText(std::to_wstring(c.DisplayText().size()), L"60", "all of them shown");
        for (size_t i = 60; i < Composer::kMaxReadings + 5; ++i) committed += Type(c, "ji3").commit;
        Check(!committed.empty(), "a runaway buffer commits its head");
        Check(c.DisplayText().size() <= Composer::kMaxReadings, "buffer stays within the limit");
    }
    {
        // English mode while composing: the sentence stays, typing goes into it.
        Composer c(lm);
        Type(c, "284ru8 2");  // 大家 + half-typed ㄉ
        c.EnterEnglishMode();
        CheckText(c.DisplayText(), L"大家", "half-typed syllable dropped, sentence kept");
        Check(!c.Empty(), "still composing");
        for (wchar_t ch : std::wstring(L"Hi")) c.Feed({KeyKind::Literal, ch});
        CheckText(c.DisplayText(), L"大家Hi", "English typed into the sentence");
        Press(c, KeyKind::Home);
        Press(c, KeyKind::Down);
        Check(c.CandidatesOpen() && Has(c, L"大"), "Chinese part still has candidates");
        Press(c, KeyKind::Escape);
        CheckText(Press(c, KeyKind::Enter).commit, L"大家Hi", "commit all");
    }
}

static bool Near(double a, double b, double tolerance = 1e-6) { return std::fabs(a - b) <= tolerance; }

static void TestLearningModel() {
    const double day = 24 * 3600.0;
    const double t0 = 1'700'000'000;
    const std::string ni = WideToUtf8(L"ㄋㄧˇ"), you = WideToUtf8(L"妳");

    // Journal lines.
    LearnEvent e;
    Check(LearningModel::ParseEvent("P\t1700000000\t" + ni + "\t" + you + "\r\n", &e) && e.type == 'P' &&
              e.time == t0 && e.reading == ni && e.value == you,
          "parse a pick line");
    Check(LearningModel::FormatEvent(e) == "P\t1700000000\t" + ni + "\t" + you + "\n", "format a pick line");
    Check(LearningModel::ParseEvent("A\t5\t" + ni + "\t" + you + "\t2.500", &e) && e.count == 2.5,
          "parse an aggregate line");
    for (const char* bad : {"", "# comment", "P\tabc\tx\ty", "P\t1\t_punctuation_x\ty", "X\t1\tx\ty", "P\t1\tx",
                            "A\t1\tx\ty", "P\t1\tx\ty\t3", "P\t-1\tx\ty"})
        Check(!LearningModel::ParseEvent(bad, &e), "reject a malformed line");

    // Pick counts: grow, saturate, decay.
    LearningModel m;
    Check(m.Boost(ni, you, t0) == 0, "no boost before any pick");
    m.Apply({'P', t0, ni, you, 0});
    const double one = m.Boost(ni, you, t0);
    Check(Near(one, LearningModel::kBoostScale), "one pick: boost = scale * log2(2)");
    m.Apply({'P', t0, ni, you, 0});
    m.Apply({'P', t0, ni, you, 0});
    Check(m.Boost(ni, you, t0) > one, "more picks, more boost");
    Check(Near(m.Boost(ni, you, t0 + 30 * day), LearningModel::kBoostScale * std::log2(1 + 1.5)),
          "half-life of 30 days");
    Check(m.Boost(ni, you, t0 - day) == m.Boost(ni, you, t0), "a clock running backwards does not inflate");
    for (int i = 0; i < 200; ++i) m.Apply({'P', t0, ni, you, 0});
    Check(m.Boost(ni, you, t0) == LearningModel::kMaxBoost, "boost is capped");
    Check(m.Boost(ni, WideToUtf8(L"你"), t0) == 0, "other values are not boosted");

    // Phrases.
    const std::string r = WideToUtf8(L"ㄋㄧˇ-ㄏㄠˇ"), v = WideToUtf8(L"妳郝");
    for (int i = 0; i < 3; ++i) m.Apply({'N', t0, r, v, 0});
    Check(m.ObservationCount(r, v) == 3 && !m.IsLearnedPhrase(r, v), "observations alone do not learn");
    m.Apply({'L', t0, r, v, 0});
    Check(m.IsLearnedPhrase(r, v) && m.HasLearnedPhrases(r) && m.LearnedPhrases(r).size() == 1, "learned phrase");

    // A snapshot replays to the same state.
    LearningModel copy;
    for (const auto& ev : m.Snapshot(t0)) {
        LearnEvent parsed;
        Check(LearningModel::ParseEvent(LearningModel::FormatEvent(ev), &parsed), "snapshot lines parse");
        copy.Apply(parsed);
    }
    Check(Near(copy.Boost(ni, you, t0 + day), m.Boost(ni, you, t0 + day), 1e-3) && copy.IsLearnedPhrase(r, v) &&
              copy.ObservationCount(r, v) == 3,
          "snapshot rebuilds the model");
    Check(m.Snapshot(t0 + 3650 * day).size() == 2, "old pick counts are dropped; learned phrases stay");

    m.Apply({'F', t0, r, v, 0});
    m.Apply({'F', t0, ni, you, 0});
    Check(!m.IsLearnedPhrase(r, v) && m.ObservationCount(r, v) == 0 && m.Boost(ni, you, t0) == 0, "forget");
    Check(m.Snapshot(t0).empty(), "nothing left after forgetting");
}

static std::wstring TempDir() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    wchar_t name[64];
    swprintf_s(name, L"idealism_test_%lu_%llu", GetCurrentProcessId(), GetTickCount64());
    std::wstring dir = std::wstring(temp) + name;
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static std::string ReadFileBytes(const std::wstring& path) {
    std::string data;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return data;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(f, buf, sizeof(buf), &n, nullptr) && n) data.append(buf, n);
    CloseHandle(f);
    return data;
}

static double g_now = 1'700'000'000;

static std::unique_ptr<Learner> MakeLearner(std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm,
                                            const std::wstring& journal) {
    auto learner = std::make_unique<Learner>(std::move(lm), journal);
    learner->SetClock([] { return g_now; });
    learner->Load();
    return learner;
}

// Types 你好 (ㄋㄧˇㄏㄠˇ), changes it to 妳郝 by picking each character, and commits.
static void TypeNiHaoAsYouHao(Composer& c) {
    Type(c, "su3cl3");
    Press(c, KeyKind::Home);
    Press(c, KeyKind::Down);
    Pick(c, L"妳");
    Press(c, KeyKind::Down);
    Pick(c, L"郝");
    Type(c, "\n");
}

static size_t CandidateIndex(Composer& c, const wchar_t* text) {
    for (size_t i = 0; i < c.CandidateCount(); ++i)
        if (c.CandidateText(i) == text) return i;
    return SIZE_MAX;
}

static void TestLearner(const std::wstring& dataDir) {
    auto lm = LoadPacks(dataDir);
    if (!lm) return;
    const std::wstring dir = TempDir();
    const std::string ni = WideToUtf8(L"ㄋㄧˇ"), you = WideToUtf8(L"妳");

    {
        // A: picked characters move forward, are remembered across programs, and become the default.
        const std::wstring journal = dir + L"\\picks.txt";
        auto a = MakeLearner(lm, journal);
        size_t before = SIZE_MAX;
        for (int i = 0; i < 5; ++i) {
            Composer c(a->LanguageModel(), a.get());
            Type(c, "su3");
            Press(c, KeyKind::Down);
            if (i == 0) before = CandidateIndex(c, L"妳");
            Check(Pick(c, L"妳"), "pick 妳");
            Type(c, "\n");
        }
        Check(before != SIZE_MAX && before > 0, "妳 is not the first candidate to begin with");
        Check(Near(a->Boost(ni, you), LearningModel::kBoostScale * std::log2(6.0)), "five commits counted");
        Check(a->PendingCount() == 5, "events wait for the next sync");
        a->Sync();
        Check(a->PendingCount() == 0, "sync writes them out");
        Check(ReadFileBytes(journal).rfind("# IdealismIME learning v1", 0) == 0, "journal starts with its header");

        // Another program (a new Learner on the same journal, with no short-term memory of its own).
        auto b = MakeLearner(lm, journal);
        Check(Near(b->Boost(ni, you), a->Boost(ni, you)), "another program sees the same counts");
        {
            Composer c(b->LanguageModel(), b.get());
            Type(c, "su3");
            CheckText(c.DisplayText(), L"妳", "often-picked character became the default");
            Press(c, KeyKind::Down);
            Check(CandidateIndex(c, L"妳") == 0, "and is first in the list");
            Press(c, KeyKind::Escape);
            Press(c, KeyKind::End);  // (opening the list stepped the caret back)
            Type(c, "cl3");
            CheckText(c.DisplayText(), L"你好", "dictionary phrases still win over single characters");
            Type(c, "\n");
        }
        // Accepting the default counts as well, for readings that have counts.
        {
            Composer c(b->LanguageModel(), b.get());
            Type(c, "su3\n");
        }
        Check(b->Boost(ni, you) > a->Boost(ni, you), "accepted default is counted");
        b->Sync();
        a->Sync();
        Check(Near(a->Boost(ni, you), b->Boost(ni, you)), "the first program catches up on the next sync");

        // A journal written before the rename (old header line) is read the same way.
        {
            std::string old = ReadFileBytes(journal);
            const std::string prefix = "# IdealismIME";
            Check(old.rfind(prefix, 0) == 0, "journal header to rewrite");
            old.replace(0, prefix.size(), "# ZhuyinIME");
            const std::wstring legacy = dir + L"\\legacy.txt";
            WriteFileReplace(legacy, old);
            auto c = MakeLearner(lm, legacy);
            Check(Near(c->Boost(ni, you), a->Boost(ni, you)), "journal from before the rename is read");
        }

        // Time passes: the habit fades.
        g_now += 365 * 24 * 3600.0;
        Check(a->Boost(ni, you) < 0.01, "a year later the boost is gone");
        g_now -= 365 * 24 * 3600.0;
    }
    {
        // C: a run of characters put together three times becomes a phrase, permanently.
        const std::wstring journal = dir + L"\\phrases.txt";
        const std::string reading = WideToUtf8(L"ㄋㄧˇ-ㄏㄠˇ"), value = WideToUtf8(L"妳郝");
        auto a = MakeLearner(lm, journal);
        for (int i = 0; i < 2; ++i) {
            Composer c(a->LanguageModel(), a.get());
            TypeNiHaoAsYouHao(c);
        }
        Check(!a->IsLearnedPhrase(reading, value), "twice is not enough");
        {
            Composer c(a->LanguageModel(), a.get());
            TypeNiHaoAsYouHao(c);
        }
        Check(a->IsLearnedPhrase(reading, value), "three times: learned");
        a->Sync();

        auto b = MakeLearner(lm, journal);
        Check(b->IsLearnedPhrase(reading, value), "learned phrase is in the journal");
        {
            Composer c(b->LanguageModel(), b.get());
            Type(c, "su3cl3");
            CheckText(c.DisplayText(), L"妳郝", "learned phrase is used automatically");
            Type(c, "ji3");
            CheckText(c.DisplayText(), L"妳郝我", "and the sentence goes on");
            // Ctrl+Delete on it in the candidate list forgets it.
            Press(c, KeyKind::Home);
            Press(c, KeyKind::Down);
            CheckText(c.CandidateText(0), L"妳郝", "learned phrase is the first candidate");
            Press(c, KeyKind::Forget);
            Check(!c.CandidatesOpen(), "forget closes the list");
            Check(!b->IsLearnedPhrase(reading, value), "forgotten");
        }
        {
            Composer c(b->LanguageModel(), b.get());
            Type(c, "su3cl3");
            Check(c.DisplayText() != L"妳郝", "forgotten phrase is no longer used");
        }

        // Picking something else where a learned phrase stood unlearns it too.
        auto d = MakeLearner(lm, journal);
        Check(d->IsLearnedPhrase(reading, value), "third program still has it (nothing synced yet)");
        {
            Composer c(d->LanguageModel(), d.get());
            Type(c, "su3cl3");
            Press(c, KeyKind::Home);
            Press(c, KeyKind::Down);
            Check(Pick(c, L"你好"), "pick the dictionary phrase instead");
            Type(c, "\n");
        }
        Check(!d->IsLearnedPhrase(reading, value), "replacing a learned phrase unlearns it");
        d->Sync();
        b->Sync();
        auto e = MakeLearner(lm, journal);
        Check(!e->IsLearnedPhrase(reading, value), "and that is permanent");
    }
    {
        // Punctuation and literals never end up in the journal.
        const std::wstring journal = dir + L"\\clean.txt";
        auto a = MakeLearner(lm, journal);
        Composer c(a->LanguageModel(), a.get());
        Type(c, "su3");
        Press(c, KeyKind::CtrlPunctuation, L',');
        Press(c, KeyKind::Down);
        Press(c, KeyKind::Enter);
        Press(c, KeyKind::Literal, L'A');
        Type(c, " cl3\n");
        a->Sync();
        Check(ReadFileBytes(journal).find("_zy_") == std::string::npos, "no internal readings in the journal");
    }
    {
        // The journal is compacted when it grows; programs following the old file start over
        // from the new one and end up with the same state. Deleting the file clears everything.
        const std::wstring journal = dir + L"\\compact.txt";
        auto a = MakeLearner(lm, journal);
        {
            Composer c(a->LanguageModel(), a.get());
            Type(c, "su3");
            Press(c, KeyKind::Down);
            Pick(c, L"妳");
            Type(c, "\n");
        }
        a->Sync();
        auto b = MakeLearner(lm, journal);
        a->SetCompactThreshold(200);
        for (int i = 0; i < 12; ++i) {
            Composer c(a->LanguageModel(), a.get());
            Type(c, "su3\n");
            a->Sync();
        }
        const std::string content = ReadFileBytes(journal);
        Check(content.find("\nA\t") != std::string::npos && content.size() < 400, "journal was compacted");
        Check(Near(a->Boost(ni, you), LearningModel::kBoostScale * std::log2(14.0)), "13 commits counted");
        b->Sync();
        Check(Near(b->Boost(ni, you), a->Boost(ni, you), 1e-3), "follower of the old file catches up");

        DeleteFileW(journal.c_str());
        b->Sync();
        Check(b->Boost(ni, you) == 0, "deleting learning.txt clears what was learned");
    }

    for (const wchar_t* name : {L"picks.txt", L"phrases.txt", L"clean.txt", L"compact.txt", L"compact.txt.tmp"})
        DeleteFileW((dir + L"\\" + name).c_str());
    RemoveDirectoryW(dir.c_str());
}


static bool HasValue(const std::vector<Formosa::Gramambular2::LanguageModel::Unigram>& unigrams, const char* value) {
    for (const auto& u : unigrams)
        if (u.value() == value) return true;
    return false;
}

static void TestPhraseFile() {
    auto entries = phrase_file::Parse(
        "\xEF\xBB\xBF# comment\n小麥 ㄒㄧㄠˇ ㄇㄞˋ\r\n\n  注音\tㄓㄨˋ-ㄧㄣ  \nㄘˊ 詞\n怪詞\n書 ㄕㄨˉ\n雞 ㄐㄧ 12\n");
    Check(entries.size() == 6, "phrase file: comments and blank lines skipped");
    if (entries.size() == 6) {
        Check(entries[0].value == "小麥" && entries[0].reading == "ㄒㄧㄠˇ-ㄇㄞˋ", "syllables separated by spaces");
        Check(entries[1].value == "注音" && entries[1].reading == "ㄓㄨˋ-ㄧㄣ", "tabs, dashes, CRLF");
        Check(entries[2].value == "詞" && entries[2].reading == "ㄘˊ", "reading first is accepted");
        Check(entries[3].value == "怪詞" && entries[3].reading.empty(), "reading may be left out");
        Check(entries[4].reading == "ㄕㄨ", "first-tone mark dropped");
        Check(entries[5].reading == "ㄐㄧ", "a trailing score is not part of the reading");
    }
    // Microsoft Bopomofo's export: UTF-16, each syllable in four fixed slots padded with U+3000.
    const std::string ms = DecodeText(std::string("\xFF\xFE", 2) +
                                      std::string(reinterpret_cast<const char*>(L"武藏  　ㄨ　ˇ ㄗ　ㄤˋ   \r\n大和  ㄉ　ㄚˋ ㄏ　ㄜˊ   \r\n"),
                                                  sizeof(L"武藏  　ㄨ　ˇ ㄗ　ㄤˋ   \r\n大和  ㄉ　ㄚˋ ㄏ　ㄜˊ   \r\n") - sizeof(wchar_t)));
    auto msEntries = phrase_file::Parse(ms);
    Check(msEntries.size() == 2 && msEntries[0].value == "武藏" && msEntries[0].reading == "ㄨˇ-ㄗㄤˋ" &&
              msEntries[1].value == "大和" && msEntries[1].reading == "ㄉㄚˋ-ㄏㄜˊ",
          "Microsoft Bopomofo export is read");
    Check(phrase_file::NormalizeReading("ㄒㄧㄠㄇㄞ") == "ㄒㄧㄠ-ㄇㄞ", "first-tone syllables split by structure");
    Check(phrase_file::NormalizeReading("ㄒㄧㄠˇㄇㄞˋ") == "ㄒㄧㄠˇ-ㄇㄞˋ", "syllables split after tone marks");
    Check(phrase_file::NormalizeReading("ㄒㄧㄠ ˇ ㄇㄞˋ") == "ㄒㄧㄠˇ-ㄇㄞˋ", "a lone tone mark joins the syllable before");
    Check(DecodeText("\xA4\xA4\xA4\xE5") == "中文", "Big5 is converted");
    const std::string text = phrase_file::Format({{"小麥", "ㄒㄧㄠˇ-ㄇㄞˋ"}, {"怪詞", ""}}, {"自訂詞"});
    Check(text == "# 自訂詞\n小麥 ㄒㄧㄠˇ-ㄇㄞˋ\n怪詞\n", "phrase file format");
    Check(phrase_file::Parse(text).size() == 2, "format round-trips");
}

static void TestLayeredDictionary(const std::wstring& dataDir) {
    const std::wstring user = TempDir();
    Dictionary d(dataDir, user);
    d.Reload();
    if (!d.Loaded()) {
        std::printf("SKIP layered dictionary tests\n");
        return;
    }
    auto packs = d.Packs();
    Check(packs.size() == 3, "three packs listed");    for (const auto& p : packs) Check(p.present && p.enabled, "pack present and on by default");
    Check(!packs.empty() && packs[0].id == "base" && packs[0].required, "base pack is required");
    Check(!d.Reload(), "nothing changed: no reload");

    Check(!HasValue(d.getUnigrams("ㄇㄧㄥˊ-ㄊㄧㄢ"), "MACRO@DATE_TOMORROW_SHORT") &&
              HasValue(d.getUnigrams("ㄇㄧㄥˊ-ㄊㄧㄢ"), "明天"),
          "MACRO@ rows are gone, words stay");
    Check(HasValue(d.getUnigrams("ㄅㄚ"), "⑧"), "symbols pack on: ㄅㄚ offers ⑧");
    Check(d.GuessReading("你好") == "ㄋㄧˇ-ㄏㄠˇ", "guess reading of a word");
    Check(d.ValidReading("ㄋㄧˇ-ㄏㄠˇ") && !d.ValidReading("ㄋㄧˇ-ㄅㄧㄤ") && !d.ValidReading(""), "valid readings");
    auto readings = d.ReadingsOf("好");
    Check(readings.size() >= 2 && readings[0] == "ㄏㄠˇ", "readings of a character, most common first");

    // Turning packs off in settings.ini.
    WriteFileReplace(user + L"\\settings.ini", "[Packs]\r\nsymbols=0\r\nphrases=0\r\nbase=0\r\n");
    Check(d.Reload(), "settings change reloads");
    Check(!HasValue(d.getUnigrams("ㄅㄚ"), "⑧") && HasValue(d.getUnigrams("ㄅㄚ"), "八"), "symbols pack off");
    Check(!d.hasUnigrams("ㄋㄧˇ-ㄏㄠˇ"), "phrases pack off: no words");
    Check(d.Loaded() && d.hasUnigrams("ㄋㄧˇ"), "the base pack cannot be turned off");
    {
        Composer c(std::shared_ptr<Dictionary>(&d, [](Dictionary*) {}));
        Type(c, "su3cl3");
        Check(CodePointCount(c.DisplayText()) == 2, "characters only still type");
    }
    WriteFileReplace(user + L"\\settings.ini", "[Packs]\r\n");
    d.Reload();
    Check(d.Options().enterCommitsBopomofo, "Enter types Bopomofo by default");
    WriteFileReplace(user + L"\\settings.ini", "[Packs]\r\n[Options]\r\nEnterCommitsBopomofo=0\r\n");
    Check(d.Reload() && !d.Options().enterCommitsBopomofo, "option turned off in settings.ini");
    WriteFileReplace(user + L"\\settings.ini", "[Packs]\r\n");
    d.Reload();
    Check(d.hasUnigrams("ㄋㄧˇ-ㄏㄠˇ") && HasValue(d.getUnigrams("ㄅㄚ"), "⑧"), "packs back on");

    // User phrases: always win; a missing reading is guessed.
    WriteFileReplace(user + L"\\user-phrases.txt", "小麥注音 ㄒㄧㄠˇ ㄇㄞˋ ㄓㄨˋ ㄧㄣ\n妳郝\n");
    Check(d.Reload(), "user phrases change reloads");
    Check(d.UserPhrases().size() == 2 && d.UserPhrases()[1].reading == "ㄋㄧˇ-ㄏㄠˇ", "reading guessed for 妳郝");
    {
        Composer c(std::shared_ptr<Dictionary>(&d, [](Dictionary*) {}));
        Type(c, "vul3a945j4up ");
        CheckText(c.DisplayText(), L"小麥注音", "user phrase is typed");
        Type(c, "\n");
        Type(c, "su3cl3");
        CheckText(c.DisplayText(), L"妳郝", "user phrase beats the dictionary word");
    }

    // Excluded phrases: per reading, or under every reading.
    WriteFileReplace(user + L"\\user-phrases.txt", "");
    WriteFileReplace(user + L"\\excluded-phrases.txt", "你好 ㄋㄧˇ-ㄏㄠˇ\n妳\n");
    d.Reload();
    Check(!HasValue(d.getUnigrams("ㄋㄧˇ-ㄏㄠˇ"), "你好"), "excluded word is gone");
    Check(!HasValue(d.getUnigrams("ㄋㄧˇ"), "妳") && HasValue(d.getUnigrams("ㄋㄧˇ"), "你"),
          "excluded under every reading; the rest stays");
    Check(d.IsExcluded("ㄋㄧˇ", "妳") && d.IsExcluded("ㄋㄧˇ-ㄏㄠˇ", "你好") && !d.IsExcluded("ㄋㄧˇ", "你"),
          "IsExcluded");
    {
        Composer c(std::shared_ptr<Dictionary>(&d, [](Dictionary*) {}));
        Type(c, "su3cl3");
        Press(c, KeyKind::Home);
        Press(c, KeyKind::Down);
        // (你 + 好 typed character by character is still possible: only the word is excluded.)
        Check(c.CandidatesOpen() && !Has(c, L"你好") && !Has(c, L"妳"), "excluded words are not offered");
    }

    DeleteFileW((user + L"\\settings.ini").c_str());
    DeleteFileW((user + L"\\user-phrases.txt").c_str());
    DeleteFileW((user + L"\\excluded-phrases.txt").c_str());
    RemoveDirectoryW(user.c_str());
}

int main(int argc, char** argv) {
    TestSyllable();
    TestEcho();
    TestLearningModel();
    TestPhraseFile();
    if (argc > 1) {
        // The data folder (packs.txt and the packs); a path to a file in it works too.
        std::wstring dataDir = Utf8ToWide(argv[1]);
        if (FileExists(dataDir)) dataDir = dataDir.substr(0, dataDir.find_last_of(L"\\/"));
        TestDictionary(dataDir);
        TestLearner(dataDir);
        TestLayeredDictionary(dataDir);
    } else {
        std::printf("SKIP dictionary tests: no data folder given\n");
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
