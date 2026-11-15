#include <new>

#include "tip/ClassFactory.h"
#include "tip/Globals.h"

namespace zy {
HRESULT RegisterServer();
HRESULT UnregisterServer();
}  // namespace zy

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        zy::g_hInst = hInst;
        DisableThreadLibraryCalls(hInst);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, zy::CLSID_IdealismTextService)) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) zy::ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() { return zy::g_dllRefCount <= 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
    HRESULT hr = zy::RegisterServer();
    if (FAILED(hr)) zy::UnregisterServer();
    return hr;
}

STDAPI DllUnregisterServer() { return zy::UnregisterServer(); }
