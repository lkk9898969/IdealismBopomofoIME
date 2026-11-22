// 微新注音設定 (settings): phrase packs on/off, user phrases, excluded phrases, what was learned.
//
// Everything here is plain files under %APPDATA%\IdealismIME, written with a temporary file and a
// rename. The IME notices the changes by itself (when you switch back to a program), so this app
// never talks to the IME directly.
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "engine/Bopomofo.h"
#include "engine/Composer.h"
#include "engine/Dictionary.h"
#include "engine/Files.h"
#include "engine/Learner.h"
#include "engine/Utf.h"
#include "resource.h"

namespace {

using zy::PhraseEntry;

constexpr wchar_t kTitle[] = L"微新注音設定";
constexpr wchar_t kInstanceMutex[] = L"Local\\IdealismIME.Settings";
constexpr int kUser = 0;
constexpr int kExcluded = 1;
constexpr int kPageCount = 5;

// ---- State ----

struct LearnRow {
    char type = 0;  // 'L' learned phrase, 'A' pick count
    std::string reading, value;
    double count = 0;
};

struct App {
    HINSTANCE inst = nullptr;
    HWND dlg = nullptr;
    std::wstring dataDir;
    std::wstring userDir;
    std::shared_ptr<zy::Dictionary> dict;
    std::unique_ptr<zy::Learner> learner;
    std::vector<PhraseEntry> lists[2];
    std::vector<LearnRow> learnRows;
    int page = 0;
    bool filling = false;  // suppresses change notifications while lists are being filled
};
App g;

// ---- Small helpers ----

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, n);
    return dir.substr(0, dir.find_last_of(L'\\'));
}

std::wstring FindDataDirectory() {
    // Installed: <root>\IdealismSettings.exe next to <root>\data. Build tree: build\<arch>\Release.
    const std::wstring exe = ExeDirectory();
    for (const wchar_t* rel : {L"\\data", L"\\..\\data", L"\\..\\..\\data", L"\\..\\..\\..\\data"}) {
        const std::wstring dir = exe + rel;
        if (zy::FileExists(dir + L"\\" + zy::kManifestFile)) return dir;
    }
    return {};
}

std::wstring FindUserDirectory() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::wstring(buf, n) + L"\\IdealismIME";
}

std::wstring UserFile(const wchar_t* name) { return g.userDir + L"\\" + name; }
std::wstring ListFile(int kind) { return UserFile(kind == kUser ? zy::kUserPhrasesFile : zy::kExcludedPhrasesFile); }

std::wstring W(const std::string& s) { return zy::Utf8ToWide(s); }
std::string U(const std::wstring& s) { return zy::WideToUtf8(s); }

// Readings are stored as "ㄒㄧㄠˇ-ㄇㄞˋ" and shown as "ㄒㄧㄠˇ ㄇㄞˋ".
std::wstring ShowReading(const std::string& reading) {
    std::wstring w = W(reading);
    std::replace(w.begin(), w.end(), L'-', L' ');
    return w;
}

std::wstring GetText(HWND hwnd) {
    const int n = GetWindowTextLengthW(hwnd);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(hwnd, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

std::wstring Trim(std::wstring s) {
    const wchar_t* ws = L" \t\r\n　";
    s.erase(0, s.find_first_not_of(ws));
    const size_t end = s.find_last_not_of(ws);
    s.erase(end == std::wstring::npos ? 0 : end + 1);
    return s;
}

void Status(const std::wstring& text) { SetDlgItemTextW(g.dlg, IDC_STATUS, text.c_str()); }

int Ask(HWND owner, const std::wstring& text, UINT flags) { return MessageBoxW(owner, text.c_str(), kTitle, flags); }

size_t CodePoints(const std::string& s) { return zy::CodePointCount(W(s)); }
size_t Syllables(const std::string& reading) {
    return reading.empty() ? 0 : static_cast<size_t>(std::count(reading.begin(), reading.end(), '-')) + 1;
}

// ---- Phrase lists (files) ----

std::vector<std::string> ListComments(int kind) {
    if (kind == kUser)
        return {"自訂詞：每行一個「詞 注音」，注音的音節用 - 分開，例如「小麥 ㄒㄧㄠˇ-ㄇㄞˋ」。",
                "自訂詞永遠排在候選字最前面。可以用「微新注音設定」編輯，也可以直接改這個檔（UTF-8）。"};
    return {"排除詞：每行一個「詞 注音」；只寫詞、不寫注音，就是所有讀音都排除。",
            "被排除的詞不會出現在候選字裡。可以用「微新注音設定」編輯，也可以直接改這個檔（UTF-8）。"};
}

void LoadList(int kind) { g.lists[kind] = zy::phrase_file::Parse(zy::ReadWholeFile(ListFile(kind))); }

bool SaveList(int kind) {
    zy::EnsureDirectory(g.userDir);
    if (!zy::WriteFileReplace(ListFile(kind), zy::phrase_file::Format(g.lists[kind], ListComments(kind)))) {
        Ask(g.dlg, L"無法寫入 " + ListFile(kind), MB_ICONERROR);
        return false;
    }
    if (g.dict) g.dict->Reload();
    return true;
}

bool SameEntry(const PhraseEntry& a, const PhraseEntry& b) { return a.value == b.value && a.reading == b.reading; }

// Changes are applied to a fresh copy of the file, so edits made meanwhile in Notepad survive.
void AddEntries(int kind, const std::vector<PhraseEntry>& entries, size_t* added = nullptr) {
    LoadList(kind);
    size_t n = 0;
    for (const auto& e : entries) {
        if (std::none_of(g.lists[kind].begin(), g.lists[kind].end(), [&](const PhraseEntry& x) { return SameEntry(x, e); })) {
            g.lists[kind].push_back(e);
            ++n;
        }
    }
    if (added) *added = n;
}

void RemoveEntries(int kind, const std::vector<PhraseEntry>& entries) {
    LoadList(kind);
    auto& list = g.lists[kind];
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const PhraseEntry& x) {
                                  return std::any_of(entries.begin(), entries.end(),
                                                     [&](const PhraseEntry& e) { return SameEntry(x, e); });
                              }),
               list.end());
}

// ---- List views ----

void AddColumn(HWND list, int index, const wchar_t* title, int widthDlu) {
    RECT r = {0, 0, widthDlu, 1};
    MapDialogRect(g.dlg, &r);
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(title);
    col.cx = r.right;
    ListView_InsertColumn(list, index, &col);
}

int AddRow(HWND list, const std::vector<std::wstring>& cells, LPARAM data) {
    LVITEMW item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = ListView_GetItemCount(list);
    item.pszText = const_cast<wchar_t*>(cells[0].c_str());
    item.lParam = data;
    const int row = ListView_InsertItem(list, &item);
    for (size_t i = 1; i < cells.size(); ++i)
        ListView_SetItemText(list, row, static_cast<int>(i), const_cast<wchar_t*>(cells[i].c_str()));
    return row;
}

std::vector<LPARAM> SelectedData(HWND list) {
    std::vector<LPARAM> out;
    for (int i = ListView_GetNextItem(list, -1, LVNI_SELECTED); i >= 0; i = ListView_GetNextItem(list, i, LVNI_SELECTED)) {
        LVITEMW item = {};
        item.mask = LVIF_PARAM;
        item.iItem = i;
        ListView_GetItem(list, &item);
        out.push_back(item.lParam);
    }
    return out;
}

bool Matches(const std::wstring& filter, const std::string& value, const std::string& reading) {
    if (filter.empty()) return true;
    const std::string f = U(filter);
    return value.find(f) != std::string::npos || reading.find(U(Trim(filter))) != std::string::npos ||
           U(ShowReading(reading)).find(f) != std::string::npos;
}

// ---- Page 1: phrase packs ----

void FillPacks() {
    HWND list = GetDlgItem(g.dlg, IDC_PACKS_LIST);
    g.filling = true;
    ListView_DeleteAllItems(list);
    const auto packs = g.dict ? g.dict->Packs() : std::vector<zy::PackInfo>();
    for (size_t i = 0; i < packs.size(); ++i) {
        const auto& p = packs[i];
        std::wstring name = W(p.name);
        if (p.required) name += L"（必要）";
        if (!p.present) name += L"（檔案不見了）";
        const int row = AddRow(list, {name, std::to_wstring(p.rows), W(p.description)}, static_cast<LPARAM>(i));
        ListView_SetCheckState(list, row, p.enabled);
    }
    g.filling = false;
}

void OnPackToggled(int row) {
    HWND list = GetDlgItem(g.dlg, IDC_PACKS_LIST);
    const auto packs = g.dict->Packs();
    LVITEMW item = {};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    ListView_GetItem(list, &item);
    if (item.lParam < 0 || static_cast<size_t>(item.lParam) >= packs.size()) return;
    const auto& pack = packs[static_cast<size_t>(item.lParam)];
    const bool on = ListView_GetCheckState(list, row) != 0;
    if (pack.required) {
        if (!on) {
            g.filling = true;
            ListView_SetCheckState(list, row, TRUE);
            g.filling = false;
            Status(W(pack.name) + L"不能停用：沒有它就打不出字。");
        }
        return;
    }
    if (on == pack.enabled) return;
    zy::EnsureDirectory(g.userDir);
    WritePrivateProfileStringW(zy::kPacksSection, W(pack.id).c_str(), on ? L"1" : L"0",
                               UserFile(zy::kSettingsFile).c_str());
    g.dict->Reload();
    std::wstring note = on ? L"已啟用「" + W(pack.name) + L"」。" : L"已停用「" + W(pack.name) + L"」。";
    if (!on && pack.id == "phrases") note += L"停用後只能一個字一個字選字。";
    Status(note + L"切回打字的程式就會生效。");
}

// ---- Pages 2 and 3: user phrases, excluded phrases ----

int Base(int kind) { return kind == kUser ? IDC_USER_BASE : IDC_EXCL_BASE; }
HWND Item(int kind, int offset) { return GetDlgItem(g.dlg, Base(kind) + offset); }

void FillList(int kind) {
    HWND list = Item(kind, OFS_LIST);
    const std::wstring filter = Trim(GetText(Item(kind, OFS_SEARCH)));
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    const auto& entries = g.lists[kind];
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (!Matches(filter, e.value, e.reading)) continue;
        std::wstring reading = e.reading.empty() ? (kind == kUser ? L"（自動）" : L"（所有讀音）") : ShowReading(e.reading);
        AddRow(list, {W(e.value), reading}, static_cast<LPARAM>(i));
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

struct PhraseDialog {
    int kind = kUser;
    bool editing = false;
    PhraseEntry entry;
    bool readingTouched = false;  // the user typed a reading: stop filling it in automatically
    bool settingReading = false;
};

// Bopomofo typed directly, or the keys of the standard layout ("cl3" -> ㄏㄠˇ).
std::string ReadingFromInput(const std::wstring& input) {
    const std::wstring text = Trim(input);
    const bool ascii = !text.empty() && std::all_of(text.begin(), text.end(), [](wchar_t c) { return c < 0x80; });
    if (ascii) {
        std::wstring keys = text;
        return U(zy::StandardKeysToReading(keys));
    }
    return zy::phrase_file::NormalizeReading(U(text));
}

void UpdatePhraseHint(HWND hwnd, PhraseDialog* d) {
    std::wstring hint;
    const std::wstring value = Trim(GetText(GetDlgItem(hwnd, IDC_PH_VALUE)));
    if (d->kind == kExcluded) hint = L"注音留空：這個詞的所有讀音都排除。\n";
    else hint = L"注音會自動填入常用讀音。\n";
    if (g.dict && !value.empty()) {
        std::wstring heteronyms;
        for (size_t i = 0; i < value.size(); ++i) {
            const size_t n = (IS_HIGH_SURROGATE(value[i]) && i + 1 < value.size()) ? 2 : 1;
            const std::wstring ch = value.substr(i, n);
            i += n - 1;
            const auto readings = g.dict->ReadingsOf(U(ch));
            if (readings.size() < 2) continue;
            heteronyms += L"\n" + ch + L"：";
            for (size_t k = 0; k < readings.size() && k < 5; ++k) heteronyms += (k ? L"／" : L"") + W(readings[k]);
        }
        if (!heteronyms.empty()) hint += L"破音字，必要時請修改注音：" + heteronyms + L"\n";
    }
    hint += L"\n注音也可以用英文鍵盤打（大千排列），例如 cl3 = ㄏㄠˇ";
    SetDlgItemTextW(hwnd, IDC_PH_HINT, hint.c_str());
}

void SetReadingText(HWND hwnd, PhraseDialog* d, const std::string& reading) {
    d->settingReading = true;
    SetDlgItemTextW(hwnd, IDC_PH_READING, ShowReading(reading).c_str());
    d->settingReading = false;
}

INT_PTR CALLBACK PhraseDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<PhraseDialog*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            d = reinterpret_cast<PhraseDialog*>(lp);
            SetWindowLongPtrW(hwnd, DWLP_USER, lp);
            const wchar_t* what = d->kind == kUser ? L"自訂詞" : L"排除詞";
            SetWindowTextW(hwnd, ((d->editing ? L"修改" : L"新增") + std::wstring(what)).c_str());
            SetDlgItemTextW(hwnd, IDC_PH_VALUE, W(d->entry.value).c_str());
            SetReadingText(hwnd, d, d->entry.reading);
            d->readingTouched = d->editing && !d->entry.reading.empty();
            UpdatePhraseHint(hwnd, d);
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == IDC_PH_VALUE && code == EN_CHANGE) {
                if (!d->readingTouched && g.dict)
                    SetReadingText(hwnd, d, g.dict->GuessReading(U(Trim(GetText(GetDlgItem(hwnd, IDC_PH_VALUE))))));
                UpdatePhraseHint(hwnd, d);
            } else if (id == IDC_PH_READING && code == EN_CHANGE && !d->settingReading) {
                d->readingTouched = !Trim(GetText(GetDlgItem(hwnd, IDC_PH_READING))).empty();
            } else if (id == IDC_PH_READING && code == EN_KILLFOCUS) {
                const std::wstring text = GetText(GetDlgItem(hwnd, IDC_PH_READING));
                const std::string reading = ReadingFromInput(text);
                if (!reading.empty()) SetReadingText(hwnd, d, reading);
            } else if (id == IDOK) {
                const std::wstring value = Trim(GetText(GetDlgItem(hwnd, IDC_PH_VALUE)));
                const std::wstring readingText = Trim(GetText(GetDlgItem(hwnd, IDC_PH_READING)));
                std::string reading = ReadingFromInput(readingText);
                if (value.empty() || value.find_first_of(L" \t　") != std::wstring::npos) {
                    Ask(hwnd, L"請輸入詞（中間不能有空白）。", MB_ICONWARNING);
                    return TRUE;
                }
                if (!readingText.empty() && reading.empty()) {
                    Ask(hwnd, L"看不懂這個注音：" + readingText, MB_ICONWARNING);
                    return TRUE;
                }
                if (reading.empty() && d->kind == kUser && g.dict) reading = g.dict->GuessReading(U(value));
                if (reading.empty() && d->kind == kUser) {
                    Ask(hwnd, L"請輸入注音（有些字查不到讀音）。", MB_ICONWARNING);
                    return TRUE;
                }
                if (!reading.empty() && g.dict && g.dict->Loaded()) {
                    if (!g.dict->ValidReading(reading)) {
                        Ask(hwnd, L"注音裡有不存在的音節：" + ShowReading(reading), MB_ICONWARNING);
                        return TRUE;
                    }
                    const size_t chars = CodePoints(U(value));
                    const size_t syllables = Syllables(reading);
                    if (chars != syllables) {
                        Ask(hwnd, L"詞有 " + std::to_wstring(chars) + L" 個字，注音卻有 " + std::to_wstring(syllables) +
                                      L" 個音節。",
                            MB_ICONWARNING);
                        return TRUE;
                    }
                }
                d->entry = {U(value), reading};
                EndDialog(hwnd, IDOK);
            } else if (id == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
            }
            return TRUE;
        }
    }
    return FALSE;
}

bool EditPhrase(int kind, PhraseEntry* entry, bool editing) {
    PhraseDialog d;
    d.kind = kind;
    d.editing = editing;
    d.entry = *entry;
    if (DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_PHRASE), g.dlg, PhraseDialogProc, reinterpret_cast<LPARAM>(&d)) !=
        IDOK)
        return false;
    *entry = d.entry;
    return true;
}

std::vector<PhraseEntry> SelectedEntries(int kind) {
    std::vector<PhraseEntry> out;
    for (LPARAM i : SelectedData(Item(kind, OFS_LIST)))
        if (i >= 0 && static_cast<size_t>(i) < g.lists[kind].size()) out.push_back(g.lists[kind][static_cast<size_t>(i)]);
    return out;
}

const wchar_t* KindName(int kind) { return kind == kUser ? L"自訂詞" : L"排除詞"; }

void OnAdd(int kind) {
    PhraseEntry e;
    if (!EditPhrase(kind, &e, false)) return;
    size_t added = 0;
    AddEntries(kind, {e}, &added);
    if (SaveList(kind)) Status(added ? L"已加入「" + W(e.value) + L"」。切回打字的程式就會生效。" : L"這個詞已經在清單裡。");
    FillList(kind);
}

void OnEdit(int kind) {
    const auto selected = SelectedEntries(kind);
    if (selected.size() != 1) {
        Status(L"請先選一個詞。");
        return;
    }
    PhraseEntry e = selected[0];
    if (!EditPhrase(kind, &e, true) || SameEntry(e, selected[0])) return;
    RemoveEntries(kind, {selected[0]});
    g.lists[kind].push_back(e);
    if (SaveList(kind)) Status(L"已修改「" + W(e.value) + L"」。切回打字的程式就會生效。");
    FillList(kind);
}

void OnDelete(int kind) {
    const auto selected = SelectedEntries(kind);
    if (selected.empty()) {
        Status(L"請先選要刪除的詞。");
        return;
    }
    RemoveEntries(kind, selected);
    if (SaveList(kind)) Status(L"已刪除 " + std::to_wstring(selected.size()) + L" 個" + KindName(kind) + L"。");
    FillList(kind);
}

std::wstring PickFile(bool save, const std::wstring& defaultName) {
    wchar_t path[MAX_PATH] = {};
    lstrcpynW(path, defaultName.c_str(), MAX_PATH);
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = g.dlg;
    ofn.lpstrFilter = L"文字檔 (*.txt)\0*.txt\0所有檔案 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::wstring(path) : std::wstring();
}

void OnImport(int kind) {
    const std::wstring path = PickFile(false, L"");
    if (path.empty()) return;
    std::vector<PhraseEntry> incoming;
    size_t skipped = 0;
    for (auto& e : zy::phrase_file::Parse(zy::DecodeText(zy::ReadWholeFile(path)))) {
        if (e.reading.empty() && kind == kUser && g.dict) e.reading = g.dict->GuessReading(e.value);
        const bool bad = e.value.empty() || (kind == kUser && e.reading.empty()) ||
                         (!e.reading.empty() && g.dict && g.dict->Loaded() &&
                          (!g.dict->ValidReading(e.reading) || Syllables(e.reading) != CodePoints(e.value)));
        if (bad) {
            ++skipped;
            continue;
        }
        incoming.push_back(std::move(e));
    }
    size_t added = 0;
    AddEntries(kind, incoming, &added);
    if (SaveList(kind)) {
        std::wstring msg = L"匯入了 " + std::to_wstring(added) + L" 個" + KindName(kind);
        if (incoming.size() > added) msg += L"（" + std::to_wstring(incoming.size() - added) + L" 個已經有了）";
        if (skipped) msg += L"，略過 " + std::to_wstring(skipped) + L" 行看不懂或注音不對的";
        Status(msg + L"。");
    }
    FillList(kind);
}

void OnExport(int kind) {
    const std::wstring path = PickFile(true, std::wstring(KindName(kind)) + L".txt");
    if (path.empty()) return;
    LoadList(kind);
    const std::string text = "\xEF\xBB\xBF" + zy::phrase_file::Format(g.lists[kind], ListComments(kind));
    if (zy::WriteFileReplace(path, text)) Status(L"已匯出 " + std::to_wstring(g.lists[kind].size()) + L" 個" + KindName(kind) + L"。");
    else Ask(g.dlg, L"無法寫入 " + path, MB_ICONERROR);
    FillList(kind);
}

// ---- Page 4: learning ----

void LoadLearning() {
    g.learnRows.clear();
    if (!g.learner) return;
    g.learner->Sync();  // pick up what the IME learned meanwhile
    for (const auto& e : g.learner->Snapshot()) {
        if (e.type == 'L') g.learnRows.push_back({'L', e.reading, e.value, 0});
        else if (e.type == 'A') g.learnRows.push_back({'A', e.reading, e.value, e.count});
    }
    std::stable_sort(g.learnRows.begin(), g.learnRows.end(), [](const LearnRow& a, const LearnRow& b) {
        if (a.type != b.type) return a.type == 'L';
        return a.count > b.count;
    });
}

void FillLearning() {
    HWND list = GetDlgItem(g.dlg, IDC_LEARN_LIST);
    const std::wstring filter = Trim(GetText(GetDlgItem(g.dlg, IDC_LEARN_SEARCH)));
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    for (size_t i = 0; i < g.learnRows.size(); ++i) {
        const auto& r = g.learnRows[i];
        if (!Matches(filter, r.value, r.reading)) continue;
        wchar_t count[32] = L"";
        if (r.type == 'A') swprintf_s(count, L"%.1f", r.count);
        AddRow(list, {r.type == 'L' ? L"學到的詞" : L"常用字詞", W(r.value), ShowReading(r.reading), count},
               static_cast<LPARAM>(i));
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

std::vector<LearnRow> SelectedLearning() {
    std::vector<LearnRow> out;
    for (LPARAM i : SelectedData(GetDlgItem(g.dlg, IDC_LEARN_LIST)))
        if (i >= 0 && static_cast<size_t>(i) < g.learnRows.size()) out.push_back(g.learnRows[static_cast<size_t>(i)]);
    return out;
}

void OnForget() {
    const auto rows = SelectedLearning();
    if (rows.empty() || !g.learner) {
        Status(L"請先選要忘記的項目。");
        return;
    }
    for (const auto& r : rows) g.learner->Forget(r.reading, r.value);
    LoadLearning();
    FillLearning();
    Status(L"已忘記 " + std::to_wstring(rows.size()) + L" 項。切回打字的程式就會生效。");
}

void OnLearnedToUser() {
    std::vector<PhraseEntry> entries;
    for (const auto& r : SelectedLearning()) entries.push_back({r.value, r.reading});
    if (entries.empty()) {
        Status(L"請先選要改成自訂詞的項目。");
        return;
    }
    size_t added = 0;
    AddEntries(kUser, entries, &added);
    if (!SaveList(kUser)) return;
    // Now a user phrase: no need to keep it as learned too.
    if (g.learner)
        for (const auto& e : entries) g.learner->Forget(e.reading, e.value);
    LoadLearning();
    FillLearning();
    FillList(kUser);
    Status(L"已把 " + std::to_wstring(entries.size()) + L" 項改成自訂詞。");
}

void OnClearLearning() {
    if (Ask(g.dlg, L"要清除所有學習記錄嗎？常用字的排序和自動學到的詞都會回到預設，不能復原。",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    const std::wstring journal = UserFile(L"learning.txt");
    g.learner.reset();
    DeleteFileW(journal.c_str());
    DeleteFileW((journal + L".tmp").c_str());
    if (g.dict) {
        g.learner = std::make_unique<zy::Learner>(g.dict, journal);
        g.learner->Load();
    }
    LoadLearning();
    FillLearning();
    Status(zy::FileExists(journal) ? L"有程式正在使用學習記錄，請稍後再試。" : L"已清除所有學習記錄。");
}

// ---- Main window ----

std::vector<int> PageControls(int page) {
    switch (page) {
        case 0: return {IDC_PACKS_HINT, IDC_PACKS_LIST};
        case 1:
        case 2: {
            const int base = page == 1 ? IDC_USER_BASE : IDC_EXCL_BASE;
            std::vector<int> ids;
            for (int i = OFS_HINT; i <= OFS_EXPORT; ++i) ids.push_back(base + i);
            return ids;
        }
        case 3:
            return {IDC_LEARN_HINT, IDC_LEARN_SEARCH, IDC_LEARN_LIST, IDC_LEARN_FORGET, IDC_LEARN_TOUSER, IDC_LEARN_CLEAR};
        default:
            return {IDC_OPT_HINT,        IDC_OPT_ENTER_BOPOMOFO, IDC_OPT_CTRL_HINT,   IDC_OPT_CTRL_SEARCH,
                    IDC_OPT_CTRL_LIST,   IDC_OPT_CTRL_ALL,       IDC_OPT_CTRL_NONE,   IDC_OPT_CTRL_DEFAULT};
    }
}

// ---- Page 5: options (settings.ini [Options]) ----

zy::CtrlSymbolSet g_ctrlOff;  // Ctrl+symbol keys turned off, as in settings.ini

std::wstring LowerText(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

// Shown rows: those matching the search (shortcut, symbol typed, name or output).
void FillCtrlSymbols() {
    HWND list = GetDlgItem(g.dlg, IDC_OPT_CTRL_LIST);
    const std::wstring query = LowerText(GetText(GetDlgItem(g.dlg, IDC_OPT_CTRL_SEARCH)));
    g.filling = true;
    ListView_DeleteAllItems(list);
    for (size_t i = 0; i < std::size(zy::kCtrlSymbolKeys); ++i) {
        const auto& k = zy::kCtrlSymbolKeys[i];
        const std::wstring shortcut = zy::CtrlSymbolShortcut(k);
        const std::wstring typed(1, k.ascii);
        const std::wstring output(1, zy::Composer::FullWidthSymbol(k.ascii));
        if (!query.empty()) {
            const std::wstring haystack = LowerText(shortcut + L"\t" + typed + L"\t" + k.name + L"\t" + output);
            if (haystack.find(query) == std::wstring::npos) continue;
        }
        const int row = AddRow(list, {shortcut, typed, output, k.name}, static_cast<LPARAM>(i));
        ListView_SetCheckState(list, row, !g_ctrlOff.test(static_cast<size_t>(k.ascii)));
    }
    g.filling = false;
}

void SaveCtrlSymbols(const std::wstring& note) {
    zy::EnsureDirectory(g.userDir);
    WritePrivateProfileStringW(zy::kOptionsSection, zy::kCtrlSymbolsOffKey, zy::FormatCtrlSymbolSet(g_ctrlOff).c_str(),
                               UserFile(zy::kSettingsFile).c_str());
    Status(note + L"切回打字的程式就會生效。");
}

void OnCtrlSymbolToggled(int row) {
    HWND list = GetDlgItem(g.dlg, IDC_OPT_CTRL_LIST);
    LVITEMW item = {};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    ListView_GetItem(list, &item);
    if (item.lParam < 0 || static_cast<size_t>(item.lParam) >= std::size(zy::kCtrlSymbolKeys)) return;
    const auto& k = zy::kCtrlSymbolKeys[static_cast<size_t>(item.lParam)];
    const bool on = ListView_GetCheckState(list, row) != 0;
    if (on == !g_ctrlOff.test(static_cast<size_t>(k.ascii))) return;
    g_ctrlOff.set(static_cast<size_t>(k.ascii), !on);
    const std::wstring output(1, zy::Composer::FullWidthSymbol(k.ascii));
    SaveCtrlSymbols(zy::CtrlSymbolShortcut(k) + (on ? L" 會輸出「" + output + L"」。" : L" 交給程式處理。"));
}

// 全選 / 全不選 apply to the rows shown (all of them unless searching).
void SetShownCtrlSymbols(bool on) {
    HWND list = GetDlgItem(g.dlg, IDC_OPT_CTRL_LIST);
    const int count = ListView_GetItemCount(list);
    for (int row = 0; row < count; ++row) {
        LVITEMW item = {};
        item.mask = LVIF_PARAM;
        item.iItem = row;
        ListView_GetItem(list, &item);
        if (item.lParam >= 0 && static_cast<size_t>(item.lParam) < std::size(zy::kCtrlSymbolKeys))
            g_ctrlOff.set(static_cast<size_t>(zy::kCtrlSymbolKeys[static_cast<size_t>(item.lParam)].ascii), !on);
    }
    FillCtrlSymbols();
    const bool all = count == static_cast<int>(std::size(zy::kCtrlSymbolKeys));
    SaveCtrlSymbols(std::wstring(all ? L"全部" : L"列出的 ") + (all ? L"" : std::to_wstring(count) + L" 個") +
                    (on ? L"都會輸出全形符號。" : L"都交給程式處理。"));
}

void FillOptions() {
    const std::wstring ini = UserFile(zy::kSettingsFile);
    const bool enter = GetPrivateProfileIntW(zy::kOptionsSection, zy::kEnterCommitsBopomofoKey, 1, ini.c_str()) != 0;
    CheckDlgButton(g.dlg, IDC_OPT_ENTER_BOPOMOFO, enter ? BST_CHECKED : BST_UNCHECKED);
    wchar_t off[512] = {};
    GetPrivateProfileStringW(zy::kOptionsSection, zy::kCtrlSymbolsOffKey, zy::kCtrlSymbolsOffDefault, off,
                             static_cast<DWORD>(std::size(off)), ini.c_str());
    g_ctrlOff = zy::ParseCtrlSymbolSet(off);
    FillCtrlSymbols();
}

void OnOptionToggled(int id) {
    const bool on = IsDlgButtonChecked(g.dlg, id) == BST_CHECKED;
    zy::EnsureDirectory(g.userDir);
    if (id == IDC_OPT_ENTER_BOPOMOFO) {
        WritePrivateProfileStringW(zy::kOptionsSection, zy::kEnterCommitsBopomofoKey, on ? L"1" : L"0",
                                   UserFile(zy::kSettingsFile).c_str());
        Status(on ? L"已開啟：只打了注音時按 Enter 會輸出注音。切回打字的程式就會生效。"
                  : L"已關閉：只打了注音時按 Enter 不會輸出任何字。切回打字的程式就會生效。");
    }
}

void ShowPage(int page) {
    g.page = page;
    for (int p = 0; p < kPageCount; ++p)
        for (int id : PageControls(p)) ShowWindow(GetDlgItem(g.dlg, id), p == page ? SW_SHOW : SW_HIDE);
    Status(L"");
    if (page == 0) {
        if (g.dict) g.dict->Reload();
        FillPacks();
    } else if (page == 1 || page == 2) {
        const int kind = page == 1 ? kUser : kExcluded;
        LoadList(kind);
        FillList(kind);
    } else if (page == 3) {
        LoadLearning();
        FillLearning();
    } else {
        FillOptions();
    }
}

void SetupList(HWND list, DWORD extra) {
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | extra);
}

void InitMainDialog(HWND hwnd) {
    g.dlg = hwnd;
    const HICON icon = LoadIconW(g.inst, MAKEINTRESOURCEW(IDI_APP));
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));

    HWND tab = GetDlgItem(hwnd, IDC_TAB);
    const wchar_t* titles[kPageCount] = {L"詞庫包", L"自訂詞", L"排除詞", L"學習記錄", L"選項"};
    for (int i = 0; i < kPageCount; ++i) {
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(titles[i]);
        TabCtrl_InsertItem(tab, i, &item);
    }
    // The page controls sit on top of the tab control.
    SetWindowPos(tab, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    SetDlgItemTextW(hwnd, IDC_PACKS_HINT,
                    L"勾選要使用的詞庫包，取消勾選就停用。改完切回打字的程式就會生效。\n"
                    L"只想拿掉個別不要的詞，請用「排除詞」。");
    HWND packs = GetDlgItem(hwnd, IDC_PACKS_LIST);
    SetupList(packs, LVS_EX_CHECKBOXES);
    AddColumn(packs, 0, L"詞庫包", 90);
    AddColumn(packs, 1, L"詞數", 40);
    AddColumn(packs, 2, L"說明", 228);

    SetDlgItemTextW(hwnd, IDC_USER_BASE + OFS_HINT,
                    L"自己加的詞，永遠排在候選字最前面。存在 user-phrases.txt。");
    SetDlgItemTextW(hwnd, IDC_EXCL_BASE + OFS_HINT,
                    L"被排除的詞不會出現在候選字裡（單字照樣能組出來）。存在 excluded-phrases.txt。");
    for (int kind : {kUser, kExcluded}) {
        HWND list = Item(kind, OFS_LIST);
        SetupList(list, 0);
        AddColumn(list, 0, L"詞", 120);
        AddColumn(list, 1, L"注音", 160);
        SendMessageW(Item(kind, OFS_SEARCH), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"搜尋詞或注音"));
    }

    SetDlgItemTextW(hwnd, IDC_LEARN_HINT,
                    L"輸入法學到的東西：自動學到的詞，和常選的字詞（次數會隨時間減少）。\n"
                    L"選錯的可以按「忘記」；存在 learning.txt，所有程式共用。");
    HWND learn = GetDlgItem(hwnd, IDC_LEARN_LIST);
    SetupList(learn, 0);
    AddColumn(learn, 0, L"類型", 54);
    AddColumn(learn, 1, L"字詞", 80);
    AddColumn(learn, 2, L"注音", 110);
    AddColumn(learn, 3, L"次數", 36);
    SendMessageW(GetDlgItem(hwnd, IDC_LEARN_SEARCH), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"搜尋字詞或注音"));

    SetDlgItemTextW(hwnd, IDC_OPT_HINT, L"輸入法的行為設定。改完切回打字的程式就會生效。");
    SetDlgItemTextW(hwnd, IDC_OPT_CTRL_HINT,
                    L"Ctrl + 標點符號：勾選的會輸出全形符號（中英文模式都一樣）；不勾的交給程式，\n"
                    L"例如 VS Code 的 Ctrl+/ 註解。全形符號也可以用 ` 加符號鍵輸入。");
    HWND ctrlList = GetDlgItem(hwnd, IDC_OPT_CTRL_LIST);
    SetupList(ctrlList, LVS_EX_CHECKBOXES);
    AddColumn(ctrlList, 0, L"快速鍵", 92);
    AddColumn(ctrlList, 1, L"按鍵", 30);
    AddColumn(ctrlList, 2, L"輸出", 30);
    AddColumn(ctrlList, 3, L"名稱", 120);
    SendMessageW(GetDlgItem(hwnd, IDC_OPT_CTRL_SEARCH), EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"搜尋快速鍵、符號或名稱"));

    ShowPage(0);
    if (g.dataDir.empty() || !g.dict || !g.dict->Loaded())
        Status(L"找不到詞庫（data\\packs.txt），只能編輯清單，無法自動標注音。");
}

int KindOfControl(int id) {
    if (id >= IDC_USER_BASE && id <= IDC_USER_BASE + OFS_EXPORT) return kUser;
    if (id >= IDC_EXCL_BASE && id <= IDC_EXCL_BASE + OFS_EXPORT) return kExcluded;
    return -1;
}

INT_PTR CALLBACK MainDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            InitMainDialog(hwnd);
            return TRUE;
        case WM_NOTIFY: {
            const auto* hdr = reinterpret_cast<const NMHDR*>(lp);
            if (hdr->idFrom == IDC_TAB && hdr->code == TCN_SELCHANGE) {
                ShowPage(TabCtrl_GetCurSel(hdr->hwndFrom));
            } else if (hdr->idFrom == IDC_PACKS_LIST && hdr->code == LVN_ITEMCHANGED && !g.filling) {
                const auto* nm = reinterpret_cast<const NMLISTVIEW*>(lp);
                if ((nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK) &&
                    (nm->uOldState & LVIS_STATEIMAGEMASK))
                    OnPackToggled(nm->iItem);
            } else if (hdr->idFrom == IDC_OPT_CTRL_LIST && hdr->code == LVN_ITEMCHANGED && !g.filling) {
                const auto* nm = reinterpret_cast<const NMLISTVIEW*>(lp);
                if ((nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK) &&
                    (nm->uOldState & LVIS_STATEIMAGEMASK))
                    OnCtrlSymbolToggled(nm->iItem);
            } else if (const int kind = KindOfControl(static_cast<int>(hdr->idFrom)); kind >= 0) {
                if (hdr->code == NM_DBLCLK) OnEdit(kind);
                else if (hdr->code == LVN_KEYDOWN && reinterpret_cast<const NMLVKEYDOWN*>(lp)->wVKey == VK_DELETE)
                    OnDelete(kind);
            } else if (hdr->idFrom == IDC_LEARN_LIST && hdr->code == LVN_KEYDOWN &&
                       reinterpret_cast<const NMLVKEYDOWN*>(lp)->wVKey == VK_DELETE) {
                OnForget();
            }
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (const int kind = KindOfControl(id); kind >= 0) {
                switch (id - Base(kind)) {
                    case OFS_SEARCH:
                        if (code == EN_CHANGE) FillList(kind);
                        break;
                    case OFS_ADD: OnAdd(kind); break;
                    case OFS_EDIT: OnEdit(kind); break;
                    case OFS_DELETE: OnDelete(kind); break;
                    case OFS_IMPORT: OnImport(kind); break;
                    case OFS_EXPORT: OnExport(kind); break;
                    default: break;
                }
                return TRUE;
            }
            switch (id) {
                case IDC_LEARN_SEARCH:
                    if (code == EN_CHANGE) FillLearning();
                    break;
                case IDC_LEARN_FORGET: OnForget(); break;
                case IDC_LEARN_TOUSER: OnLearnedToUser(); break;
                case IDC_LEARN_CLEAR: OnClearLearning(); break;
                case IDC_OPT_ENTER_BOPOMOFO:
                    if (code == BN_CLICKED) OnOptionToggled(id);
                    break;
                case IDC_OPT_CTRL_SEARCH:
                    if (code == EN_CHANGE) FillCtrlSymbols();
                    break;
                case IDC_OPT_CTRL_ALL: SetShownCtrlSymbols(true); break;
                case IDC_OPT_CTRL_NONE: SetShownCtrlSymbols(false); break;
                case IDC_OPT_CTRL_DEFAULT:
                    g_ctrlOff = zy::ParseCtrlSymbolSet(zy::kCtrlSymbolsOffDefault);
                    FillCtrlSymbols();
                    SaveCtrlSymbols(L"已還原預設：只有 Ctrl+/ 交給程式。");
                    break;
                case IDC_OPEN_FOLDER:
                    zy::EnsureDirectory(g.userDir);
                    ShellExecuteW(hwnd, L"open", g.userDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                case IDCANCEL:
                    EndDialog(hwnd, 0);
                    break;
                default: break;
            }
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            // The hints sit on the tab page, which is painted white.
            const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            if (id == IDC_PACKS_HINT || id == IDC_USER_BASE + OFS_HINT || id == IDC_EXCL_BASE + OFS_HINT ||
                id == IDC_LEARN_HINT || id == IDC_OPT_HINT || id == IDC_OPT_ENTER_BOPOMOFO ||
                id == IDC_OPT_CTRL_HINT) {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetBkColor(dc, GetSysColor(COLOR_WINDOW));
                SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
                return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
            }
            return FALSE;
        }
        case WM_CLOSE:
            EndDialog(hwnd, 0);
            return TRUE;
    }
    return FALSE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    // One window is enough: a second start brings the first one forward.
    HANDLE instance = CreateMutexW(nullptr, FALSE, kInstanceMutex);
    if (instance && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"#32770", kTitle)) {
            if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    g.inst = inst;
    g.dataDir = FindDataDirectory();
    g.userDir = FindUserDirectory();
    if (g.userDir.empty()) {
        MessageBoxW(nullptr, L"找不到使用者資料夾（%APPDATA%）。", kTitle, MB_ICONERROR);
        return 1;
    }
    if (!g.dataDir.empty()) {
        g.dict = std::make_shared<zy::Dictionary>(g.dataDir, g.userDir);
        g.dict->Reload();
        g.learner = std::make_unique<zy::Learner>(g.dict, g.userDir + L"\\learning.txt");
        g.learner->Load();
    }
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainDialogProc, 0);
    if (g.learner) g.learner->Sync();  // write out anything forgotten just before closing
    if (instance) CloseHandle(instance);
    return 0;
}
