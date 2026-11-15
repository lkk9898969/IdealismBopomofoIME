#include "tip/TextService.h"

#include <new>

#include "engine/EngineHost.h"
#include "tip/LangBarButton.h"

using Microsoft::WRL::ComPtr;

namespace zy {
namespace {

std::wstring ModuleDirectory() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(g_hInst, path, ARRAYSIZE(path));
    std::wstring dir(path, len);
    size_t slash = dir.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash);
}

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Installed layout: <root>\x64\IdealismTip.dll, <root>\x86\IdealismTip.dll, <root>\data\packs.txt (+ packs)
std::wstring FindDataDirectory() {
    std::wstring dir = ModuleDirectory();
    for (const wchar_t* rel : {L"\\..\\data", L"\\data", L""}) {
        std::wstring d = dir + rel;
        if (FileExists(d + L"\\packs.txt")) return d;
    }
    return {};
}

std::wstring UserDataDirectory() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, ARRAYSIZE(buf));
    if (n == 0 || n >= ARRAYSIZE(buf)) return {};
    return std::wstring(buf, n) + L"\\IdealismIME";
}

}  // namespace

TextService::TextService() { DllAddRef(); }
TextService::~TextService() { DllRelease(); }

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppv = static_cast<ITfCompositionSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompartmentEventSink)) {
        *ppv = static_cast<ITfCompartmentEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadFocusSink)) {
        *ppv = static_cast<ITfThreadFocusSink*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() { return InterlockedIncrement(&ref_); }

STDMETHODIMP_(ULONG) TextService::Release() {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP TextService::Activate(ITfThreadMgr* threadMgr, TfClientId clientId) {
    return ActivateEx(threadMgr, clientId, 0);
}

void TextService::InitEngine() {
    try {
        EngineHost& host = EngineHost::Instance();
        host.EnsureLoaded(FindDataDirectory(), UserDataDirectory());
        composer_ = std::make_unique<Composer>(host.LanguageModel(), host.Learning());
    } catch (...) {
        composer_.reset();
    }
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags) {
    threadMgr_ = threadMgr;
    clientId_ = clientId;
    // The app shows all IME UI itself (UILess): never show our own windows unasked.
    uiElementsOnly_ = (flags & TF_TMAE_UIELEMENTENABLEDONLY) != 0;

    ComPtr<ITfKeystrokeMgr> keystrokes;
    if (SUCCEEDED(threadMgr_.As(&keystrokes)) &&
        SUCCEEDED(keystrokes->AdviseKeyEventSink(clientId_, static_cast<ITfKeyEventSink*>(this), TRUE))) {
        keySinkAdvised_ = true;
    } else if (requireKeySink_) {
        Deactivate();
        return E_FAIL;
    }

    ComPtr<ITfSource> source;
    if (SUCCEEDED(threadMgr_.As(&source)))
        source->AdviseSink(IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this), &threadFocusCookie_);

    InitEngine();
    InitLangBar();
    InitCompartments();  // drives the 中/英 state; failure is not fatal
    return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
    // Don't touch the document here: TSF terminates any open composition itself.
    composition_.Reset();
    compositionContext_.Reset();
    EndCandidateUI();
    candidateWindow_.Destroy();
    composer_.reset();

    UninitCompartments();
    UninitLangBar();

    if (threadMgr_ && threadFocusCookie_ != TF_INVALID_COOKIE) {
        ComPtr<ITfSource> source;
        if (SUCCEEDED(threadMgr_.As(&source))) source->UnadviseSink(threadFocusCookie_);
    }
    threadFocusCookie_ = TF_INVALID_COOKIE;

    if (keySinkAdvised_ && threadMgr_) {
        ComPtr<ITfKeystrokeMgr> keystrokes;
        if (SUCCEEDED(threadMgr_.As(&keystrokes))) keystrokes->UnadviseKeyEventSink(clientId_);
    }
    keySinkAdvised_ = false;
    threadMgr_.Reset();
    clientId_ = TF_CLIENTID_NULL;
    return S_OK;
}

void TextService::InitLangBar() {
    ComPtr<ITfLangBarItemMgr> mgr;
    if (FAILED(threadMgr_.As(&mgr))) return;
    langBarButton_ = new (std::nothrow) LangBarButton(this);
    if (!langBarButton_) return;
    if (FAILED(mgr->AddItem(langBarButton_))) {
        langBarButton_->Detach();
        langBarButton_->Release();
        langBarButton_ = nullptr;
    }
}

void TextService::UninitLangBar() {
    if (!langBarButton_) return;
    ComPtr<ITfLangBarItemMgr> mgr;
    if (threadMgr_ && SUCCEEDED(threadMgr_.As(&mgr))) mgr->RemoveItem(langBarButton_);
    langBarButton_->Detach();
    langBarButton_->Release();
    langBarButton_ = nullptr;
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie, ITfComposition*) {
    // The app ended our composition (focus change, mouse click...). Whatever text was showing stays in
    // the document as-is; just drop our state.
    composition_.Reset();
    compositionContext_.Reset();
    ResetComposer();
    return S_OK;
}

STDMETHODIMP TextService::OnSetThreadFocus() {
    // Coming back to this program: pick up what was learned in others meanwhile. Only schedules
    // background work; nothing here touches a file.
    try {
        EngineHost::Instance().RefreshAsync();
    } catch (...) {
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
    candidateWindow_.Hide();
    return S_OK;
}

}  // namespace zy
