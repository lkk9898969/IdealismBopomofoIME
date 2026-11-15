#include "tip/CandidateWindow.h"

#include <algorithm>

#include "tip/Globals.h"

namespace zy {
namespace {

constexpr wchar_t kClassName[] = L"IdealismIME_CandidateWindow";

constexpr COLORREF kBackground = RGB(255, 255, 255);
constexpr COLORREF kBorder = RGB(160, 160, 160);
constexpr COLORREF kHighlight = RGB(204, 232, 255);
constexpr COLORREF kText = RGB(0, 0, 0);
constexpr COLORREF kLabel = RGB(110, 110, 110);

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, IDC_ARROW)); return TRUE;
        default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

bool RegisterWindowClass() {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = kClassName;
    if (RegisterClassExW(&wc)) return true;
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    // A class left behind by a previous load of this DLL would point at a stale WndProc.
    // Re-register it; if windows of it still exist elsewhere, the existing one is ours anyway.
    if (UnregisterClassW(kClassName, g_hInst)) return RegisterClassExW(&wc) != 0;
    return true;
}

UINT DpiFor(HWND hwnd) {
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
    return dpi ? dpi : 96;
}

HFONT MakeFont(int pixelHeight) {
    return CreateFontW(-pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");
}

void Fill(HDC dc, const RECT& rc, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

}  // namespace

// A page painted into a memory DC holding a top-down 32-bit DIB.
struct CandidateWindow::Bitmap {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    void* bits = nullptr;
    SIZE size = {};

    ~Bitmap() {
        if (dc) {
            SelectObject(dc, oldBitmap);
            DeleteDC(dc);
        }
        if (bitmap) DeleteObject(bitmap);
    }
};

bool CandidateWindow::CreateCanvas(int width, int height, Bitmap* out) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    out->bitmap = CreateDIBSection(out->dc, &bmi, DIB_RGB_COLORS, &out->bits, nullptr, 0);
    if (!out->bitmap) return false;
    out->oldBitmap = SelectObject(out->dc, out->bitmap);
    out->size = {width, height};
    RECT all = {0, 0, width, height};
    Fill(out->dc, all, kBackground);
    HBRUSH border = CreateSolidBrush(kBorder);
    FrameRect(out->dc, &all, border);
    DeleteObject(border);
    SetBkMode(out->dc, TRANSPARENT);
    return true;
}

// The symbol keyboard: the keys laid out like a keyboard, each showing its symbol, with the key
// itself in small print in the corner.
bool CandidateWindow::PaintKeyboard(const CandidatePage& page, UINT dpi, Bitmap* out) {
    auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
    HDC screen = GetDC(nullptr);
    out->dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!out->dc) return false;

    HFONT font = MakeFont(px(18));
    HFONT smallFont = MakeFont(px(11));
    HGDIOBJ oldFont = SelectObject(out->dc, smallFont);

    const int pad = px(6);
    const int key = px(34);      // key pitch
    const int gap = px(3);
    const int titleHeight = px(20);
    const int indent[] = {0, key / 2, key * 3 / 4, key * 5 / 4};  // rows stagger like a keyboard
    size_t widest = 0;
    for (size_t r = 0; r < page.keyboard.size(); ++r)
        widest = std::max<size_t>(widest, indent[std::min<size_t>(r, 3)] + page.keyboard[r].size() * key);
    const int width = pad * 2 + static_cast<int>(widest);
    const int height = pad + titleHeight + static_cast<int>(page.keyboard.size()) * key + pad;
    bool ok = CreateCanvas(width, height, out);
    if (ok) {
        HDC dc = out->dc;
        RECT tr = {pad, pad, width - pad, pad + titleHeight};
        SetTextColor(dc, kLabel);
        wchar_t pageText[32];
        wsprintfW(pageText, L"  %u / %u", static_cast<unsigned>(page.page + 1), static_cast<unsigned>(page.pageCount));
        const std::wstring title = L"符號鍵盤：" + page.title + pageText + L"　PgUp／PgDn 換頁，Esc 關閉";
        DrawTextW(dc, title.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        HBRUSH keyBorder = CreateSolidBrush(RGB(200, 200, 200));
        for (size_t r = 0; r < page.keyboard.size(); ++r) {
            const int top = pad + titleHeight + static_cast<int>(r) * key;
            for (size_t i = 0; i < page.keyboard[r].size(); ++i) {
                const auto& k = page.keyboard[r][i];
                const int left = pad + indent[std::min<size_t>(r, 3)] + static_cast<int>(i) * key;
                RECT cell = {left, top, left + key - gap, top + key - gap};
                if (!k.symbol.empty()) Fill(dc, cell, RGB(246, 248, 252));
                FrameRect(dc, &cell, keyBorder);
                SelectObject(dc, smallFont);
                SetTextColor(dc, kLabel);
                wchar_t label[2] = {k.label, 0};
                TextOutW(dc, cell.left + px(2), cell.top + px(1), label, 1);
                if (!k.symbol.empty()) {
                    SelectObject(dc, font);
                    SetTextColor(dc, kText);
                    RECT sr = {cell.left, cell.top + px(6), cell.right, cell.bottom};
                    DrawTextW(dc, k.symbol.c_str(), static_cast<int>(k.symbol.size()), &sr,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
        }
        DeleteObject(keyBorder);
    }
    SelectObject(out->dc, oldFont);
    DeleteObject(font);
    DeleteObject(smallFont);
    return ok;
}

bool CandidateWindow::Paint(const CandidatePage& page, UINT dpi, Bitmap* out) {
    if (!page.keyboard.empty()) return PaintKeyboard(page, dpi, out);
    auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };

    HDC screen = GetDC(nullptr);
    out->dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!out->dc) return false;

    HFONT font = MakeFont(px(18));
    HFONT smallFont = MakeFont(px(12));
    HGDIOBJ oldFont = SelectObject(out->dc, font);

    // Measure.
    const int pad = px(6);
    SIZE labelSize = {};
    GetTextExtentPoint32W(out->dc, L"9", 1, &labelSize);
    TEXTMETRICW tm = {};
    GetTextMetricsW(out->dc, &tm);
    const int rowHeight = tm.tmHeight + px(6);
    const int labelWidth = labelSize.cx + px(10);
    // Each column is as wide as its widest candidate; columnLeft[i] is where column i starts.
    std::vector<int> columnLeft;
    size_t rows = 0;
    int x = pad;
    for (const auto& column : page.columns) {
        int textWidth = px(24);
        for (const auto& item : column) {
            SIZE sz = {};
            GetTextExtentPoint32W(out->dc, item.c_str(), static_cast<int>(item.size()), &sz);
            textWidth = std::max<int>(textWidth, sz.cx);
        }
        columnLeft.push_back(x);
        x += labelWidth + textWidth + pad * 2;
        rows = std::max(rows, column.size());
    }
    columnLeft.push_back(x);
    const bool showFooter = page.pageCount > 1;
    const int footerHeight = showFooter ? px(18) : 0;
    const int width = x;
    const int height = pad + static_cast<int>(rows) * rowHeight + footerHeight + pad / 2;

    if (!CreateCanvas(width, height, out)) {
        SelectObject(out->dc, oldFont);
        DeleteObject(font);
        DeleteObject(smallFont);
        return false;
    }

    // Paint.
    HDC dc = out->dc;
    for (size_t c = 0; c < page.columns.size(); ++c) {
        const int left = columnLeft[c];
        const int right = columnLeft[c + 1];
        // The number keys pick from the highlighted column, so only that one shows its numbers.
        const bool numbered = c == page.highlightedColumn;
        for (size_t i = 0; i < page.columns[c].size(); ++i) {
            const int top = pad + static_cast<int>(i) * rowHeight;
            if (numbered && i == page.highlightedRow)
                Fill(dc, {left - pad + px(2), top, right - pad - px(2), top + rowHeight}, kHighlight);
            if (numbered) {
                wchar_t label[2] = {static_cast<wchar_t>(L'1' + i), 0};
                SetTextColor(dc, kLabel);
                TextOutW(dc, left, top + px(3), label, 1);
            }
            SetTextColor(dc, kText);
            const auto& item = page.columns[c][i];
            TextOutW(dc, left + labelWidth, top + px(3), item.c_str(), static_cast<int>(item.size()));
        }
    }
    if (showFooter) {
        SelectObject(dc, smallFont);
        wchar_t footer[32];
        wsprintfW(footer, L"%u / %u", static_cast<unsigned>(page.page + 1), static_cast<unsigned>(page.pageCount));
        RECT fr = {pad, height - footerHeight - pad / 2, width - pad, height - pad / 2};
        SetTextColor(dc, kLabel);
        DrawTextW(dc, footer, -1, &fr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
    DeleteObject(smallFont);
    return true;
}

bool CandidateWindow::EnsureWindow() {
    if (hwnd_ && IsWindow(hwnd_)) return true;
    hwnd_ = nullptr;
    if (!RegisterWindowClass()) return false;
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"",
                            WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_hInst, nullptr);
    visible_ = false;
    return hwnd_ != nullptr;
}

void CandidateWindow::Show(const CandidatePage& page, const RECT& anchor) {
    if (page.Empty()) {
        Hide();
        return;
    }
    if (!EnsureWindow()) return;

    const UINT dpi = DpiFor(hwnd_);
    Bitmap bmp;
    if (!Paint(page, dpi, &bmp)) return;
    const int width = bmp.size.cx;
    const int height = bmp.size.cy;
    const int gap = MulDiv(2, static_cast<int>(dpi), 96);

    // Position below the text; flip above if it would leave the monitor's work area.
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi);
    POINT pos = {anchor.left, anchor.bottom + gap};
    if (pos.y + height > mi.rcWork.bottom) pos.y = anchor.top - height - gap;
    if (pos.x + width > mi.rcWork.right) pos.x = mi.rcWork.right - width;
    if (pos.x < mi.rcWork.left) pos.x = mi.rcWork.left;
    if (pos.y < mi.rcWork.top) pos.y = mi.rcWork.top;

    POINT src = {0, 0};
    UpdateLayeredWindow(hwnd_, nullptr, &pos, &bmp.size, bmp.dc, &src, 0, nullptr, ULW_OPAQUE);
    if (!visible_) {
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        visible_ = true;
    }
}

void CandidateWindow::Hide() {
    if (hwnd_ && visible_) ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

void CandidateWindow::Destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    visible_ = false;
    UnregisterClassW(kClassName, g_hInst);  // fails harmlessly if another thread still has one
}

bool CandidateWindow::SavePreview(const CandidatePage& page, UINT dpi, const wchar_t* path) {
    Bitmap bmp;
    if (!Paint(page, dpi, &bmp)) return false;
    const DWORD pixelBytes = static_cast<DWORD>(bmp.size.cx * bmp.size.cy * 4);
    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    ih.biSize = sizeof(ih);
    ih.biWidth = bmp.size.cx;
    ih.biHeight = -bmp.size.cy;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + pixelBytes;
    GdiFlush();
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, &fh, sizeof(fh), &written, nullptr) && WriteFile(f, &ih, sizeof(ih), &written, nullptr) &&
              WriteFile(f, bmp.bits, pixelBytes, &written, nullptr);
    CloseHandle(f);
    return ok;
}

}  // namespace zy
