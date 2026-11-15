#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace zy {

struct CandidatePage {
    // One column in list view; several in table view (each column is one page of 1..9).
    std::vector<std::vector<std::wstring>> columns;
    size_t highlightedColumn = 0;
    size_t highlightedRow = 0;
    size_t page = 0;  // which screenful this is, for the "page / pageCount" footer
    size_t pageCount = 0;

    // Symbol keyboard (instead of columns): rows of keys, each with its key label and symbol.
    struct Key {
        wchar_t label = 0;
        std::wstring symbol;
    };
    std::vector<std::vector<Key>> keyboard;
    std::wstring title;

    bool Empty() const { return columns.empty() && keyboard.empty(); }
};

// Candidate list (one column) or table (several columns), created in-process on the host's UI thread.
//
// Painted with UpdateLayeredWindow straight from Show(): no WM_PAINT, no dependency on the host's
// message loop dispatching our messages. Never takes focus (WS_EX_NOACTIVATE + MA_NOACTIVATE).
class CandidateWindow {
public:
    CandidateWindow() = default;
    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;
    ~CandidateWindow() { Destroy(); }

    void Show(const CandidatePage& page, const RECT& anchor);  // anchor: text rect in screen coords
    void Hide();
    void Destroy();

    // Development aid: paints a page to a .bmp file (used by tests/tip_smoke).
    static bool SavePreview(const CandidatePage& page, UINT dpi, const wchar_t* path);

private:
    struct Bitmap;
    static bool Paint(const CandidatePage& page, UINT dpi, Bitmap* out);
    static bool PaintKeyboard(const CandidatePage& page, UINT dpi, Bitmap* out);
    static bool CreateCanvas(int width, int height, Bitmap* out);
    bool EnsureWindow();

    HWND hwnd_ = nullptr;
    bool visible_ = false;
};

}  // namespace zy
