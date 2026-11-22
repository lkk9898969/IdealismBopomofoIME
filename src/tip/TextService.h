#pragma once
#include <wrl/client.h>

#include <memory>
#include <string>
#include <vector>

#include "engine/Composer.h"
#include "tip/CandidateWindow.h"
#include "tip/Globals.h"

namespace zy {

class LangBarButton;
class CandidateUI;

// Everything an edit session needs to bring the document and UI in line with the engine.
struct RenderState {
    std::wstring commit;
    std::wstring display;
    size_t caret = 0;
    CandidatePage candidates;  // empty items = no candidate window
    bool showCandidateWindow = true;  // false when the app draws the list itself (UILess)
};

// The TSF text input processor.
//
// Compatibility rules (see docs/01-survey.md section 2.2) — the reason this project exists:
//  * Everything runs in-process on the host's UI thread; there is no IPC on the key path.
//  * Engine state is updated first, then an edit session *renders* a snapshot of it. Edit sessions
//    are requested synchronously when TSF allows it; if TSF queues one instead, we never wait.
//  * Nothing here blocks: no waits, no cross-process calls, no file I/O while handling keys.
//  * The candidate window paints via UpdateLayeredWindow, so it never depends on the host
//    dispatching WM_PAINT (hosts with odd message loops are exactly what we must survive).
//  * No C++ exception may escape into the host: COM entry points catch everything.
class TextService final : public ITfTextInputProcessorEx,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfCompartmentEventSink,
                          public ITfThreadFocusSink {
public:
    TextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor(Ex)
    STDMETHODIMP Activate(ITfThreadMgr* threadMgr, TfClientId clientId) override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags) override;
    STDMETHODIMP Deactivate() override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL foreground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* ctx, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* ctx, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* ctx, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* ctx, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* ctx, REFGUID guid, BOOL* eaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* composition) override;

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID guid) override;

    // ITfThreadFocusSink
    STDMETHODIMP OnSetThreadFocus() override;
    STDMETHODIMP OnKillThreadFocus() override;

    // Used by edit sessions (Composition.cpp)
    void RenderInSession(ITfContext* ctx, TfEditCookie ec, const RenderState& state);

    // Used by the language bar button
    bool IsChineseMode() const;
    void ToggleChineseMode(ITfContext* ctx);
    void KeepSentenceInEnglishMode(ITfContext* ctx);
    void OpenKeyboard();  // reopen after the app closed the keyboard; Chinese mode
    bool IsFullWidth() const { return fullWidth_; }
    void ToggleFullWidth();

    // Used by the candidate list UI element (UILess mode, CandidateUI.cpp)
    void OnCandidateUIShown(bool shown);
    void OnCandidateUISelect(UINT index);
    void OnCandidateUIFinish(bool accept);

    // tests/tip_smoke only: TSF refuses key event sinks from TIPs it did not activate itself
    // (i.e. not installed), so the smoke test calls the sink methods directly instead.
    void AllowMissingKeySinkForTesting() { requireKeySink_ = false; }
    // tests/tip_smoke only: everything committed so far, and what the composition shows now.
    const std::wstring& CommittedForTesting() const { return committedForTesting_; }
    std::wstring DisplayForTesting() const { return composer_ ? composer_->DisplayText() : std::wstring(); }

private:
    ~TextService();

    // KeyHandler.cpp
    bool TranslateKey(WPARAM vk, Key* out) const;
    void TranslateEnglishKey(WPARAM vk, Key* out) const;
    bool ShouldEatKeyDown(WPARAM vk) const;
    void CommitBeforeAppShortcut(ITfContext* ctx, WPARAM vk);
    void TrackShiftDown(WPARAM vk);
    void HandleShiftUp(ITfContext* ctx, WPARAM vk);
    void ResetComposer();

    // Composition.cpp
    RenderState Snapshot(const std::wstring& commit) const;
    void Render(ITfContext* ctx, const std::wstring& commit);
    void CommitAll(ITfContext* ctx);
    // Begins, updates or ends the candidate list UI element to match the composer.
    void UpdateCandidateUI(ITfContext* ctx);
    void EndCandidateUI();

    // Compartment.cpp
    HRESULT InitCompartments();
    void UninitCompartments();
    void SyncModeToCompartment();
    void ReadModeFromCompartment();

    // TextService.cpp
    void InitEngine();
    void InitLangBar();
    void UninitLangBar();

    LONG ref_ = 1;
    Microsoft::WRL::ComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    bool keySinkAdvised_ = false;
    bool requireKeySink_ = true;
    std::wstring committedForTesting_;
    DWORD threadFocusCookie_ = TF_INVALID_COOKIE;

    Microsoft::WRL::ComPtr<ITfComposition> composition_;
    Microsoft::WRL::ComPtr<ITfContext> compositionContext_;

    DWORD conversionSinkCookie_ = TF_INVALID_COOKIE;
    DWORD openCloseSinkCookie_ = TF_INVALID_COOKIE;
    bool updatingCompartment_ = false;

    std::unique_ptr<Composer> composer_;
    CandidateWindow candidateWindow_;
    CandidateUI* candidateUI_ = nullptr;  // while a candidate list / symbol keyboard is open
    DWORD candidateUIId_ = TF_INVALID_UIELEMENTID;
    bool candidateUIShown_ = true;  // false: the app shows the list itself
    bool uiElementsOnly_ = false;   // activated with TF_TMAE_UIELEMENTENABLEDONLY
    LangBarButton* langBarButton_ = nullptr;
    bool chineseMode_ = true;
    bool keyboardOpen_ = true;
    bool fullWidth_ = false;  // 全形 (Shift+Space)

    // Shift-tap detection: Shift pressed and released with no other key in between.
    bool shiftPending_ = false;
    unsigned mouseStateAtShift_ = 0;  // mouse buttons when Shift went down (see MouseButtonState)
};

}  // namespace zy
