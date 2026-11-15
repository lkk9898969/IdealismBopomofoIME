#pragma once
#include <windows.h>

#include <string>
#include <vector>

struct IInkRecognizer;

namespace zy {

// Handwriting recognition through the Tablet PC ink API (msinkaut), which uses the recognizers
// Windows has installed — here the Traditional Chinese one ("Microsoft 中文(繁體)手寫辨識器").
// COM must be initialized (apartment-threaded) on the calling thread.
class HandwritingRecognizer {
public:
    HandwritingRecognizer();
    ~HandwritingRecognizer();
    HandwritingRecognizer(const HandwritingRecognizer&) = delete;
    HandwritingRecognizer& operator=(const HandwritingRecognizer&) = delete;

    // Finds the recognizer for Traditional Chinese (LANGID 0x0404). False if none is installed.
    bool Init();
    bool Available() const { return recognizer_ != nullptr; }
    std::wstring Name() const { return name_; }

    // strokes: each a list of points in pixels, inside a writing box of the given size (origin at
    // its top-left). One Chinese character is expected: the alternates are Chinese characters,
    // best first (anything else the recognizer suggests comes only if there is nothing better).
    std::vector<std::wstring> Recognize(const std::vector<std::vector<POINT>>& strokes, SIZE box, size_t max) const;

private:
    IInkRecognizer* recognizer_ = nullptr;
    std::wstring name_;
};

}  // namespace zy
