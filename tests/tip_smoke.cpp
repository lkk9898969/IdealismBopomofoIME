// Smoke test for the TSF text service, without installing it:
//  1. Activates the TIP against a real TSF thread manager in this process (as a host app would),
//     feeds keys through the key event sink, and deactivates — repeatedly. Any crash here would
//     crash every app the IME is loaded into.
//  2. Renders a candidate window page to a .bmp for visual inspection.
// Usage: tip_smoke [preview.bmp [keyboard.bmp]]
#include <wrl/client.h>

#include <cstdio>
#include <string>

#include "tip/CandidateWindow.h"
#include "tip/Globals.h"
#include "tip/TextService.h"

using Microsoft::WRL::ComPtr;

static int g_failures = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static void PressKey(ITfKeyEventSink* sink, ITfContext* ctx, WPARAM vk) {
    BOOL eaten = FALSE;
    sink->OnTestKeyDown(ctx, vk, 0, &eaten);
    if (eaten) sink->OnKeyDown(ctx, vk, 0, &eaten);
    sink->OnTestKeyUp(ctx, vk, 0, &eaten);
}

static void RunSession(int round) {
    ComPtr<ITfThreadMgr> tm;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tm));
    Check(SUCCEEDED(hr), "create thread manager");
    if (FAILED(hr)) return;
    TfClientId tid = TF_CLIENTID_NULL;
    Check(SUCCEEDED(tm->Activate(&tid)), "activate thread manager");

    // A document with an (empty, store-less) context, so key handling goes through Render().
    ComPtr<ITfDocumentMgr> doc;
    ComPtr<ITfContext> ctx;
    TfEditCookie ecStore = 0;
    if (SUCCEEDED(tm->CreateDocumentMgr(&doc)) && SUCCEEDED(doc->CreateContext(tid, 0, nullptr, &ctx, &ecStore))) {
        doc->Push(ctx.Get());
        tm->SetFocus(doc.Get());
    }

    auto* service = new zy::TextService();
    service->AllowMissingKeySinkForTesting();
    ComPtr<ITfTextInputProcessorEx> tip;
    service->QueryInterface(IID_PPV_ARGS(&tip));
    service->Release();
    Check(SUCCEEDED(tip->ActivateEx(tm.Get(), tid, 0)), "TIP ActivateEx");

    ComPtr<ITfKeyEventSink> keys;
    tip.As(&keys);
    // ㄋㄧˇ ㄏㄠˇ, open candidates, page, table view and back, select 2, caret, space, reopen,
    // type on, punctuation, commit.
    for (WPARAM vk : {WPARAM('S'), WPARAM('U'), WPARAM('3'), WPARAM('C'), WPARAM('L'), WPARAM('3'), WPARAM(VK_DOWN),
                      WPARAM(VK_DOWN), WPARAM(VK_NEXT), WPARAM(VK_PRIOR), WPARAM(VK_RIGHT), WPARAM(VK_RIGHT),
                      WPARAM(VK_SPACE), WPARAM(VK_LEFT), WPARAM(VK_LEFT), WPARAM(VK_TAB), WPARAM('2'),
                      WPARAM(VK_HOME), WPARAM(VK_SPACE), WPARAM(VK_DOWN), WPARAM(VK_ESCAPE), WPARAM('J'),
                      WPARAM('I'), WPARAM('3'), WPARAM(VK_OEM_4), WPARAM(VK_OEM_3), WPARAM(VK_OEM_COMMA),
                      WPARAM(VK_OEM_3), WPARAM(VK_ESCAPE), WPARAM(VK_BACK), WPARAM(VK_RETURN)})
        PressKey(keys.Get(), ctx.Get(), vk);
    // Shift tap toggles to English and back.
    PressKey(keys.Get(), ctx.Get(), VK_SHIFT);
    PressKey(keys.Get(), ctx.Get(), VK_SHIFT);
    // Null context (no focused document) must be harmless too.
    PressKey(keys.Get(), nullptr, 'S');
    PressKey(keys.Get(), nullptr, VK_ESCAPE);

    Check(SUCCEEDED(tip->Deactivate()), "TIP Deactivate");
    keys.Reset();
    tip.Reset();
    if (doc) doc->Pop(TF_POPF_ALL);
    tm->Deactivate();
    std::printf("round %d ok\n", round);
}

// A host that draws IME UI itself (like a full-screen game): it tells the IME not to show its
// candidate window and reads the list through the UI element instead.
class UILessHost final : public ITfUIElementSink {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfUIElementSink)) {
            *ppv = static_cast<ITfUIElementSink*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP BeginUIElement(DWORD id, BOOL* show) override {
        ++begun;
        current = id;
        if (show) *show = FALSE;  // "I'll draw it"
        return S_OK;
    }
    STDMETHODIMP UpdateUIElement(DWORD) override {
        ++updated;
        return S_OK;
    }
    STDMETHODIMP EndUIElement(DWORD id) override {
        if (id == current) ++ended;
        return S_OK;
    }
    int begun = 0, updated = 0, ended = 0;
    DWORD current = TF_INVALID_UIELEMENTID;
};

static void RunUILessSession() {
    ComPtr<ITfThreadMgr> tm;
    if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tm)))) return;
    TfClientId tid = TF_CLIENTID_NULL;
    tm->Activate(&tid);
    ComPtr<ITfDocumentMgr> doc;
    ComPtr<ITfContext> ctx;
    TfEditCookie ecStore = 0;
    if (SUCCEEDED(tm->CreateDocumentMgr(&doc)) && SUCCEEDED(doc->CreateContext(tid, 0, nullptr, &ctx, &ecStore))) {
        doc->Push(ctx.Get());
        tm->SetFocus(doc.Get());
    }
    UILessHost host;
    ComPtr<ITfSource> source;
    DWORD cookie = TF_INVALID_COOKIE;
    tm.As(&source);
    Check(source && SUCCEEDED(source->AdviseSink(IID_ITfUIElementSink, static_cast<ITfUIElementSink*>(&host), &cookie)),
          "UILess host advises a UI element sink");

    auto* service = new zy::TextService();
    service->AllowMissingKeySinkForTesting();
    ComPtr<ITfTextInputProcessorEx> tip;
    service->QueryInterface(IID_PPV_ARGS(&tip));
    service->Release();
    tip->ActivateEx(tm.Get(), tid, TF_TMAE_UIELEMENTENABLEDONLY);
    ComPtr<ITfKeyEventSink> keys;
    tip.As(&keys);

    for (WPARAM vk : {WPARAM('S'), WPARAM('U'), WPARAM('3'), WPARAM(VK_DOWN)}) PressKey(keys.Get(), ctx.Get(), vk);
    Check(host.begun == 1, "opening the list begins a UI element");
    ComPtr<ITfUIElementMgr> mgr;
    ComPtr<ITfUIElement> element;
    ComPtr<ITfCandidateListUIElement> list;
    tm.As(&mgr);
    if (mgr && SUCCEEDED(mgr->GetUIElement(host.current, &element)) && element) element.As(&list);
    Check(list != nullptr, "the UI element is a candidate list");
    if (list) {
        UINT count = 0, selection = 99, pages = 0;
        list->GetCount(&count);
        list->GetSelection(&selection);
        list->GetPageIndex(nullptr, 0, &pages);
        BSTR first = nullptr;
        list->GetString(0, &first);
        Check(count > 9 && selection == 0 && pages >= 2, "the app can read the whole list");
        Check(first && first[0] == L'\x4F60', "first candidate is the current character (你)");
        SysFreeString(first);
        BOOL shown = TRUE;
        element->IsShown(&shown);
        Check(!shown, "our own window stays hidden");
    }
    PressKey(keys.Get(), ctx.Get(), VK_DOWN);
    Check(host.updated >= 1, "moving the selection updates the UI element");
    if (list) {
        UINT selection = 0;
        list->GetSelection(&selection);
        Check(selection == 1, "the app sees the new selection");
        ComPtr<ITfCandidateListUIElementBehavior> behavior;
        list.As(&behavior);
        if (behavior) behavior->Abort();  // the app closes the list itself
        Check(host.ended == 1, "aborting from the app ends the UI element");
    }
    PressKey(keys.Get(), ctx.Get(), VK_ESCAPE);
    tip->Deactivate();
    keys.Reset();
    tip.Reset();
    element.Reset();
    list.Reset();
    if (source && cookie != TF_INVALID_COOKIE) source->UnadviseSink(cookie);
    if (doc) doc->Pop(TF_POPF_ALL);
    tm->Deactivate();
    std::printf("UILess session ok\n");
}

// Real key codes through the key event sink, checking what is committed.
static void RunKeySequences() {
    ComPtr<ITfThreadMgr> tm;
    if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tm)))) return;
    TfClientId tid = TF_CLIENTID_NULL;
    tm->Activate(&tid);
    ComPtr<ITfDocumentMgr> doc;
    ComPtr<ITfContext> ctx;
    TfEditCookie ecStore = 0;
    if (SUCCEEDED(tm->CreateDocumentMgr(&doc)) && SUCCEEDED(doc->CreateContext(tid, 0, nullptr, &ctx, &ecStore))) {
        doc->Push(ctx.Get());
        tm->SetFocus(doc.Get());
    }
    auto* service = new zy::TextService();
    service->AllowMissingKeySinkForTesting();
    ComPtr<ITfTextInputProcessorEx> tip;
    service->QueryInterface(IID_PPV_ARGS(&tip));
    tip->ActivateEx(tm.Get(), tid, 0);
    ComPtr<ITfKeyEventSink> keys;
    tip.As(&keys);
    auto type = [&](std::initializer_list<WPARAM> vks) {
        for (WPARAM vk : vks) PressKey(keys.Get(), ctx.Get(), vk);
    };

    type({VK_OEM_3, 'U', '4', 'E', '0', '0'});
    Check(service->DisplayForTesting() == L"\xFF40U4E00", "` U 4E00 is shown while typing");
    type({VK_SPACE});
    Check(service->CommittedForTesting() == L"\x4E00", "` U 4E00 Space commits 一");
    type({VK_OEM_3, 'B', 'A', '4', '4', '0'});
    Check(service->CommittedForTesting() == L"\x4E00\x4E00", "` B A440 commits 一");

    // Shift to English mid-sentence keeps the sentence; English is typed into it (CapsLock off).
    if (!(GetKeyState(VK_CAPITAL) & 1)) {
        type({'2', '8', '4', '5'});  // 大 + half-typed ㄓ
        type({VK_SHIFT});
        Check(service->DisplayForTesting() == L"\x5927", "English mode keeps the sentence, drops ㄓ");
        type({'H', 'I', VK_SPACE, '3'});
        Check(service->DisplayForTesting() == L"\x5927hi 3", "English typed into the sentence");
        type({VK_RETURN});
        Check(service->CommittedForTesting() == L"\x4E00\x4E00\x5927hi 3", "Enter commits all of it");
        type({VK_SHIFT, '3', VK_RETURN});  // back to Chinese: a tone key alone types the mark
        Check(service->CommittedForTesting() == L"\x4E00\x4E00\x5927hi 3\x02C7", "tone mark alone");
    }

    // CapsLock does not change Chinese input; it only makes Shift+letter lowercase (this thread's
    // key state, which is what the TIP reads).
    {
        BYTE saved[256] = {};
        GetKeyboardState(saved);
        auto setState = [&](bool caps, bool shift) {
            BYTE state[256];
            memcpy(state, saved, sizeof(state));
            state[VK_CAPITAL] = caps ? 1 : 0;
            state[VK_SHIFT] = state[VK_LSHIFT] = shift ? 0x80 : 0;
            SetKeyboardState(state);
        };
        const std::wstring before = service->CommittedForTesting();
        setState(true, false);
        type({'2', '8', '4'});
        setState(true, true);
        type({'A'});
        setState(true, false);
        type({'C', 'L', '3', VK_RETURN});
        setState(false, true);
        type({'A', VK_RETURN});  // without CapsLock, Shift+A on an empty buffer is the app's 'A'
        SetKeyboardState(saved);
        Check(service->CommittedForTesting() == before + L"\x5927" L"a" L"\x597D",
              "CapsLock: Bopomofo still typed, Shift+A is lowercase");
    }

    tip->Deactivate();
    keys.Reset();
    tip.Reset();
    service->Release();
    if (doc) doc->Pop(TF_POPF_ALL);
    tm->Deactivate();
    std::printf("key sequences ok\n");
}

int main(int argc, char** argv) {
    zy::g_hInst = GetModuleHandleW(nullptr);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;

    // The TIP keeps the user's files under %APPDATA%\IdealismIME. Point it at a scratch folder, so
    // that what this test "learns" never reaches the real user's learning.txt.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring appData = std::wstring(temp) + L"idealism_smoke_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(appData.c_str(), nullptr);
    SetEnvironmentVariableW(L"APPDATA", appData.c_str());
    const std::wstring userDir = appData + L"\\IdealismIME";
    const std::wstring journal = userDir + L"\\learning.txt";

    for (int i = 1; i <= 3; ++i) RunSession(i);
    RunUILessSession();
    RunKeySequences();

    // The sessions picked a candidate and committed, so the learning journal must show up — written
    // by the thread pool, never by the key handlers.
    bool journalWritten = false;
    for (int i = 0; i < 50 && !journalWritten; ++i) {
        journalWritten = GetFileAttributesW(journal.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (!journalWritten) Sleep(100);
    }
    Check(journalWritten, "learning journal written in the background");
    Sleep(300);  // let a write in progress finish before cleaning up
    DeleteFileW(journal.c_str());
    DeleteFileW((journal + L".tmp").c_str());
    RemoveDirectoryW(userDir.c_str());
    RemoveDirectoryW(appData.c_str());

    if (argc > 2) {
        // The symbol keyboard, first page.
        zy::CandidatePage kb;
        const size_t rowLengths[] = {12, 13, 11, 10};
        size_t key = 0;
        for (size_t length : rowLengths) {
            std::vector<zy::CandidatePage::Key> row;
            for (size_t i = 0; i < length; ++i, ++key) {
                wchar_t label = zy::Composer::kSymbolKeys[key];
                if (label >= L'a' && label <= L'z') label -= 32;
                row.push_back({label, zy::Composer::SymbolOnKey(1, key)});
            }
            kb.keyboard.push_back(std::move(row));
        }
        kb.title = zy::Composer::SymbolPageTitle(1);
        kb.page = 1;
        kb.pageCount = zy::Composer::SymbolPageCount();
        wchar_t path[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, argv[2], -1, path, MAX_PATH);
        Check(zy::CandidateWindow::SavePreview(kb, 144, path), "save symbol keyboard preview");
    }
    if (argc > 1) {
        zy::CandidatePage page;
        // Table view: three columns, the second one focused.
        page.columns = {{L"你", L"妳", L"擬", L"尼", L"泥", L"倪", L"霓", L"膩", L"逆"},
                        {L"你們", L"旎", L"儗", L"薿", L"伱", L"禰", L"鉨", L"苨", L"檷"},
                        {L"孴", L"隬", L"聻"}};
        page.highlightedColumn = 1;
        page.highlightedRow = 2;
        page.page = 0;
        page.pageCount = 2;
        wchar_t path[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH);
        Check(zy::CandidateWindow::SavePreview(page, 144, path), "save candidate preview");
    }

    CoUninitialize();
    std::printf(g_failures ? "%d failures\n" : "smoke test passed\n", g_failures);
    return g_failures ? 1 : 0;
}
