#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "engine/Composer.h"
#include "engine/Dictionary.h"
#include "engine/Learner.h"

namespace zy {

// Process-wide engine resources shared by every TextService instance (one per UI thread).
// The dictionary is read-only after loading, so lookups from several threads are safe; what the
// user teaches us lives in the Learner, which does its own locking.
class EngineHost final {
public:
    static EngineHost& Instance();

    // Loads the dictionary once (memory-mapped, cheap) and what was learned so far.
    // dataDir: folder with packs.txt and the phrase packs; userDir: %APPDATA%\IdealismIME with
    // settings.ini, user-phrases.txt, excluded-phrases.txt and learning.txt (optional).
    // Falls back to EchoLM on failure.
    void EnsureLoaded(const std::wstring& dataDir, const std::wstring& userDir);
    // The dictionary with learning applied and excluded phrases removed.
    std::shared_ptr<Formosa::Gramambular2::LanguageModel> LanguageModel();
    UserModel* Learning();
    bool DictionaryLoaded();

    // Picks up what other programs learned meanwhile, and dictionary changes made in the settings app. Returns at once: the work runs on the
    // thread pool, at most once every couple of seconds.
    void RefreshAsync();
    // Thread pool entry point: reloads changed dictionary files, reads and writes the learning
    // journal. Blocking; not for UI threads.
    void RunSync();

private:
    EngineHost() = default;
    Learner& EnsureLearner();  // call with mutex_ held
    void ScheduleSync();

    std::mutex mutex_;
    bool attempted_ = false;
    bool dictionaryLoaded_ = false;
    std::shared_ptr<Dictionary> dictionary_;                    // null when it could not be loaded
    std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm_;  // dictionary_ or EchoLM
    std::unique_ptr<Learner> learner_;
    std::shared_ptr<Formosa::Gramambular2::LanguageModel> final_;  // learner's LM minus exclusions

    std::atomic<bool> syncScheduled_{false};
    std::atomic<unsigned long long> lastRefreshTick_{0};
};

}  // namespace zy
