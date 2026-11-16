#include "engine/EngineHost.h"

#include <windows.h>

#include <algorithm>

#include "engine/EchoLM.h"
#include "engine/Utf.h"

namespace zy {
namespace {

constexpr unsigned long long kRefreshIntervalMs = 2000;

// The outermost layer: excluded phrases stay out even when learning would add them back.
class ExcludingLM : public Formosa::Gramambular2::LanguageModel {
public:
    ExcludingLM(std::shared_ptr<LanguageModel> inner, std::shared_ptr<Dictionary> dictionary)
        : inner_(std::move(inner)), dictionary_(std::move(dictionary)) {}
    std::vector<Unigram> getUnigrams(const std::string& reading) override {
        auto unigrams = inner_->getUnigrams(reading);
        unigrams.erase(std::remove_if(unigrams.begin(), unigrams.end(),
                                      [&](const Unigram& u) { return dictionary_->IsExcluded(reading, u.value()); }),
                       unigrams.end());
        return unigrams;
    }
    bool hasUnigrams(const std::string& reading) override {
        return inner_->hasUnigrams(reading) && !getUnigrams(reading).empty();
    }

private:
    std::shared_ptr<LanguageModel> inner_;
    std::shared_ptr<Dictionary> dictionary_;
};

VOID CALLBACK SyncCallback(PTP_CALLBACK_INSTANCE, PVOID) {
    // Runs on a thread pool thread inside the host process: nothing may escape.
    try {
        EngineHost::Instance().RunSync();
    } catch (...) {
    }
}

}  // namespace

EngineHost& EngineHost::Instance() {
    static EngineHost host;  // thread-safe static init (C++11)
    return host;
}

void EngineHost::EnsureLoaded(const std::wstring& dataDir, const std::wstring& userDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (attempted_) return;
    attempted_ = true;

    // This runs inside arbitrary host processes: an exception escaping here would crash the
    // app, so anything unexpected degrades to the echo model instead.
    try {
        auto dictionary = std::make_shared<Dictionary>(dataDir, userDir);
        dictionary->Reload();  // maps the packs (cheap) and reads the small user files
        enterCommitsBopomofo_ = dictionary->Options().enterCommitsBopomofo;
        if (dictionary->Loaded()) {
            dictionary_ = dictionary;
            lm_ = dictionary;
            dictionaryLoaded_ = true;
        }
    } catch (...) {
        dictionary_.reset();
        lm_.reset();
    }
    try {
        if (dictionaryLoaded_ && !userDir.empty()) {
            // What was learned so far, shared by every program through this file.
            learner_ = std::make_unique<Learner>(lm_, userDir + L"\\learning.txt");
            learner_->Load();
            learner_->SetScheduler([this] { ScheduleSync(); });
        }
    } catch (...) {
        learner_.reset();
    }
    EnsureLearner();
}

Learner& EngineHost::EnsureLearner() {
    if (!learner_) {
        // No dictionary or no user folder: still works, but nothing is remembered.
        if (!lm_) lm_ = std::make_shared<EchoLM>();
        learner_ = std::make_unique<Learner>(lm_, std::wstring());
    }
    if (!final_)
        final_ = dictionary_ ? std::make_shared<ExcludingLM>(learner_->LanguageModel(), dictionary_)
                             : learner_->LanguageModel();
    return *learner_;
}

std::shared_ptr<Formosa::Gramambular2::LanguageModel> EngineHost::LanguageModel() {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureLearner();
    return final_;
}

UserModel* EngineHost::Learning() {
    std::lock_guard<std::mutex> lock(mutex_);
    return &EnsureLearner();
}

bool EngineHost::DictionaryLoaded() {
    std::lock_guard<std::mutex> lock(mutex_);
    return dictionaryLoaded_;
}

void EngineHost::RefreshAsync() {
    const unsigned long long now = GetTickCount64();
    if (now - lastRefreshTick_.load() < kRefreshIntervalMs) return;
    lastRefreshTick_ = now;
    ScheduleSync();
}

void EngineHost::ScheduleSync() {
    if (syncScheduled_.exchange(true)) return;  // one is already on its way
    TP_CALLBACK_ENVIRON env;
    InitializeThreadpoolEnvironment(&env);
    // Keeps our DLL loaded until the callback has returned, even if the host unloads the IME.
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&SyncCallback), &self) &&
        self)
        SetThreadpoolCallbackLibrary(&env, self);
    if (!TrySubmitThreadpoolCallback(&SyncCallback, nullptr, &env)) syncScheduled_ = false;
    DestroyThreadpoolEnvironment(&env);
}

void EngineHost::RunSync() {
    syncScheduled_ = false;  // events recorded from here on schedule a new run
    Learner* learner = nullptr;
    std::shared_ptr<Dictionary> dictionary;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        learner = learner_.get();
        dictionary = dictionary_;
    }
    // Packs turned on/off and phrases edited in the settings app take effect here.
    if (dictionary) {
        dictionary->Reload();
        enterCommitsBopomofo_ = dictionary->Options().enterCommitsBopomofo;
    }
    if (learner) learner->Sync();
}

}  // namespace zy
