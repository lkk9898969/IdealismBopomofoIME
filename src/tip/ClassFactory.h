#pragma once
#include <unknwn.h>

namespace zy {

class ClassFactory final : public IClassFactory {
public:
    ClassFactory();
    ~ClassFactory();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL lock) override;

private:
    LONG ref_ = 1;
};

}  // namespace zy
