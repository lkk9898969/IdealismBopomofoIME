// Key event handling. OnTestKeyDown and OnKeyDown must agree on which keys are eaten,
// so both go through ShouldEatKeyDown(), which has no side effects on the composer.
#include "tip/TextService.h"

#include "tip/Beep.h"

namespace zy {
namespace {

bool IsModifierKey(WPARAM vk) {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
            return true;
        default:
            return false;
    }
}

bool KeyDown(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }
bool CapsLockOn() { return (GetKeyState(VK_CAPITAL) & 1) != 0; }

// Character a key produces on the US layout (the physical layout Bopomofo keyboards use).
wchar_t UsLayoutChar(WPARAM vk, bool shift) {
    if (vk >= '0' && vk <= '9') return shift ? L")!@#$%^&*("[vk - '0'] : static_cast<wchar_t>(vk);
    if (vk >= 'A' && vk <= 'Z') return static_cast<wchar_t>(shift ? vk : vk + 32);
    switch (vk) {
        case VK_OEM_1: return shift ? L':' : L';';
        case VK_OEM_PLUS: return shift ? L'+' : L'=';
        case VK_OEM_COMMA: return shift ? L'<' : L',';
        case VK_OEM_MINUS: return shift ? L'_' : L'-';
        case VK_OEM_PERIOD: return shift ? L'>' : L'.';
        case VK_OEM_2: return shift ? L'?' : L'/';
        case VK_OEM_3: return shift ? L'~' : L'`';
        case VK_OEM_4: return shift ? L'{' : L'[';
        case VK_OEM_5: return shift ? L'|' : L'\\';
        case VK_OEM_6: return shift ? L'}' : L']';
        case VK_OEM_7: return shift ? L'"' : L'\'';
        default: return 0;
    }
}

// The character a key types in English mode (letters follow Shift/CapsLock); 0 if not printable.
wchar_t TypedChar(WPARAM vk) {
    const bool shift = KeyDown(VK_SHIFT);
    if (vk == VK_SPACE) return L' ';
    if (vk >= 'A' && vk <= 'Z') return static_cast<wchar_t>(shift != CapsLockOn() ? vk : vk + 32);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return static_cast<wchar_t>(L'0' + (vk - VK_NUMPAD0));
    switch (vk) {
        case VK_MULTIPLY: return L'*';
        case VK_ADD: return L'+';
        case VK_SUBTRACT: return L'-';
        case VK_DECIMAL: return L'.';
        case VK_DIVIDE: return L'/';
        default: return UsLayoutChar(vk, shift);
    }
}

// The ASCII symbol of a "Ctrl + symbol" key (Ctrl+,  Ctrl+Shift+/ ...), or 0 for letters, plain
// digits and everything else — those stay application shortcuts (Ctrl+C, Ctrl+1 ...).
wchar_t CtrlSymbolChar(WPARAM vk, bool shift) {
    if (vk >= 'A' && vk <= 'Z') return 0;
    if (vk >= '0' && vk <= '9' && !shift) return 0;
    const wchar_t c = UsLayoutChar(vk, shift);
    return Composer::FullWidthSymbol(c) ? c : 0;
}

}  // namespace

bool TextService::IsChineseMode() const { return keyboardOpen_ && chineseMode_; }

bool TextService::TranslateKey(WPARAM vk, Key* out) const {
    const bool shift = KeyDown(VK_SHIFT);
    *out = {KeyKind::Other};
    // Every key also carries its US-layout character (the ` prefix needs it for Bopomofo keys).
    struct SetAscii {
        Key* key;
        wchar_t ascii;
        ~SetAscii() { key->ascii = ascii; }
    } setAscii{out, UsLayoutChar(vk, shift)};

    // Ctrl+Alt+, opens the symbol keyboard (Chinese mode).
    if (KeyDown(VK_CONTROL) && KeyDown(VK_MENU)) {
        if (vk == VK_OEM_COMMA) *out = {KeyKind::SymbolKeyboard};
        return true;
    }
    // Ctrl + symbol types full-width punctuation; any other Ctrl combination is never text.
    if (KeyDown(VK_CONTROL)) {
        if (wchar_t c = CtrlSymbolChar(vk, shift)) *out = {KeyKind::CtrlPunctuation, c};
        else if (vk == VK_DELETE) *out = {KeyKind::Forget};
        return true;
    }

    // While the candidate list is open, the top-row 1-9 select candidates.
    if (composer_ && composer_->CandidatesOpen() && !shift && vk >= '1' && vk <= '9') {
        *out = {KeyKind::Digit, static_cast<wchar_t>(vk)};
        return true;
    }

    switch (vk) {
        case VK_SPACE: *out = {KeyKind::Space}; return true;
        case VK_RETURN: *out = {KeyKind::Enter}; return true;
        case VK_ESCAPE: *out = {KeyKind::Escape}; return true;
        case VK_TAB: *out = {KeyKind::Tab}; return true;
        case VK_BACK: *out = {KeyKind::Backspace}; return true;
        case VK_DELETE: *out = {KeyKind::Delete}; return true;
        case VK_LEFT: *out = {KeyKind::Left}; return true;
        case VK_RIGHT: *out = {KeyKind::Right}; return true;
        case VK_UP: *out = {KeyKind::Up}; return true;
        case VK_DOWN: *out = {KeyKind::Down}; return true;
        case VK_HOME: *out = {KeyKind::Home}; return true;
        case VK_END: *out = {KeyKind::End}; return true;
        case VK_PRIOR: *out = {KeyKind::PageUp}; return true;
        case VK_NEXT: *out = {KeyKind::PageDown}; return true;
        case VK_MULTIPLY: *out = {KeyKind::Literal, L'*'}; return true;
        case VK_ADD: *out = {KeyKind::Literal, L'+'}; return true;
        case VK_SUBTRACT: *out = {KeyKind::Literal, L'-'}; return true;
        case VK_DECIMAL: *out = {KeyKind::Literal, L'.'}; return true;
        case VK_DIVIDE: *out = {KeyKind::Literal, L'/'}; return true;
        default: break;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        *out = {KeyKind::Literal, static_cast<wchar_t>(L'0' + (vk - VK_NUMPAD0))};
        return true;
    }

    // Shift+letter types English, like Microsoft Bopomofo: uppercase, or lowercase with CapsLock on.
    // CapsLock alone changes nothing in Chinese mode (letters stay Bopomofo keys).
    if (vk >= 'A' && vk <= 'Z' && shift) {
        const bool upper = !CapsLockOn();
        *out = {KeyKind::Literal, static_cast<wchar_t>(upper ? vk : vk + 32)};
        return true;
    }

    if (!shift) {
        if (auto s = StandardLayoutSymbol(static_cast<unsigned>(vk))) {
            *out = {KeyKind::Symbol, *s};
            return true;
        }
        if (auto t = StandardLayoutTone(static_cast<unsigned>(vk))) {
            *out = {KeyKind::Tone, *t};
            return true;
        }
    }

    // ` starts a full-width symbol: ` then , types ，
    if (vk == VK_OEM_3 && !shift) {
        *out = {KeyKind::SymbolPrefix};
        return true;
    }

    // Remaining printable keys (Shift+digits, Shift+/, brackets, quotes...) type what is on the key,
    // half-width. Full-width punctuation is Ctrl+symbol or ` + symbol, like Microsoft Bopomofo.
    if (wchar_t c = UsLayoutChar(vk, shift)) {
        *out = {KeyKind::Punctuation, c};
        return true;
    }
    return true;
}

// English mode with a sentence still being composed (switched mid-sentence): keys that type
// something type it as English into the sentence; editing keys, Enter and the candidate list
// keep working as in Chinese mode.
void TextService::TranslateEnglishKey(WPARAM vk, Key* out) const {
    TranslateKey(vk, out);
    switch (out->kind) {
        case KeyKind::Symbol:
        case KeyKind::Tone:
        case KeyKind::Punctuation:
        case KeyKind::SymbolPrefix:
        case KeyKind::Literal:
            if (wchar_t c = TypedChar(vk)) *out = {KeyKind::Literal, c, out->ascii};
            break;
        default:
            break;
    }
}

bool TextService::ShouldEatKeyDown(WPARAM vk) const {
    if (!composer_ || !keyboardOpen_ || IsModifierKey(vk)) return false;
    // Characters sent with SendInput (the IME pad, on-screen keyboards) are never ours.
    if (vk == VK_PACKET) return false;
    // Never block menu accelerators / Alt+Tab. The one Alt combination we take is Ctrl+Alt+,
    // (the symbol keyboard), and only in Chinese mode, like Microsoft Bopomofo.
    if (KeyDown(VK_MENU)) return IsChineseMode() && KeyDown(VK_CONTROL) && vk == VK_OEM_COMMA;

    // (Also in English mode: switching mid-sentence keeps the sentence, like Microsoft Bopomofo.)
    const bool composing = composer_->Composing();
    if (KeyDown(VK_CONTROL)) {
        // Ctrl + symbol is full-width punctuation in both Chinese and English mode, like Microsoft
        // Bopomofo. Every other Ctrl shortcut (Ctrl+C!) is left untouched unless we are composing.
        if (CtrlSymbolChar(vk, KeyDown(VK_SHIFT))) return true;
        return composing;
    }
    if (vk == VK_SPACE && KeyDown(VK_SHIFT)) return true;  // Shift+Space: full/half width
    // English mode: nothing is ours, except that 全形 mode types full-width characters (and the
    // sentence, if one is still being composed).
    if (!IsChineseMode()) return composing || (fullWidth_ && TypedChar(vk) != 0);
    Key key;
    TranslateKey(vk, &key);
    return composer_->WouldHandle(key);
}

// The mouse buttons' toggle bits (bit 0 of GetKeyState flips on every press) plus "held now"
// bits. This is the thread's own key state, updated as it processes input: unlike the
// GetAsyncKeyState "pressed since last call" bit, the host app polling it cannot clear it, and
// reading it does not disturb the host.
unsigned MouseButtonState() {
    unsigned state = 0;
    int bit = 0;
    for (int button : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2}) {
        const SHORT s = GetKeyState(button);
        if (s & 1) state |= 1u << bit;
        if (s & 0x8000) state |= 1u << (bit + 8);
        ++bit;
    }
    return state;
}

void TextService::TrackShiftDown(WPARAM vk) {
    // Auto-repeat of Shift keeps the tap pending; any other key cancels it (Shift+A, Ctrl+Shift...).
    const bool shift = vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT;
    if (shift && !shiftPending_) mouseStateAtShift_ = MouseButtonState();
    shiftPending_ = shift;
}

void TextService::HandleShiftUp(ITfContext* ctx, WPARAM vk) {
    if (vk != VK_SHIFT && vk != VK_LSHIFT && vk != VK_RSHIFT) return;
    // Both OnTestKeyUp and OnKeyUp may run for the same key; the first one consumes the flag.
    if (!shiftPending_) return;
    shiftPending_ = false;
    // Shift+click (add to selection in 3ds Max, …) is not a tap: a click in between flipped a
    // button's toggle bit, or a button is still held.
    const unsigned mouse = MouseButtonState();
    if (mouse != mouseStateAtShift_ || (mouse & 0xFF00)) return;
    ToggleChineseMode(ctx);
}

void TextService::ToggleChineseMode(ITfContext* ctx) {
    if (!keyboardOpen_) {
        // The app (or IMM32's ImmSetOpenStatus) closed the keyboard, which looks like English mode.
        // Shift / the tray button then mean "back to Chinese", as with Microsoft Bopomofo, rather
        // than leaving the user stuck until they switch input methods.
        OpenKeyboard();
        return;
    }
    chineseMode_ = !chineseMode_;
    SyncModeToCompartment();
    if (!chineseMode_) KeepSentenceInEnglishMode(ctx);
}

// Switched to English while composing: like Microsoft Bopomofo the sentence stays (English then
// types into it); only a half-typed syllable / the symbol keyboard go away.
void TextService::KeepSentenceInEnglishMode(ITfContext* ctx) {
    if (!composer_ || composer_->Empty()) return;
    composer_->EnterEnglishMode();
    Render(ctx ? ctx : compositionContext_.Get(), std::wstring());
}

void TextService::ToggleFullWidth() {
    fullWidth_ = !fullWidth_;
    if (composer_) composer_->SetFullWidth(fullWidth_);
    SyncModeToCompartment();
}

void TextService::ResetComposer() {
    if (composer_) composer_->Clear();
    candidateWindow_.Hide();
    EndCandidateUI();
}

STDMETHODIMP TextService::OnSetFocus(BOOL) { return S_OK; }

STDMETHODIMP TextService::OnTestKeyDown(ITfContext*, WPARAM wParam, LPARAM, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    try {
        TrackShiftDown(wParam);
        *eaten = ShouldEatKeyDown(wParam);
    } catch (...) {
        *eaten = FALSE;
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* ctx, WPARAM wParam, LPARAM, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    try {
        TrackShiftDown(wParam);
        if (!ShouldEatKeyDown(wParam)) return S_OK;
        *eaten = TRUE;
        Key key;
        if (!KeyDown(VK_CONTROL) && wParam == VK_SPACE && KeyDown(VK_SHIFT)) {
            ToggleFullWidth();
            return S_OK;
        }
        if (!IsChineseMode() && composer_->Composing()) {
            TranslateEnglishKey(wParam, &key);
            ComposeResult result = composer_->Feed(key);
            Render(ctx, result.commit);
            return S_OK;
        }
        TranslateKey(wParam, &key);
        if (!IsChineseMode()) {
            // English mode with nothing composed: text goes straight into the document.
            const wchar_t ch = key.kind == KeyKind::CtrlPunctuation ? Composer::FullWidthSymbol(key.ch)
                                                                    : Composer::FullWidthChar(TypedChar(wParam));
            if (ch) Render(ctx, std::wstring(1, ch));
            return S_OK;
        }
        ComposeResult result = composer_->Feed(key);
        if (result.beep) PlayErrorBeep();  // returns at once; played on the thread pool
        Render(ctx, result.commit);
    } catch (...) {
        ResetComposer();
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* ctx, WPARAM wParam, LPARAM, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;  // never eat key-ups: the app must see Shift released
    try {
        HandleShiftUp(ctx, wParam);
    } catch (...) {
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* ctx, WPARAM wParam, LPARAM, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    try {
        HandleShiftUp(ctx, wParam);
    } catch (...) {
    }
    return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) {
    if (eaten) *eaten = FALSE;
    return S_OK;
}

}  // namespace zy
