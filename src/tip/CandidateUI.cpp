#include "tip/CandidateUI.h"

#include "tip/TextService.h"

namespace zy {
namespace {

// {6E3C1E9A-2B7D-4C58-9A61-0F5D3B2E7A14}
constexpr GUID kCandidateUIGuid = {0x6e3c1e9a, 0x2b7d, 0x4c58, {0x9a, 0x61, 0x0f, 0x5d, 0x3b, 0x2e, 0x7a, 0x14}};

}  // namespace

CandidateUI::CandidateUI(TextService* owner) : owner_(owner) { DllAddRef(); }
CandidateUI::~CandidateUI() { DllRelease(); }

void CandidateUI::SetContent(Content content) {
    if (content.items != content_.items) updated_ |= TF_CLUIE_STRING | TF_CLUIE_COUNT;
    if (content.pageStarts != content_.pageStarts) updated_ |= TF_CLUIE_PAGEINDEX;
    if (content.selection != content_.selection) updated_ |= TF_CLUIE_SELECTION;
    if (content.currentPage != content_.currentPage) updated_ |= TF_CLUIE_CURRENTPAGE;
    if (content.documentMgr.Get() != content_.documentMgr.Get()) updated_ |= TF_CLUIE_DOCUMENTMGR;
    content_ = std::move(content);
}

STDMETHODIMP CandidateUI::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfUIElement) ||
        IsEqualIID(riid, IID_ITfCandidateListUIElement) || IsEqualIID(riid, IID_ITfCandidateListUIElementBehavior)) {
        *ppv = static_cast<ITfCandidateListUIElementBehavior*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CandidateUI::AddRef() { return InterlockedIncrement(&ref_); }

STDMETHODIMP_(ULONG) CandidateUI::Release() {
    const LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP CandidateUI::GetDescription(BSTR* description) {
    if (!description) return E_INVALIDARG;
    *description = SysAllocString(L"Candidate list");
    return *description ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateUI::GetGUID(GUID* guid) {
    if (!guid) return E_INVALIDARG;
    *guid = kCandidateUIGuid;
    return S_OK;
}

STDMETHODIMP CandidateUI::Show(BOOL show) {
    shown_ = show != FALSE;
    if (owner_) owner_->OnCandidateUIShown(shown_);
    return S_OK;
}

STDMETHODIMP CandidateUI::IsShown(BOOL* shown) {
    if (!shown) return E_INVALIDARG;
    *shown = shown_ ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetUpdatedFlags(DWORD* flags) {
    if (!flags) return E_INVALIDARG;
    *flags = updated_;
    updated_ = 0;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetDocumentMgr(ITfDocumentMgr** documentMgr) {
    if (!documentMgr) return E_INVALIDARG;
    *documentMgr = content_.documentMgr.Get();
    if (*documentMgr) (*documentMgr)->AddRef();
    return *documentMgr ? S_OK : E_FAIL;
}

STDMETHODIMP CandidateUI::GetCount(UINT* count) {
    if (!count) return E_INVALIDARG;
    *count = static_cast<UINT>(content_.items.size());
    return S_OK;
}

STDMETHODIMP CandidateUI::GetSelection(UINT* index) {
    if (!index) return E_INVALIDARG;
    *index = content_.selection;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetString(UINT index, BSTR* text) {
    if (!text) return E_INVALIDARG;
    *text = nullptr;
    if (index >= content_.items.size()) return E_INVALIDARG;
    *text = SysAllocString(content_.items[index].c_str());
    return *text ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateUI::GetPageIndex(UINT* index, UINT size, UINT* pageCount) {
    if (!pageCount) return E_INVALIDARG;
    *pageCount = static_cast<UINT>(content_.pageStarts.size());
    if (!index) return S_OK;  // asking for the count only
    if (size < content_.pageStarts.size()) return E_NOT_SUFFICIENT_BUFFER;
    for (size_t i = 0; i < content_.pageStarts.size(); ++i) index[i] = content_.pageStarts[i];
    return S_OK;
}

STDMETHODIMP CandidateUI::SetPageIndex(UINT*, UINT) { return S_OK; }  // our pages are fixed

STDMETHODIMP CandidateUI::GetCurrentPage(UINT* page) {
    if (!page) return E_INVALIDARG;
    *page = content_.currentPage;
    return S_OK;
}

STDMETHODIMP CandidateUI::SetSelection(UINT index) {
    if (!owner_) return E_FAIL;
    try {
        owner_->OnCandidateUISelect(index);
    } catch (...) {
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::Finalize() {
    if (!owner_) return E_FAIL;
    try {
        owner_->OnCandidateUIFinish(true);
    } catch (...) {
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::Abort() {
    if (!owner_) return E_FAIL;
    try {
        owner_->OnCandidateUIFinish(false);
    } catch (...) {
    }
    return S_OK;
}

}  // namespace zy
