#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "UserOverrideModel.h"
#include "engine/Composer.h"
#include "engine/LearningModel.h"

namespace zy {

// Learns the user's habits and applies them:
//
//  A. Pick counts: candidates picked from the list get a score bonus (LearningModel).
//  B. Context overrides: "after these words, this reading means that" — McBopomofo's
//     UserOverrideModel. Short-term (half-life 1.5 h) and per process, as upstream designed it.
//  C. New phrases: a run of characters the user keeps putting together becomes a phrase.
//
// A and C are permanent and shared by every program: each process applies its own events at once
// and appends them to a journal (learning.txt); Sync() also reads what the others appended.
//
// Threading: Suggest/Observe/Commit/Forget and the language model run on UI threads, on the key
// path — they only touch memory under a short lock. Load() and Sync() do file I/O; Sync() must be
// called off the UI thread (EngineHost uses the thread pool). Never both locks at once.
class Learner final : public UserModel {
public:
    // journalPath empty = keep everything in memory only.
    Learner(std::shared_ptr<Formosa::Gramambular2::LanguageModel> base, std::wstring journalPath);

    // The dictionary with learning applied; this is what the Composer should use.
    std::shared_ptr<Formosa::Gramambular2::LanguageModel> LanguageModel() { return lm_; }

    // Called (outside any lock) whenever there are new events to write; should arrange for Sync().
    void SetScheduler(std::function<void()> scheduler) { scheduler_ = std::move(scheduler); }
    void SetClock(std::function<double()> clock) { clock_ = std::move(clock); }  // tests
    void SetCompactThreshold(uint64_t bytes) { compactThreshold_ = bytes; }      // tests

    void Load();  // first read of the journal; cheap enough for activation
    void Sync();  // read others' events, append ours, compact when large. Blocking file I/O.

    // UserModel
    McBopomofo::UserOverrideModel::Suggestion Suggest(const Formosa::Gramambular2::ReadingGrid::WalkResult& walk,
                                                      size_t cursor) override;
    void Observe(const Formosa::Gramambular2::ReadingGrid::WalkResult& before,
                 const Formosa::Gramambular2::ReadingGrid::WalkResult& after, size_t cursor) override;
    void Commit(const Formosa::Gramambular2::ReadingGrid::WalkResult& walk) override;
    void Forget(const std::string& reading, const std::string& value) override;

    // For the language model wrapper.
    void Adjust(const std::string& reading, std::vector<Formosa::Gramambular2::LanguageModel::Unigram>* unigrams);
    bool HasLearnedPhrases(const std::string& reading);

    // Introspection (tests, future settings UI).
    double Boost(const std::string& reading, const std::string& value);
    bool IsLearnedPhrase(const std::string& reading, const std::string& value);
    size_t PendingCount();
    // Everything learned, as the compact list of events a journal compaction would write
    // (A = pick counts, M/L = phrases). For the settings app.
    std::vector<LearnEvent> Snapshot();

private:
    struct ReadResult {
        bool ok = false;
        bool reset = false;  // the file is not the one we were following: start over
        std::vector<LearnEvent> events;
    };

    double Now() const { return clock_(); }
    void Record(std::vector<LearnEvent> events);
    void Requeue(std::vector<LearnEvent> events);
    ReadResult ReadNew();
    bool Append(const std::string& text);
    bool Compact();

    std::shared_ptr<Formosa::Gramambular2::LanguageModel> base_;
    std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm_;
    std::wstring journalPath_;
    std::function<void()> scheduler_;
    std::function<double()> clock_;
    uint64_t compactThreshold_ = 256 * 1024;

    std::mutex modelMutex_;  // model_, overrides_, pending_
    LearningModel model_;
    McBopomofo::UserOverrideModel overrides_;
    std::vector<LearnEvent> pending_;  // applied here, not yet in the journal

    std::mutex ioMutex_;  // one Load/Sync at a time; generation_, offset_
    std::string generation_;  // header line of the journal we are following
    uint64_t offset_ = 0;     // how much of it we have read
};

}  // namespace zy
