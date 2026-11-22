#include "tip/LangBarButton.h"

#include <olectl.h>

#include <string>

#include "tip/TextService.h"

namespace zy {
namespace {

// GUID_LBI_INPUTMODE {2C77A81E-41CC-4178-A3A7-5F8A987568E6}: the system tray input-mode button.
constexpr GUID kGuidLbiInputMode = {0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};
constexpr DWORD kSinkCookie = 1;

// Resource IDs (IdealismTip.rc)
constexpr int IDI_MODE_ZH = 102;
constexpr int IDI_MODE_EN = 103;
constexpr int IDI_MODE_ZH_FULL = 104;  // 全形 variants
constexpr int IDI_MODE_EN_FULL = 105;

// Starts one of our programs, which sit in <root>, next to the x64/x86 folders with the DLLs.
// Returns at once: nothing here waits for it, so the host app is never held up.
bool Launch(const wchar_t* exeName) {
    wchar_t path[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(g_hInst, path, ARRAYSIZE(path));
    std::wstring dir(path, len);
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    for (const wchar_t* rel : {L"\\..\\", L"\\"}) {
        std::wstring exe = dir + rel + exeName;
        if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        std::wstring cmd = L"\"" + exe + L"\"";
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
            return false;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    return false;
}

void LaunchPad() {
    if (!Launch(L"IdealismPad.exe")) Launch(L"IdealismSettings.exe");
}

enum MenuCommand : UINT { kMenuPad = 1, kMenuSettings = 2 };

// Right click on the 中/英 button: a small menu (輸入法整合器, 設定), shown by the thread the IME
// runs on — the app's UI thread, where TSF delivers the click. The menu is owned by the app's own
// focus window and returns the choice (TPM_RETURNCMD | TPM_NONOTIFY), so the app never receives a
// command from it. Without a window to own it, the pad opens directly, as before.
void ShowMenu() {
    HWND owner = GetFocus();
    if (!owner) owner = GetActiveWindow();
    if (!owner) {
        LaunchPad();
        return;
    }
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        LaunchPad();
        return;
    }
    AppendMenuW(menu, MF_STRING, kMenuPad, L"輸入法整合器(&P)");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"設定(&S)");
    POINT pt = {};
    GetCursorPos(&pt);
    // The menu closes on a click elsewhere only if its owner is in the foreground (the taskbar may
    // have taken it with the click). This is the app's own window, so nothing else is disturbed.
    SetForegroundWindow(GetAncestor(owner, GA_ROOT));
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, owner,
        nullptr));
    PostMessageW(owner, WM_NULL, 0, 0);  // lets the menu close properly (KB135788)
    DestroyMenu(menu);
    if (command == kMenuPad) LaunchPad();
    else if (command == kMenuSettings) Launch(L"IdealismSettings.exe");
}

}  // namespace

LangBarButton::LangBarButton(TextService* owner) : owner_(owner) { DllAddRef(); }
LangBarButton::~LangBarButton() { DllRelease(); }

bool LangBarButton::Chinese() const { return owner_ ? owner_->IsChineseMode() : true; }
bool LangBarButton::FullWidth() const { return owner_ ? owner_->IsFullWidth() : false; }

void LangBarButton::Update() {
    if (sink_) sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

STDMETHODIMP LangBarButton::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton)) {
        *ppv = static_cast<ITfLangBarItemButton*>(this);
    } else if (IsEqualIID(riid, IID_ITfSource)) {
        *ppv = static_cast<ITfSource*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) LangBarButton::AddRef() { return InterlockedIncrement(&ref_); }

STDMETHODIMP_(ULONG) LangBarButton::Release() {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* info) {
    if (!info) return E_INVALIDARG;
    info->clsidService = CLSID_IdealismTextService;
    info->guidItem = kGuidLbiInputMode;
    info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    info->ulSort = 0;
    lstrcpynW(info->szDescription, L"中／英", ARRAYSIZE(info->szDescription));
    return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD* status) {
    if (!status) return E_INVALIDARG;
    *status = 0;
    return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL) { return E_NOTIMPL; }

STDMETHODIMP LangBarButton::GetTooltipString(BSTR* tooltip) {
    if (!tooltip) return E_INVALIDARG;
    const wchar_t* text = Chinese() ? (FullWidth() ? L"中文模式・全形（Shift 切換中／英，Shift+Space 切換全／半形；按右鍵開選單）"
                                                   : L"中文模式・半形（Shift 切換中／英，Shift+Space 切換全／半形；按右鍵開選單）")
                                    : (FullWidth() ? L"英文模式・全形（Shift 切換中／英，Shift+Space 切換全／半形；按右鍵開選單）"
                                                   : L"英文模式・半形（Shift 切換中／英，Shift+Space 切換全／半形；按右鍵開選單）");
    *tooltip = SysAllocString(text);
    return *tooltip ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT, const RECT*) {
    if (click == TF_LBI_CLK_LEFT && owner_) owner_->ToggleChineseMode(nullptr);
    else if (click == TF_LBI_CLK_RIGHT) ShowMenu();  // 輸入法整合器, 設定
    return S_OK;
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu*) { return S_OK; }
STDMETHODIMP LangBarButton::OnMenuSelect(UINT) { return S_OK; }

STDMETHODIMP LangBarButton::GetIcon(HICON* icon) {
    if (!icon) return E_INVALIDARG;
    const int size = GetSystemMetrics(SM_CXSMICON);
    *icon = static_cast<HICON>(LoadImageW(g_hInst, MAKEINTRESOURCEW(Chinese() ? (FullWidth() ? IDI_MODE_ZH_FULL : IDI_MODE_ZH)
                                                                            : (FullWidth() ? IDI_MODE_EN_FULL : IDI_MODE_EN)),
                                          IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
    return *icon ? S_OK : E_FAIL;
}

STDMETHODIMP LangBarButton::GetText(BSTR* text) {
    if (!text) return E_INVALIDARG;
    *text = SysAllocString(Chinese() ? L"中" : L"英");
    return *text ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown* sink, DWORD* cookie) {
    if (!sink || !cookie) return E_INVALIDARG;
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink)) return CONNECT_E_CANNOTCONNECT;
    if (sink_) return CONNECT_E_ADVISELIMIT;
    if (FAILED(sink->QueryInterface(IID_PPV_ARGS(&sink_)))) return E_NOINTERFACE;
    *cookie = kSinkCookie;
    return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD cookie) {
    if (cookie != kSinkCookie || !sink_) return CONNECT_E_NOCONNECTION;
    sink_.Reset();
    return S_OK;
}

}  // namespace zy
