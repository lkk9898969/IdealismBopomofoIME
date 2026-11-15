#pragma once
#include <wrl/client.h>

#include "tip/Globals.h"

namespace zy {

class TextService;

// The 中/英 indicator in the taskbar notification area (GUID_LBI_INPUTMODE). Click toggles the mode.
class LangBarButton final : public ITfLangBarItemButton, public ITfSource {
public:
    explicit LangBarButton(TextService* owner);

    // Called by the owner before it goes away; the button may outlive it briefly (TSF holds refs).
    void Detach() { owner_ = nullptr; }
    // Notifies TSF that icon/text changed.
    void Update();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
    STDMETHODIMP GetStatus(DWORD* status) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP GetTooltipString(BSTR* tooltip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* area) override;
    STDMETHODIMP InitMenu(ITfMenu* menu) override;
    STDMETHODIMP OnMenuSelect(UINT id) override;
    STDMETHODIMP GetIcon(HICON* icon) override;
    STDMETHODIMP GetText(BSTR* text) override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* sink, DWORD* cookie) override;
    STDMETHODIMP UnadviseSink(DWORD cookie) override;

private:
    ~LangBarButton();
    bool Chinese() const;
    bool FullWidth() const;

    LONG ref_ = 1;
    TextService* owner_;
    Microsoft::WRL::ComPtr<ITfLangBarItemSink> sink_;
};

}  // namespace zy
