// 微新注音整合器 (IME pad): handwriting recognition with the readings of each character, and a
// symbol picker. A small always-on-top window that never takes the focus: clicking it leaves the
// keyboard focus in the program you are typing in, and the chosen character is sent there with
// SendInput. Everything is painted by hand (no child controls, which would grab the focus).
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/Dictionary.h"
#include "engine/Files.h"
#include "engine/Utf.h"
#include "pad/Handwriting.h"

namespace {

constexpr wchar_t kClassName[] = L"IdealismIME_Pad";
constexpr wchar_t kTitle[] = L"微新注音整合器";
constexpr wchar_t kInstanceMutex[] = L"Local\\IdealismIME.Pad";

// Layout, in 96-dpi pixels.
constexpr int kWidth = 640;
constexpr int kHeight = 400;
constexpr int kTabTop = 6, kTabBottom = 30;
constexpr int kTop = 38;
constexpr int kBoxSize = 300;
constexpr int kResultLeft = 20 + kBoxSize;
constexpr int kResultCell = 56, kResultPitch = 60, kResultColumns = 5, kResultCount = 10;
constexpr int kCategoryRow = 17;
constexpr int kSymbolCell = 36;
constexpr int kScrollbar = 12;

constexpr COLORREF kBack = RGB(250, 250, 250);
constexpr COLORREF kPanel = RGB(255, 255, 255);
constexpr COLORREF kLine = RGB(200, 200, 200);
constexpr COLORREF kGuide = RGB(225, 225, 225);
constexpr COLORREF kAccent = RGB(204, 232, 255);
constexpr COLORREF kHover = RGB(229, 243, 255);
constexpr COLORREF kText = RGB(0, 0, 0);
constexpr COLORREF kDim = RGB(110, 110, 110);
constexpr COLORREF kInk = RGB(20, 20, 20);

// ---- Symbol categories ----

struct Range {
    char32_t first, last;
};
struct Category {
    const wchar_t* name;
    std::vector<Range> ranges;
};

const std::vector<Category>& Categories() {
    static const std::vector<Category> categories = {
        {L"中文標點", {{0x3001, 0x3003}, {0x3008, 0x3011}, {0x3014, 0x301F}, {0xFF01, 0xFF0F}, {0xFF1A, 0xFF20},
                       {0xFF3B, 0xFF40}, {0xFF5B, 0xFF5E}, {0x2018, 0x201F}, {0x2025, 0x2027}, {0x2032, 0x2035},
                       {0xFE30, 0xFE44}, {0xFE49, 0xFE52}, {0xFE54, 0xFE57}, {0xFE59, 0xFE66}, {0xFE68, 0xFE6B}}},
        {L"全形英數", {{0xFF10, 0xFF19}, {0xFF21, 0xFF3A}, {0xFF41, 0xFF5A}}},
        {L"拉丁字母", {{0x00C0, 0x00D6}, {0x00D8, 0x00F6}, {0x00F8, 0x024F}, {0x1E00, 0x1EFF}}},
        {L"國際音標", {{0x0250, 0x02AF}, {0x02B0, 0x02FF}}},
        {L"希臘字母", {{0x0386, 0x0386}, {0x0388, 0x038A}, {0x038C, 0x038C}, {0x038E, 0x03A1}, {0x03A3, 0x03CE}}},
        {L"西里爾字母", {{0x0400, 0x045F}}},
        {L"平假名", {{0x3041, 0x3096}, {0x309B, 0x309F}}},
        {L"片假名", {{0x30A0, 0x30FF}, {0x31F0, 0x31FF}, {0xFF66, 0xFF9F}}},
        {L"注音符號", {{0x3105, 0x312F}, {0x31A0, 0x31BA}, {0x02C9, 0x02CB}, {0x02C7, 0x02C7}, {0x02D9, 0x02D9}}},
        {L"數學符號", {{0x00B1, 0x00B1}, {0x00D7, 0x00D7}, {0x00F7, 0x00F7}, {0x2200, 0x22FF}, {0x2A00, 0x2A1F}}},
        {L"上下標", {{0x2070, 0x2071}, {0x2074, 0x208E}, {0x2090, 0x209C}, {0x00B2, 0x00B3}, {0x00B9, 0x00B9}}},
        {L"箭頭", {{0x2190, 0x21FF}, {0x27F0, 0x27FF}, {0x2900, 0x297F}}},
        {L"序號", {{0x2460, 0x24FF}, {0x2776, 0x2793}, {0x2150, 0x218B}, {0x3220, 0x3243}, {0x3280, 0x32B0}}},
        {L"單位", {{0x3371, 0x33FF}, {0x2100, 0x214F}}},
        {L"貨幣", {{0x0024, 0x0024}, {0x00A2, 0x00A5}, {0x20A0, 0x20C0}, {0xFFE0, 0xFFE6}}},
        {L"幾何圖形", {{0x25A0, 0x25FF}, {0x2B12, 0x2B2F}}},
        {L"雜項符號", {{0x2600, 0x26FF}, {0x2700, 0x2775}, {0x2794, 0x27BF}}},
        {L"框線方塊", {{0x2500, 0x259F}}},
        {L"表情符號", {{0x1F600, 0x1F64F}, {0x1F910, 0x1F92F}, {0x1F300, 0x1F37F}}},
    };
    return categories;
}

std::wstring FromCodePoint(char32_t c) {
    if (c < 0x10000) return std::wstring(1, static_cast<wchar_t>(c));
    c -= 0x10000;
    return {static_cast<wchar_t>(0xD800 + (c >> 10)), static_cast<wchar_t>(0xDC00 + (c & 0x3FF))};
}

char32_t ToCodePoint(const std::wstring& s) {
    if (s.empty()) return 0;
    if (s.size() >= 2 && s[0] >= 0xD800 && s[0] <= 0xDBFF)
        return 0x10000 + ((static_cast<char32_t>(s[0]) - 0xD800) << 10) + (s[1] - 0xDC00);
    return s[0];
}

// ---- State ----

enum class Page { Handwriting, Symbols };

struct Pad {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    UINT dpi = 96;
    Page page = Page::Handwriting;

    zy::HandwritingRecognizer hwr;
    bool hwrReady = false;
    std::vector<std::vector<POINT>> strokes;  // in 96-dpi box coordinates
    bool drawing = false;
    std::vector<std::wstring> results;
    int hoverResult = -1;
    std::wstring info;  // the line under the results: a character and its readings

    size_t category = 0;
    std::vector<std::wstring> symbols;
    int symbolScroll = 0;  // first visible row
    int categoryScroll = 0;
    int hoverSymbol = -1;
    int hoverCategory = -1;
    bool draggingThumb = false;
    int dragOffset = 0;

    std::shared_ptr<zy::Dictionary> dict;
    std::wstring exeDir;
};
Pad g;

int Px(int v) { return MulDiv(v, static_cast<int>(g.dpi), 96); }
RECT R(int l, int t, int r, int b) { return {Px(l), Px(t), Px(r), Px(b)}; }
bool Inside(const RECT& rc, POINT pt) { return PtInRect(&rc, pt) != FALSE; }

RECT TabRect(int i) { return R(8 + i * 68, kTabTop, 72 + i * 68, kTabBottom); }
RECT SettingsRect() { return R(kWidth - 80, kTabTop, kWidth - 8, kTabBottom); }
RECT BoxRect() { return R(8, kTop, 8 + kBoxSize, kTop + kBoxSize); }
RECT ResultRect(int i) {
    const int x = kResultLeft + (i % kResultColumns) * kResultPitch, y = kTop + (i / kResultColumns) * kResultPitch;
    return R(x, y, x + kResultCell, y + kResultCell);
}
RECT InfoRect() { return R(kResultLeft, kTop + 2 * kResultPitch + 6, kWidth - 8, kTop + kBoxSize - 40); }
RECT ClearRect() { return R(kResultLeft, kTop + kBoxSize - 30, kResultLeft + 80, kTop + kBoxSize); }
RECT UndoRect() { return R(kResultLeft + 88, kTop + kBoxSize - 30, kResultLeft + 184, kTop + kBoxSize); }
RECT CategoryListRect() { return R(8, kTop, 128, kHeight - 28); }
RECT GridRect() { return R(136, kTop, kWidth - 10 - kScrollbar, kHeight - 28); }
RECT ScrollRect() { return R(kWidth - 8 - kScrollbar, kTop, kWidth - 8, kHeight - 28); }
RECT StatusRect() { return R(8, kHeight - 24, kWidth - 8, kHeight - 4); }

int GridColumns() { return std::max(1, (kWidth - 10 - kScrollbar - 136) / kSymbolCell); }
int GridRows() { return std::max(1, (kHeight - 28 - kTop) / kSymbolCell); }
int SymbolRows() { return static_cast<int>((g.symbols.size() + GridColumns() - 1) / GridColumns()); }
int MaxSymbolScroll() { return std::max(0, SymbolRows() - GridRows()); }
int VisibleCategories() { return (kHeight - 28 - kTop) / kCategoryRow; }

RECT SymbolRect(int index) {  // index into g.symbols, for the current scroll position
    const int row = index / GridColumns() - g.symbolScroll, col = index % GridColumns();
    const int x = 136 + col * kSymbolCell, y = kTop + row * kSymbolCell;
    return R(x, y, x + kSymbolCell - 2, y + kSymbolCell - 2);
}

RECT ThumbRect() {
    const RECT track = ScrollRect();
    const int rows = std::max(SymbolRows(), 1);
    const int height = track.bottom - track.top;
    const int thumb = std::max(Px(20), height * std::min(GridRows(), rows) / rows);
    const int maxScroll = MaxSymbolScroll();
    const int top = maxScroll ? track.top + (height - thumb) * g.symbolScroll / maxScroll : track.top;
    return {track.left + Px(2), top, track.right - Px(2), top + thumb};
}

// ---- Actions ----

void Invalidate() { InvalidateRect(g.hwnd, nullptr, FALSE); }

// "好　ㄏㄠˇ／ㄏㄠˋ　U+597D"
std::wstring Describe(const std::wstring& ch) {
    if (ch.empty()) return {};
    std::wstring text = ch + L"　";
    std::vector<std::string> readings;
    if (g.dict) readings = g.dict->ReadingsOf(zy::WideToUtf8(ch));
    if (!readings.empty()) {
        for (size_t i = 0; i < readings.size(); ++i) text += (i ? L"／" : L"") + zy::Utf8ToWide(readings[i]);
    }
    wchar_t code[16];
    swprintf_s(code, L"　U+%04X", static_cast<unsigned>(ToCodePoint(ch)));
    return text + code;
}

void SendText(const std::wstring& text) {
    std::vector<INPUT> inputs;
    for (wchar_t c : text) {
        INPUT down = {};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = c;
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(down);
        inputs.push_back(up);
    }
    if (!inputs.empty()) SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void Recognize() {
    g.results.clear();
    if (g.hwrReady && !g.strokes.empty()) g.results = g.hwr.Recognize(g.strokes, {kBoxSize, kBoxSize}, kResultCount);
    g.info = g.results.empty() ? std::wstring() : L"第一個字：" + Describe(g.results[0]);
}

void ClearInk() {
    g.strokes.clear();
    g.results.clear();
    g.hoverResult = -1;
}

void PickResult(int i) {
    if (i < 0 || i >= static_cast<int>(g.results.size())) return;
    const std::wstring ch = g.results[i];
    SendText(ch);
    ClearInk();
    g.info = L"剛輸入：" + Describe(ch);  // the readings stay visible after the character is typed
}

void SelectCategory(size_t i) {
    if (i >= Categories().size()) return;
    g.category = i;
    g.symbols.clear();
    for (const auto& range : Categories()[i].ranges)
        for (char32_t c = range.first; c <= range.last; ++c) g.symbols.push_back(FromCodePoint(c));
    g.symbolScroll = 0;
    g.hoverSymbol = -1;
}

void ScrollSymbols(int rows) { g.symbolScroll = std::clamp(g.symbolScroll + rows, 0, MaxSymbolScroll()); }

void OpenSettings() {
    const std::wstring exe = g.exeDir + L"\\IdealismSettings.exe";
    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---- Painting ----

HFONT Font(int size, int weight = FW_NORMAL) {
    return CreateFontW(-Px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");
}

void Fill(HDC dc, const RECT& rc, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

void Frame(HDC dc, const RECT& rc, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FrameRect(dc, &rc, b);
    DeleteObject(b);
}

void Text(HDC dc, const std::wstring& s, RECT rc, UINT flags, COLORREF color = kText) {
    SetTextColor(dc, color);
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &rc, flags | DT_NOPREFIX);
}

void Button(HDC dc, const RECT& rc, const wchar_t* label, bool selected, HFONT font) {
    Fill(dc, rc, selected ? kAccent : kPanel);
    Frame(dc, rc, kLine);
    SelectObject(dc, font);
    Text(dc, label, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void PaintHandwriting(HDC dc, HFONT ui, HFONT big, HFONT tiny) {
    const RECT box = BoxRect();
    Fill(dc, box, kPanel);
    // Guides: a dashed cross, like squared writing paper.
    HPEN guide = CreatePen(PS_DOT, 1, kGuide);
    HGDIOBJ oldPen = SelectObject(dc, guide);
    const int cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
    MoveToEx(dc, cx, box.top, nullptr);
    LineTo(dc, cx, box.bottom);
    MoveToEx(dc, box.left, cy, nullptr);
    LineTo(dc, box.right, cy);
    SelectObject(dc, oldPen);
    DeleteObject(guide);
    Frame(dc, box, kLine);

    HPEN ink = CreatePen(PS_SOLID, std::max(2, Px(4)), kInk);
    oldPen = SelectObject(dc, ink);
    for (const auto& stroke : g.strokes) {
        if (stroke.empty()) continue;
        MoveToEx(dc, box.left + Px(stroke[0].x), box.top + Px(stroke[0].y), nullptr);
        if (stroke.size() == 1) LineTo(dc, box.left + Px(stroke[0].x) + 1, box.top + Px(stroke[0].y));
        for (size_t i = 1; i < stroke.size(); ++i) LineTo(dc, box.left + Px(stroke[i].x), box.top + Px(stroke[i].y));
    }
    SelectObject(dc, oldPen);
    DeleteObject(ink);
    if (g.strokes.empty()) {
        SelectObject(dc, ui);
        RECT hint = box;
        hint.top = box.bottom - Px(28);
        Text(dc, g.hwrReady ? L"在這裡寫一個字" : L"沒有安裝繁體中文手寫辨識", hint,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE, kDim);
    }

    for (int i = 0; i < kResultCount; ++i) {
        const RECT rc = ResultRect(i);
        const bool has = i < static_cast<int>(g.results.size());
        Fill(dc, rc, has && i == g.hoverResult ? kHover : kPanel);
        Frame(dc, rc, kLine);
        if (!has) continue;
        SelectObject(dc, big);
        Text(dc, g.results[i], rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, tiny);
        wchar_t n[4];
        swprintf_s(n, L"%d", (i + 1) % 10);
        RECT nr = {rc.left + Px(3), rc.top + Px(1), rc.right, rc.bottom};
        Text(dc, n, nr, DT_LEFT | DT_TOP | DT_SINGLELINE, kDim);
    }
    SelectObject(dc, ui);
    std::wstring info = g.hoverResult >= 0 && g.hoverResult < static_cast<int>(g.results.size())
                            ? Describe(g.results[g.hoverResult])
                            : g.info;
    if (info.empty()) info = L"寫好後按下面的候選字就會送到正在打字的程式。滑鼠移到候選字上會顯示它的所有讀音。";
    Text(dc, info, InfoRect(), DT_LEFT | DT_TOP | DT_WORDBREAK);
    Button(dc, ClearRect(), L"清除", false, ui);
    Button(dc, UndoRect(), L"退回一筆", false, ui);
}

void PaintSymbols(HDC dc, HFONT ui, HFONT symbolFont) {
    const RECT list = CategoryListRect();
    Fill(dc, list, kPanel);
    Frame(dc, list, kLine);
    SelectObject(dc, ui);
    const auto& categories = Categories();
    for (int row = 0; row < VisibleCategories(); ++row) {
        const int i = row + g.categoryScroll;
        if (i >= static_cast<int>(categories.size())) break;
        RECT rc = {list.left + 1, list.top + Px(row * kCategoryRow) + 1, list.right - 1,
                   list.top + Px((row + 1) * kCategoryRow)};
        if (static_cast<size_t>(i) == g.category) Fill(dc, rc, kAccent);
        else if (i == g.hoverCategory) Fill(dc, rc, kHover);
        rc.left += Px(6);
        Text(dc, categories[i].name, rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    const RECT grid = GridRect();
    Fill(dc, grid, kPanel);
    HRGN clip = CreateRectRgnIndirect(&grid);
    SelectClipRgn(dc, clip);
    SelectObject(dc, symbolFont);
    const int first = g.symbolScroll * GridColumns();
    const int last = std::min(static_cast<int>(g.symbols.size()), first + (GridRows() + 1) * GridColumns());
    for (int i = first; i < last; ++i) {
        const RECT rc = SymbolRect(i);
        Fill(dc, rc, i == g.hoverSymbol ? kHover : kPanel);
        Frame(dc, rc, kGuide);
        Text(dc, g.symbols[i], rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
    Frame(dc, grid, kLine);

    const RECT track = ScrollRect();
    Fill(dc, track, RGB(240, 240, 240));
    if (MaxSymbolScroll() > 0) Fill(dc, ThumbRect(), RGB(190, 190, 190));
}

void Paint(HDC target, const RECT& client) {
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    Fill(dc, client, kBack);
    SetBkMode(dc, TRANSPARENT);

    HFONT ui = Font(13);
    HFONT big = Font(28);
    HFONT tiny = Font(10);
    HFONT symbolFont = Font(20);
    HGDIOBJ oldFont = SelectObject(dc, ui);

    Button(dc, TabRect(0), L"手寫", g.page == Page::Handwriting, ui);
    Button(dc, TabRect(1), L"符號", g.page == Page::Symbols, ui);
    Button(dc, SettingsRect(), L"設定…", false, ui);
    if (g.page == Page::Handwriting) {
        PaintHandwriting(dc, ui, big, tiny);
    } else {
        PaintSymbols(dc, ui, symbolFont);
        SelectObject(dc, ui);
        std::wstring status = g.hoverSymbol >= 0 && g.hoverSymbol < static_cast<int>(g.symbols.size())
                                  ? Describe(g.symbols[g.hoverSymbol])
                                  : std::wstring(L"按一下符號就會送到正在打字的程式。");
        Text(dc, status, StatusRect(), DT_LEFT | DT_VCENTER | DT_SINGLELINE, kDim);
    }

    SelectObject(dc, oldFont);
    for (HFONT f : {ui, big, tiny, symbolFont}) DeleteObject(f);
    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// ---- Input ----

POINT ToBox(POINT pt) {
    const RECT box = BoxRect();
    const int x = std::clamp(static_cast<int>(pt.x - box.left), 0, static_cast<int>(box.right - box.left));
    const int y = std::clamp(static_cast<int>(pt.y - box.top), 0, static_cast<int>(box.bottom - box.top));
    return {MulDiv(x, 96, static_cast<int>(g.dpi)), MulDiv(y, 96, static_cast<int>(g.dpi))};
}

void OnLeftDown(POINT pt) {
    for (int i = 0; i < 2; ++i)
        if (Inside(TabRect(i), pt)) {
            g.page = i == 0 ? Page::Handwriting : Page::Symbols;
            Invalidate();
            return;
        }
    if (Inside(SettingsRect(), pt)) {
        OpenSettings();
        return;
    }
    if (g.page == Page::Handwriting) {
        if (Inside(BoxRect(), pt)) {
            g.drawing = true;
            g.strokes.push_back({ToBox(pt)});
            SetCapture(g.hwnd);
        } else if (Inside(ClearRect(), pt)) {
            ClearInk();
            g.info.clear();
        } else if (Inside(UndoRect(), pt)) {
            if (!g.strokes.empty()) g.strokes.pop_back();
            Recognize();
        } else {
            for (int i = 0; i < kResultCount; ++i)
                if (Inside(ResultRect(i), pt)) PickResult(i);
        }
        Invalidate();
        return;
    }
    // Symbols page
    const RECT list = CategoryListRect();
    if (Inside(list, pt)) {
        const int row = (pt.y - list.top) / Px(kCategoryRow) + g.categoryScroll;
        SelectCategory(static_cast<size_t>(row));
    } else if (Inside(ScrollRect(), pt)) {
        const RECT thumb = ThumbRect();
        if (Inside(thumb, pt)) {
            g.draggingThumb = true;
            g.dragOffset = pt.y - thumb.top;
            SetCapture(g.hwnd);
        } else {
            ScrollSymbols(pt.y < thumb.top ? -GridRows() : GridRows());
        }
    } else if (Inside(GridRect(), pt)) {
        for (int i = g.symbolScroll * GridColumns(); i < static_cast<int>(g.symbols.size()); ++i)
            if (Inside(SymbolRect(i), pt)) {
                SendText(g.symbols[i]);
                break;
            }
    }
    Invalidate();
}

void OnMouseMove(POINT pt) {
    if (g.drawing) {
        auto& stroke = g.strokes.back();
        const POINT p = ToBox(pt);
        if (stroke.back().x != p.x || stroke.back().y != p.y) stroke.push_back(p);
        const RECT box = BoxRect();
        InvalidateRect(g.hwnd, &box, FALSE);
        return;
    }
    if (g.draggingThumb) {
        const RECT track = ScrollRect();
        const RECT thumb = ThumbRect();
        const int span = (track.bottom - track.top) - (thumb.bottom - thumb.top);
        if (span > 0) {
            const int top = std::clamp(static_cast<int>(pt.y - g.dragOffset - track.top), 0, span);
            g.symbolScroll = MulDiv(top, MaxSymbolScroll(), span);
        }
        Invalidate();
        return;
    }
    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, g.hwnd, 0};
    TrackMouseEvent(&tme);
    int hoverResult = -1, hoverSymbol = -1, hoverCategory = -1;
    if (g.page == Page::Handwriting) {
        for (int i = 0; i < static_cast<int>(g.results.size()); ++i)
            if (Inside(ResultRect(i), pt)) hoverResult = i;
    } else if (Inside(GridRect(), pt)) {
        const int last = std::min(static_cast<int>(g.symbols.size()), (g.symbolScroll + GridRows() + 1) * GridColumns());
        for (int i = g.symbolScroll * GridColumns(); i < last; ++i)
            if (Inside(SymbolRect(i), pt)) hoverSymbol = i;
    } else if (Inside(CategoryListRect(), pt)) {
        hoverCategory = (pt.y - CategoryListRect().top) / Px(kCategoryRow) + g.categoryScroll;
    }
    if (hoverResult != g.hoverResult || hoverSymbol != g.hoverSymbol || hoverCategory != g.hoverCategory) {
        g.hoverResult = hoverResult;
        g.hoverSymbol = hoverSymbol;
        g.hoverCategory = hoverCategory;
        Invalidate();
    }
}

void OnLeftUp() {
    if (g.drawing) {
        g.drawing = false;
        ReleaseCapture();
        Recognize();
        Invalidate();
    }
    if (g.draggingThumb) {
        g.draggingThumb = false;
        ReleaseCapture();
    }
}

void OnWheel(POINT screen, int delta) {
    if (g.page != Page::Symbols) return;
    POINT pt = screen;
    ScreenToClient(g.hwnd, &pt);
    const int steps = delta / WHEEL_DELTA;
    if (Inside(CategoryListRect(), pt)) {
        const int maxScroll = std::max(0, static_cast<int>(Categories().size()) - VisibleCategories());
        g.categoryScroll = std::clamp(g.categoryScroll - steps, 0, maxScroll);
    } else {
        ScrollSymbols(-steps * 3);
    }
    Invalidate();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;  // never take the focus from the program being typed in
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            Paint(dc, client);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            OnLeftDown({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_LBUTTONUP:
            OnLeftUp();
            return 0;
        case WM_CAPTURECHANGED:
            if (g.drawing || g.draggingThumb) OnLeftUp();
            return 0;
        case WM_RBUTTONDOWN:  // right click in the box: undo the last stroke
            if (g.page == Page::Handwriting && !g.strokes.empty()) {
                g.strokes.pop_back();
                Recognize();
                Invalidate();
            }
            return 0;
        case WM_MOUSELEAVE:
            g.hoverResult = g.hoverSymbol = g.hoverCategory = -1;
            Invalidate();
            return 0;
        case WM_MOUSEWHEEL:
            OnWheel({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, GET_WHEEL_DELTA_WPARAM(wp));
            return 0;
        case WM_DPICHANGED: {
            g.dpi = HIWORD(wp);
            const RECT* rc = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Invalidate();
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, n);
    return dir.substr(0, dir.find_last_of(L'\\'));
}

std::wstring FindDataDirectory(const std::wstring& exe) {
    for (const wchar_t* rel : {L"\\data", L"\\..\\data", L"\\..\\..\\data", L"\\..\\..\\..\\data"}) {
        const std::wstring dir = exe + rel;
        if (zy::FileExists(dir + L"\\" + zy::kManifestFile)) return dir;
    }
    return {};
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    // One pad: starting it again brings the existing one back.
    HANDLE instance = CreateMutexW(nullptr, FALSE, kInstanceMutex);
    if (instance && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kClassName, nullptr)) {
            ShowWindow(existing, SW_SHOWNOACTIVATE);
            SetWindowPos(existing, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    g.inst = inst;
    g.exeDir = ExeDirectory();
    g.hwrReady = g.hwr.Init();
    const std::wstring dataDir = FindDataDirectory(g.exeDir);
    if (!dataDir.empty()) {
        g.dict = std::make_shared<zy::Dictionary>(dataDir, L"");
        g.dict->Reload();
    }
    SelectCategory(0);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    // Bottom right of the monitor under the mouse, out of the way.
    POINT cursor;
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    UINT dpiX = 96, dpiY = 96;
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
        if (auto fn = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor")))
            fn(monitor, 0, &dpiX, &dpiY);
    }
    g.dpi = dpiX ? dpiX : 96;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const DWORD exStyle = WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_APPWINDOW;
    RECT rc = {0, 0, Px(kWidth), Px(kHeight)};
    AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, g.dpi);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    g.hwnd = CreateWindowExW(exStyle, kClassName, kTitle, style, mi.rcWork.right - w - Px(16),
                             mi.rcWork.bottom - h - Px(16), w, h, nullptr, nullptr, inst, nullptr);
    if (!g.hwnd) return 1;
    g.dpi = GetDpiForWindow(g.hwnd);
    ShowWindow(g.hwnd, SW_SHOWNOACTIVATE);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    if (instance) CloseHandle(instance);
    return 0;
}
