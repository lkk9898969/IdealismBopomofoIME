// Checks the handwriting recognizer with synthetic strokes (skipped if none is installed).
#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <string>
#include <vector>

#include "pad/Handwriting.h"

static int g_failures = 0;

static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static std::vector<POINT> Line(int x0, int y0, int x1, int y1) {
    std::vector<POINT> pts;
    for (int i = 0; i <= 20; ++i) pts.push_back({x0 + (x1 - x0) * i / 20, y0 + (y1 - y0) * i / 20});
    return pts;
}

static bool Contains(const std::vector<std::wstring>& list, const wchar_t* s) {
    for (const auto& x : list)
        if (x == s) return true;
    return false;
}

static void Print(const char* what, const std::vector<std::wstring>& list) {
    std::wstring all;
    for (const auto& s : list) all += s + L" ";
    char buf[512] = {};
    WideCharToMultiByte(CP_UTF8, 0, all.c_str(), -1, buf, sizeof(buf) - 1, nullptr, nullptr);
    std::printf("%s: %s\n", what, buf);
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    {
        zy::HandwritingRecognizer hwr;
        if (!hwr.Init()) {
            std::printf("SKIP: no Traditional Chinese handwriting recognizer installed\n");
            CoUninitialize();
            return 0;
        }
        const auto one = hwr.Recognize({Line(20, 100, 180, 100)}, {200, 200}, 10);
        Print("一", one);
        Check(Contains(one, L"一"), "a horizontal line is 一");
        Check(!one.empty() && one[0] == L"一", "and 一 comes first");
        const auto ten = hwr.Recognize({Line(20, 100, 180, 100), Line(100, 20, 100, 180)}, {200, 200}, 10);
        Print("十", ten);
        Check(Contains(ten, L"十"), "a cross is 十");
        const auto mouth = hwr.Recognize({Line(40, 40, 40, 160), {{40, 40}, {160, 40}, {160, 160}}, Line(40, 160, 160, 160)}, {200, 200}, 10);
        Print("口", mouth);
        Check(Contains(mouth, L"口"), "a box is 口");
        Check(hwr.Recognize({}, {200, 200}, 10).empty(), "no strokes, no result");
    }
    CoUninitialize();
    std::printf(g_failures ? "%d failures\n" : "pad test passed\n", g_failures);
    return g_failures ? 1 : 0;
}
