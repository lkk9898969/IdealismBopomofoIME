#pragma once
#include <windows.h>
#include <msctf.h>

namespace zy {

extern HINSTANCE g_hInst;
extern LONG g_dllRefCount;

inline void DllAddRef() { InterlockedIncrement(&g_dllRefCount); }
inline void DllRelease() { InterlockedDecrement(&g_dllRefCount); }

// {1C2FCFCE-B688-41D6-9CA8-FFC3F808F0B8}
inline constexpr CLSID CLSID_IdealismTextService = {
    0x1c2fcfce, 0xb688, 0x41d6, {0x9c, 0xa8, 0xff, 0xc3, 0xf8, 0x08, 0xf0, 0xb8}};

// {84EF98CE-13EF-4565-8250-EA8A42727F5D}
inline constexpr GUID GUID_IdealismProfile = {
    0x84ef98ce, 0x13ef, 0x4565, {0x82, 0x50, 0xea, 0x8a, 0x42, 0x72, 0x7f, 0x5d}};

inline constexpr LANGID kLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);  // 0x0404 zh-TW
inline constexpr wchar_t kDisplayName[] = L"微新注音";  // the name in the input switcher

}  // namespace zy
