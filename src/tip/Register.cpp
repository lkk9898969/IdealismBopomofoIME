// COM + TSF registration, called by regsvr32 (needs administrator rights).
#include <wrl/client.h>

#include <string>

#include "tip/Globals.h"

using Microsoft::WRL::ComPtr;

namespace zy {
namespace {

// Categories this TIP declares. Add GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER once display
// attributes are implemented.
const GUID* const kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    &GUID_TFCAT_TIPCAP_COMLESS,
    &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,  // candidate list as a UI element (UILess, CandidateUI.cpp)
};

std::wstring ClsidKeyPath() {
    wchar_t clsid[64] = {};
    StringFromGUID2(CLSID_IdealismTextService, clsid, ARRAYSIZE(clsid));
    return std::wstring(L"CLSID\\") + clsid;
}

LSTATUS SetStringValue(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

HRESULT RegisterComServer(const std::wstring& dllPath) {
    HKEY clsidKey = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CLASSES_ROOT, ClsidKeyPath().c_str(), 0, nullptr, 0, KEY_WRITE,
                                 nullptr, &clsidKey, nullptr);
    if (st != ERROR_SUCCESS) return HRESULT_FROM_WIN32(st);
    SetStringValue(clsidKey, nullptr, kDisplayName);

    HKEY inproc = nullptr;
    st = RegCreateKeyExW(clsidKey, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &inproc, nullptr);
    if (st == ERROR_SUCCESS) {
        SetStringValue(inproc, nullptr, dllPath);
        SetStringValue(inproc, L"ThreadingModel", L"Apartment");
        RegCloseKey(inproc);
    }
    RegCloseKey(clsidKey);
    return HRESULT_FROM_WIN32(st);
}

}  // namespace

HRESULT RegisterServer() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(g_hInst, path, ARRAYSIZE(path));
    if (len == 0 || len >= ARRAYSIZE(path)) return E_FAIL;
    std::wstring dllPath(path, len);

    HRESULT hr = RegisterComServer(dllPath);
    if (FAILED(hr)) return hr;

    ComPtr<ITfInputProcessorProfileMgr> profiles;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&profiles));
    if (FAILED(hr)) return hr;

    hr = profiles->RegisterProfile(CLSID_IdealismTextService, kLangId, GUID_IdealismProfile, kDisplayName,
                                   static_cast<ULONG>(wcslen(kDisplayName)), dllPath.c_str(),
                                   static_cast<ULONG>(dllPath.size()),
                                   0,        // icon index (IDI_ZHUYIN is the first icon resource)
                                   nullptr,  // no substitute HKL
                                   0, TRUE, 0);
    if (FAILED(hr)) return hr;

    ComPtr<ITfCategoryMgr> categories;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories));
    if (FAILED(hr)) return hr;
    for (const GUID* cat : kCategories) {
        hr = categories->RegisterCategory(CLSID_IdealismTextService, *cat, CLSID_IdealismTextService);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

HRESULT UnregisterServer() {
    ComPtr<ITfCategoryMgr> categories;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&categories)))) {
        for (const GUID* cat : kCategories)
            categories->UnregisterCategory(CLSID_IdealismTextService, *cat, CLSID_IdealismTextService);
    }

    ComPtr<ITfInputProcessorProfileMgr> profiles;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&profiles)))) {
        profiles->UnregisterProfile(CLSID_IdealismTextService, kLangId, GUID_IdealismProfile, 0);
    }

    RegDeleteTreeW(HKEY_CLASSES_ROOT, ClsidKeyPath().c_str());
    return S_OK;
}

}  // namespace zy
