// Rendering engine state into the document through TSF edit sessions.
#include <new>

#include "tip/CandidateUI.h"
#include "tip/TextService.h"

using Microsoft::WRL::ComPtr;

namespace zy {
namespace {

class RenderSession final : public ITfEditSession {
public:
    RenderSession(TextService* service, ITfContext* ctx, RenderState state)
        : service_(service), ctx_(ctx), state_(std::move(state)) {
        service_->AddRef();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        try {
            service_->RenderInSession(ctx_.Get(), ec, state_);
        } catch (...) {
        }
        return S_OK;
    }

private:
    ~RenderSession() { service_->Release(); }

    LONG ref_ = 1;
    TextService* service_;
    ComPtr<ITfContext> ctx_;
    RenderState state_;
};

void SetCaret(ITfContext* ctx, TfEditCookie ec, ITfRange* range, LONG offsetFromStart, bool atEnd) {
    ComPtr<ITfRange> caret;
    if (FAILED(range->Clone(&caret))) return;
    if (atEnd) {
        caret->Collapse(ec, TF_ANCHOR_END);
    } else {
        caret->Collapse(ec, TF_ANCHOR_START);
        LONG moved = 0;
        caret->ShiftEnd(ec, offsetFromStart, &moved, nullptr);
        caret->Collapse(ec, TF_ANCHOR_END);
    }
    TF_SELECTION sel = {};
    sel.range = caret.Get();
    sel.style.ase = TF_AE_NONE;
    sel.style.fInterimChar = FALSE;
    ctx->SetSelection(ec, 1, &sel);
}

bool IsEmptyRect(const RECT& rc) { return rc.left == 0 && rc.top == 0 && rc.right == 0 && rc.bottom == 0; }

// Screen rectangle of the text, or of the system caret as a fallback (some apps report no layout).
RECT TextRect(ITfContext* ctx, TfEditCookie ec, ITfRange* range) {
    RECT rc = {};
    ComPtr<ITfContextView> view;
    BOOL clipped = FALSE;
    if (range && SUCCEEDED(ctx->GetActiveView(&view)) && view &&
        SUCCEEDED(view->GetTextExt(ec, range, &rc, &clipped)) && !IsEmptyRect(rc))
        return rc;

    GUITHREADINFO gti = {sizeof(gti)};
    if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && gti.hwndCaret) {
        rc = gti.rcCaret;
        MapWindowPoints(gti.hwndCaret, nullptr, reinterpret_cast<POINT*>(&rc), 2);
        return rc;
    }
    POINT pt = {};
    GetCursorPos(&pt);
    return {pt.x, pt.y, pt.x, pt.y + 20};
}

}  // namespace

RenderState TextService::Snapshot(const std::wstring& commit) const {
    RenderState s;
    s.commit = commit;
    if (!composer_) return s;
    s.display = composer_->DisplayText();
    s.caret = composer_->CaretOffset();
    if (composer_->SymbolKeyboardOpen()) {
        const size_t page = composer_->SymbolKeyboardPage();
        const size_t rowLengths[] = {12, 13, 11, 10};  // 1234567890-=  qwertyuiop[]\  asdfghjkl;'  zxcvbnm,./
        size_t key = 0;
        for (size_t length : rowLengths) {
            std::vector<CandidatePage::Key> row;
            for (size_t i = 0; i < length && key < Composer::kSymbolKeyCount; ++i, ++key) {
                wchar_t label = Composer::kSymbolKeys[key];
                if (label >= L'a' && label <= L'z') label -= 32;
                row.push_back({label, Composer::SymbolOnKey(page, key)});
            }
            s.candidates.keyboard.push_back(std::move(row));
        }
        s.candidates.title = Composer::SymbolPageTitle(page);
        s.candidates.page = page;
        s.candidates.pageCount = Composer::SymbolPageCount();
    } else if (composer_->CandidatesOpen()) {
        const size_t per = Composer::kCandidatesPerPage;
        const size_t count = composer_->CandidateCount();
        const size_t highlighted = composer_->HighlightedCandidate();
        // List view shows one page; table view shows several pages side by side as columns.
        const size_t columnsPerScreen = composer_->CandidatesExpanded() ? Composer::kExpandedColumns : 1;
        const size_t columnCount = (count + per - 1) / per;
        const size_t column = highlighted / per;
        const size_t firstColumn = (column / columnsPerScreen) * columnsPerScreen;
        for (size_t c = firstColumn; c < columnCount && c < firstColumn + columnsPerScreen; ++c) {
            std::vector<std::wstring> items;
            for (size_t i = c * per; i < count && i < (c + 1) * per; ++i) items.push_back(composer_->CandidateText(i));
            s.candidates.columns.push_back(std::move(items));
        }
        s.candidates.highlightedColumn = column - firstColumn;
        s.candidates.highlightedRow = highlighted % per;
        s.candidates.page = column / columnsPerScreen;
        s.candidates.pageCount = (columnCount + columnsPerScreen - 1) / columnsPerScreen;
    }
    return s;
}

void TextService::Render(ITfContext* ctx, const std::wstring& commit) {
    if (!requireKeySink_) committedForTesting_ += commit;  // smoke test only
    if (!ctx) return;
    // First tell a UILess app about the list (it decides whether we draw it), then render.
    UpdateCandidateUI(ctx);
    RenderState state = Snapshot(commit);
    state.showCandidateWindow = candidateUIShown_;
    auto* session = new (std::nothrow) RenderSession(this, ctx, std::move(state));
    if (!session) return;

    // Prefer a synchronous session (allowed inside key event sinks). If TSF can't grant one,
    // queue an async one and return immediately — never wait. The session renders a snapshot,
    // so queued sessions applied later still leave the document in the right state.
    HRESULT sessionHr = E_FAIL;
    HRESULT hr = ctx->RequestEditSession(clientId_, session, TF_ES_SYNC | TF_ES_READWRITE, &sessionHr);
    if (FAILED(hr) || sessionHr == TF_E_SYNCHRONOUS) {
        ctx->RequestEditSession(clientId_, session, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &sessionHr);
    }
    session->Release();
}

void TextService::CommitAll(ITfContext* ctx) {
    if (!composer_) return;
    Render(ctx, composer_->TakeCommitText());
}

void TextService::RenderInSession(ITfContext* ctx, TfEditCookie ec, const RenderState& state) {
    // A composition left over in another context is TSF's to clean up; don't touch it.
    if (composition_ && compositionContext_.Get() != ctx) {
        composition_.Reset();
        compositionContext_.Reset();
    }

    auto ensureComposition = [&]() -> bool {
        if (composition_) return true;
        ComPtr<ITfInsertAtSelection> insert;
        ComPtr<ITfContextComposition> contextComposition;
        ComPtr<ITfRange> range;
        if (FAILED(ctx->QueryInterface(IID_PPV_ARGS(&insert))) ||
            FAILED(ctx->QueryInterface(IID_PPV_ARGS(&contextComposition))) ||
            FAILED(insert->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &range)))
            return false;
        ComPtr<ITfComposition> composition;
        if (FAILED(contextComposition->StartComposition(ec, range.Get(), this, &composition)) || !composition)
            return false;
        composition_ = composition;
        compositionContext_ = ctx;
        return true;
    };

    // Sets the composition text; returns its range.
    auto setCompositionText = [&](const std::wstring& text, ComPtr<ITfRange>* outRange) -> bool {
        if (!ensureComposition()) return false;
        ComPtr<ITfRange> range;
        if (FAILED(composition_->GetRange(&range))) return false;
        if (FAILED(range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.size())))) return false;
        if (outRange) *outRange = range;
        return true;
    };

    auto endComposition = [&]() {
        if (!composition_) return;
        ComPtr<ITfRange> range;
        if (SUCCEEDED(composition_->GetRange(&range))) SetCaret(ctx, ec, range.Get(), 0, true);
        composition_->EndComposition(ec);
        composition_.Reset();
        compositionContext_.Reset();
    };

    if (!state.commit.empty()) {
        if (setCompositionText(state.commit, nullptr)) {
            endComposition();
        } else {
            // Some apps refuse compositions; fall back to inserting the text directly.
            endComposition();
            ComPtr<ITfInsertAtSelection> insert;
            ComPtr<ITfRange> range;
            if (SUCCEEDED(ctx->QueryInterface(IID_PPV_ARGS(&insert))) &&
                SUCCEEDED(insert->InsertTextAtSelection(ec, 0, state.commit.c_str(),
                                                        static_cast<LONG>(state.commit.size()), &range))) {
                SetCaret(ctx, ec, range.Get(), 0, true);
            }
        }
    }

    ComPtr<ITfRange> compositionRange;
    if (!state.display.empty()) {
        if (setCompositionText(state.display, &compositionRange))
            SetCaret(ctx, ec, compositionRange.Get(), static_cast<LONG>(state.caret), false);
    } else if (composition_) {
        setCompositionText(L"", nullptr);
        endComposition();
    }

    if (!state.candidates.Empty() && state.showCandidateWindow) {
        // Under the composition; with nothing composed (the symbol keyboard), under the caret.
        RECT anchor;
        if (compositionRange) {
            anchor = TextRect(ctx, ec, compositionRange.Get());
        } else {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            if (SUCCEEDED(ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched && sel.range) {
                anchor = TextRect(ctx, ec, sel.range);
                sel.range->Release();
            } else {
                anchor = TextRect(ctx, ec, nullptr);
            }
        }
        candidateWindow_.Show(state.candidates, anchor);
    } else {
        candidateWindow_.Hide();
    }
}

void TextService::UpdateCandidateUI(ITfContext* ctx) {
    const bool open = composer_ && (composer_->CandidatesOpen() || composer_->SymbolKeyboardOpen());
    if (!open) {
        EndCandidateUI();
        return;
    }
    CandidateUI::Content content;
    if (composer_->SymbolKeyboardOpen()) {
        // A game's own list shows the symbol keyboard as "key symbol" items; the keys still work.
        const size_t page = composer_->SymbolKeyboardPage();
        for (size_t k = 0; k < Composer::kSymbolKeyCount; ++k) {
            const std::wstring symbol = Composer::SymbolOnKey(page, k);
            if (symbol.empty()) continue;
            wchar_t label = Composer::kSymbolKeys[k];
            if (label >= L'a' && label <= L'z') label -= 32;
            content.items.push_back(std::wstring(1, label) + L" " + symbol);
        }
        content.pageStarts = {0};
    } else {
        const size_t count = composer_->CandidateCount();
        const size_t per = Composer::kCandidatesPerPage;
        for (size_t i = 0; i < count; ++i) content.items.push_back(composer_->CandidateText(i));
        for (size_t i = 0; i < count; i += per) content.pageStarts.push_back(static_cast<UINT>(i));
        content.selection = static_cast<UINT>(composer_->HighlightedCandidate());
        content.currentPage = static_cast<UINT>(composer_->HighlightedCandidate() / per);
    }
    if (ctx) ctx->GetDocumentMgr(&content.documentMgr);

    ComPtr<ITfUIElementMgr> mgr;
    if (threadMgr_) threadMgr_.As(&mgr);
    if (!candidateUI_) {
        candidateUI_ = new (std::nothrow) CandidateUI(this);
        if (!candidateUI_) return;
        candidateUI_->SetContent(std::move(content));
        BOOL show = TRUE;
        DWORD id = TF_INVALID_UIELEMENTID;
        if (mgr && SUCCEEDED(mgr->BeginUIElement(candidateUI_, &show, &id))) {
            candidateUIId_ = id;
            candidateUIShown_ = show != FALSE;
        } else {
            candidateUIId_ = TF_INVALID_UIELEMENTID;
            candidateUIShown_ = !uiElementsOnly_;
        }
        candidateUI_->SetShown(candidateUIShown_);
    } else {
        candidateUI_->SetContent(std::move(content));
        if (mgr && candidateUIId_ != TF_INVALID_UIELEMENTID) mgr->UpdateUIElement(candidateUIId_);
    }
}

void TextService::EndCandidateUI() {
    if (!candidateUI_) return;
    CandidateUI* ui = candidateUI_;
    candidateUI_ = nullptr;
    if (candidateUIId_ != TF_INVALID_UIELEMENTID && threadMgr_) {
        ComPtr<ITfUIElementMgr> mgr;
        if (SUCCEEDED(threadMgr_.As(&mgr))) mgr->EndUIElement(candidateUIId_);
    }
    candidateUIId_ = TF_INVALID_UIELEMENTID;
    candidateUIShown_ = true;
    ui->Detach();
    ui->Release();
}

void TextService::OnCandidateUIShown(bool shown) {
    candidateUIShown_ = shown;
    if (!shown) candidateWindow_.Hide();
}

void TextService::OnCandidateUISelect(UINT index) {
    if (!composer_ || !composer_->CandidatesOpen()) return;
    composer_->HighlightCandidate(index);
    Render(compositionContext_.Get(), L"");
}

void TextService::OnCandidateUIFinish(bool accept) {
    if (!composer_) return;
    Key key{accept ? KeyKind::Enter : KeyKind::Escape};
    if (composer_->CandidatesOpen() || composer_->SymbolKeyboardOpen()) {
        if (composer_->SymbolKeyboardOpen()) key = {KeyKind::Escape};
        const ComposeResult result = composer_->Feed(key);
        Render(compositionContext_.Get(), result.commit);
    }
}

}  // namespace zy
