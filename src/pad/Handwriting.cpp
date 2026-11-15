#include "pad/Handwriting.h"

#include <ole2.h>  // WIN32_LEAN_AND_MEAN leaves out what msinkaut.h needs

#include <msinkaut.h>
#include <wrl/client.h>

#include "msinkaut_i.c"  // CLSIDs and IIDs of the ink automation API

using Microsoft::WRL::ComPtr;

namespace zy {
namespace {

constexpr LONG kTraditionalChinese = 0x0404;
constexpr LONG kInkUnitsPerPixel = 26;  // ink space is HIMETRIC (0.01 mm); 96 dpi ≈ 26.46 per pixel

bool IsHan(const std::wstring& s) {
    if (s.empty()) return false;
    const wchar_t c = s[0];
    if (c >= 0xD800 && c <= 0xDBFF) return true;  // CJK extensions beyond the BMP
    return (c >= 0x3400 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || c == 0x3007;
}

struct Bstr {
    BSTR s = nullptr;
    ~Bstr() { SysFreeString(s); }
};

}  // namespace

HandwritingRecognizer::HandwritingRecognizer() = default;

HandwritingRecognizer::~HandwritingRecognizer() {
    if (recognizer_) recognizer_->Release();
}

bool HandwritingRecognizer::Init() {
    ComPtr<IInkRecognizers> recognizers;
    if (FAILED(CoCreateInstance(CLSID_InkRecognizers, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&recognizers))))
        return false;
    ComPtr<IInkRecognizer> recognizer;
    if (FAILED(recognizers->GetDefaultRecognizer(kTraditionalChinese, &recognizer)) || !recognizer) return false;
    Bstr name;
    if (SUCCEEDED(recognizer->get_Name(&name.s)) && name.s) name_ = name.s;
    recognizer_ = recognizer.Detach();
    return true;
}

std::vector<std::wstring> HandwritingRecognizer::Recognize(const std::vector<std::vector<POINT>>& strokes, SIZE box,
                                                           size_t max) const {
    std::vector<std::wstring> out;
    if (!recognizer_ || strokes.empty()) return out;

    ComPtr<IInkDisp> ink;
    if (FAILED(CoCreateInstance(CLSID_InkDisp, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ink)))) return out;
    for (const auto& stroke : strokes) {
        if (stroke.empty()) continue;
        // Packet data: x, y, x, y, ... (a single point is drawn as a tiny dot).
        std::vector<POINT> points = stroke;
        if (points.size() == 1) points.push_back({points[0].x + 1, points[0].y + 1});
        SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(points.size() * 2));
        if (!array) return out;
        LONG* data = nullptr;
        if (SUCCEEDED(SafeArrayAccessData(array, reinterpret_cast<void**>(&data)))) {
            for (size_t i = 0; i < points.size(); ++i) {
                data[i * 2] = points[i].x * kInkUnitsPerPixel;
                data[i * 2 + 1] = points[i].y * kInkUnitsPerPixel;
            }
            SafeArrayUnaccessData(array);
        }
        VARIANT packets;
        VariantInit(&packets);
        packets.vt = VT_ARRAY | VT_I4;
        packets.parray = array;
        VARIANT description;
        VariantInit(&description);
        ComPtr<IInkStrokeDisp> created;
        ink->CreateStroke(packets, description, &created);
        VariantClear(&packets);
    }

    ComPtr<IInkRecognizerContext> context;
    ComPtr<IInkStrokes> inkStrokes;
    if (FAILED(recognizer_->CreateRecognizerContext(&context)) || FAILED(ink->get_Strokes(&inkStrokes))) return out;
    BSTR factoid = SysAllocString(FACTOID_ONECHAR);
    context->put_Factoid(factoid);  // one character at a time; ignored if unsupported
    SysFreeString(factoid);
    // A single writing box: the recognizer then knows this is one boxed character, which matters a
    // lot for Chinese.
    ComPtr<IInkRecognizerGuide> guide;
    ComPtr<IInkRectangle> rect;
    if (SUCCEEDED(CoCreateInstance(CLSID_InkRecognizerGuide, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&guide))) &&
        SUCCEEDED(CoCreateInstance(CLSID_InkRectangle, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rect)))) {
        rect->SetRectangle(0, 0, box.cy * kInkUnitsPerPixel, box.cx * kInkUnitsPerPixel);
        guide->put_WritingBox(rect.Get());
        guide->put_DrawnBox(rect.Get());
        guide->put_Rows(1);
        guide->put_Columns(1);
        context->putref_Guide(guide.Get());
    }
    if (FAILED(context->putref_Strokes(inkStrokes.Get()))) return out;

    InkRecognitionStatus status = IRS_NoError;
    ComPtr<IInkRecognitionResult> result;
    if (FAILED(context->Recognize(&status, &result)) || !result) return out;
    // Ask for plenty: letters and punctuation that look alike are filtered out below.
    std::vector<std::wstring> all;
    ComPtr<IInkRecognitionAlternates> alternates;
    if (SUCCEEDED(result->AlternatesFromSelection(0, -1, 64, &alternates)) && alternates) {
        long count = 0;
        alternates->get_Count(&count);
        for (long i = 0; i < count; ++i) {
            ComPtr<IInkRecognitionAlternate> alternate;
            Bstr text;
            if (SUCCEEDED(alternates->Item(i, &alternate)) && alternate && SUCCEEDED(alternate->get_String(&text.s)) &&
                text.s && *text.s)
                all.emplace_back(text.s);
        }
    }
    if (all.empty()) {
        Bstr top;
        if (SUCCEEDED(result->get_TopString(&top.s)) && top.s && *top.s) all.emplace_back(top.s);
    }
    for (const auto& s : all)
        if (IsHan(s) && out.size() < max) out.push_back(s);
    for (const auto& s : all)  // then the rest, only to fill up
        if (!IsHan(s) && out.size() < max && out.size() < 3) out.push_back(s);
    return out;
}

}  // namespace zy
