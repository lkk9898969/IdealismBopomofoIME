// Keyboard open/close and Chinese/English conversion mode, shared with the system through
// TSF compartments (this is what the taskbar 中/英 indicator reads and writes).
#include "tip/LangBarButton.h"
#include "tip/TextService.h"

using Microsoft::WRL::ComPtr;

namespace zy {
namespace {

ComPtr<ITfCompartment> GetCompartment(ITfThreadMgr* threadMgr, REFGUID guid) {
    ComPtr<ITfCompartmentMgr> mgr;
    ComPtr<ITfCompartment> compartment;
    if (threadMgr && SUCCEEDED(threadMgr->QueryInterface(IID_PPV_ARGS(&mgr))))
        mgr->GetCompartment(guid, &compartment);
    return compartment;
}

bool ReadI4(ITfThreadMgr* threadMgr, REFGUID guid, LONG* value) {
    auto c = GetCompartment(threadMgr, guid);
    if (!c) return false;
    VARIANT v;
    VariantInit(&v);
    bool ok = SUCCEEDED(c->GetValue(&v)) && v.vt == VT_I4;
    if (ok) *value = v.lVal;
    VariantClear(&v);
    return ok;
}

void WriteI4(ITfThreadMgr* threadMgr, TfClientId clientId, REFGUID guid, LONG value) {
    auto c = GetCompartment(threadMgr, guid);
    if (!c) return;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = value;
    c->SetValue(clientId, &v);
}

DWORD Advise(ITfThreadMgr* threadMgr, REFGUID guid, ITfCompartmentEventSink* sink) {
    DWORD cookie = TF_INVALID_COOKIE;
    auto c = GetCompartment(threadMgr, guid);
    ComPtr<ITfSource> source;
    if (c && SUCCEEDED(c.As(&source))) source->AdviseSink(IID_ITfCompartmentEventSink, sink, &cookie);
    return cookie;
}

void Unadvise(ITfThreadMgr* threadMgr, REFGUID guid, DWORD* cookie) {
    if (*cookie == TF_INVALID_COOKIE) return;
    auto c = GetCompartment(threadMgr, guid);
    ComPtr<ITfSource> source;
    if (c && SUCCEEDED(c.As(&source))) source->UnadviseSink(*cookie);
    *cookie = TF_INVALID_COOKIE;
}

}  // namespace

HRESULT TextService::InitCompartments() {
    keyboardOpen_ = true;
    chineseMode_ = true;  // Microsoft Bopomofo starts in Chinese mode
    fullWidth_ = false;
    if (composer_) composer_->SetFullWidth(false);
    updatingCompartment_ = true;
    WriteI4(threadMgr_.Get(), clientId_, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, TRUE);
    updatingCompartment_ = false;
    SyncModeToCompartment();

    auto* sink = static_cast<ITfCompartmentEventSink*>(this);
    openCloseSinkCookie_ = Advise(threadMgr_.Get(), GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, sink);
    conversionSinkCookie_ = Advise(threadMgr_.Get(), GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, sink);
    return S_OK;
}

void TextService::OpenKeyboard() {
    keyboardOpen_ = true;
    chineseMode_ = true;
    updatingCompartment_ = true;
    WriteI4(threadMgr_.Get(), clientId_, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, TRUE);
    updatingCompartment_ = false;
    SyncModeToCompartment();
}

void TextService::UninitCompartments() {
    Unadvise(threadMgr_.Get(), GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &openCloseSinkCookie_);
    Unadvise(threadMgr_.Get(), GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &conversionSinkCookie_);
}

void TextService::SyncModeToCompartment() {
    updatingCompartment_ = true;
    WriteI4(threadMgr_.Get(), clientId_, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
            (chineseMode_ ? TF_CONVERSIONMODE_NATIVE : TF_CONVERSIONMODE_ALPHANUMERIC) |
                (fullWidth_ ? TF_CONVERSIONMODE_FULLSHAPE : 0));
    updatingCompartment_ = false;
    if (langBarButton_) langBarButton_->Update();
}

void TextService::ReadModeFromCompartment() {
    LONG value = 0;
    if (ReadI4(threadMgr_.Get(), GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &value)) keyboardOpen_ = value != 0;
    if (ReadI4(threadMgr_.Get(), GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &value))
    {
        chineseMode_ = (value & TF_CONVERSIONMODE_NATIVE) != 0;
        fullWidth_ = (value & TF_CONVERSIONMODE_FULLSHAPE) != 0;
        if (composer_) composer_->SetFullWidth(fullWidth_);
    }
}

STDMETHODIMP TextService::OnChange(REFGUID) {
    if (updatingCompartment_) return S_OK;  // our own write
    try {
        const bool wasChinese = IsChineseMode();
        ReadModeFromCompartment();
        if (wasChinese && !IsChineseMode() && composer_ && !composer_->Empty()) CommitAll(compositionContext_.Get());
        if (langBarButton_) langBarButton_->Update();
    } catch (...) {
    }
    return S_OK;
}

}  // namespace zy
