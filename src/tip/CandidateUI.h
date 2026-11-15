#pragma once
#include <wrl/client.h>

#include <string>
#include <vector>

#include "tip/Globals.h"

namespace zy {

class TextService;

// The candidate list as a TSF UI element, for UILess mode: applications that draw IME UI
// themselves (full-screen games) receive the list through ITfUIElementSink and tell us not to
// show our own window. Everything here only copies strings and indexes; no blocking.
class CandidateUI final : public ITfCandidateListUIElementBehavior {
public:
    explicit CandidateUI(TextService* owner);

    struct Content {
        std::vector<std::wstring> items;
        std::vector<UINT> pageStarts;  // first item of each page
        UINT selection = 0;
        UINT currentPage = 0;
        Microsoft::WRL::ComPtr<ITfDocumentMgr> documentMgr;
    };
    void SetContent(Content content);  // marks what changed for GetUpdatedFlags
    void Detach() { owner_ = nullptr; }
    bool Shown() const { return shown_; }
    void SetShown(bool shown) { shown_ = shown; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfUIElement
    STDMETHODIMP GetDescription(BSTR* description) override;
    STDMETHODIMP GetGUID(GUID* guid) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP IsShown(BOOL* shown) override;

    // ITfCandidateListUIElement
    STDMETHODIMP GetUpdatedFlags(DWORD* flags) override;
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** documentMgr) override;
    STDMETHODIMP GetCount(UINT* count) override;
    STDMETHODIMP GetSelection(UINT* index) override;
    STDMETHODIMP GetString(UINT index, BSTR* text) override;
    STDMETHODIMP GetPageIndex(UINT* index, UINT size, UINT* pageCount) override;
    STDMETHODIMP SetPageIndex(UINT* index, UINT pageCount) override;
    STDMETHODIMP GetCurrentPage(UINT* page) override;

    // ITfCandidateListUIElementBehavior
    STDMETHODIMP SetSelection(UINT index) override;
    STDMETHODIMP Finalize() override;
    STDMETHODIMP Abort() override;

private:
    ~CandidateUI();

    LONG ref_ = 1;
    TextService* owner_;  // not owned; cleared by Detach() when the service goes away
    Content content_;
    DWORD updated_ = 0;
    bool shown_ = true;
};

}  // namespace zy
