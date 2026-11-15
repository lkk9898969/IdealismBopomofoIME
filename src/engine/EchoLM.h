#pragma once
#include "gramambular2/language_model.h"

namespace zy {

// Fallback language model used when the dictionary file can't be loaded: every syllable maps to
// itself, so the IME still works (it shows Bopomofo, like M1) instead of swallowing keys.
class EchoLM final : public Formosa::Gramambular2::LanguageModel {
public:
    std::vector<Unigram> getUnigrams(const std::string& reading) override {
        if (!hasUnigrams(reading)) return {};
        return {Unigram(reading, 0)};
    }
    // Single syllables only: multi-syllable keys ("ㄓㄨㄥ-ㄏㄠˇ") must not form phrase nodes.
    bool hasUnigrams(const std::string& reading) override {
        return !reading.empty() && reading[0] != '_' && reading.find('-') == std::string::npos;
    }
};

}  // namespace zy
