#include "tip/ClassFactory.h"

#include <new>

#include "tip/Globals.h"
#include "tip/TextService.h"

namespace zy {

ClassFactory::ClassFactory() { DllAddRef(); }
ClassFactory::~ClassFactory() { DllRelease(); }

STDMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ClassFactory::AddRef() { return InterlockedIncrement(&ref_); }

STDMETHODIMP_(ULONG) ClassFactory::Release() {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP ClassFactory::CreateInstance(IUnknown* outer, REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    auto* service = new (std::nothrow) TextService();
    if (!service) return E_OUTOFMEMORY;
    HRESULT hr = service->QueryInterface(riid, ppv);
    service->Release();
    return hr;
}

STDMETHODIMP ClassFactory::LockServer(BOOL lock) {
    if (lock) DllAddRef();
    else DllRelease();
    return S_OK;
}

}  // namespace zy
