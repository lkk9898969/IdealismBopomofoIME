#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zy {

// One line of the learning journal (learning.txt, tab-separated, UTF-8):
//
//   P <time> <reading> <value>          a word the user chose was committed (for readings that
//                                       have counts, the default being accepted counts too)
//   N <time> <reading> <value>          a run of characters the user put together was committed
//   L <time> <reading> <value>          that run is now a learned phrase
//   F <time> <reading> <value>          forget everything learned about it
//   A <time> <reading> <value> <count>  pick count as of <time>   (written when compacting)
//   M <time> <reading> <value> <count>  observation count         (written when compacting)
//
// <time> is seconds since 1970; <reading> is Bopomofo syllables joined by '-'.
struct LearnEvent {
    char type = 0;
    double time = 0;
    std::string reading;
    std::string value;
    double count = 0;  // A and M only
};

// What has been learned about the user's habits. Plain data + arithmetic: no I/O, no locking
// (Learner guards it), so it is easy to test and to rebuild by replaying the journal.
//
//  * Pick counts (layer A): how often each "reading -> value" was picked, decaying with a half-life
//    of 30 days. They raise that candidate's score, so it moves forward in the list and, once it
//    is close enough to the default, becomes the default.
//  * Phrases (layer C): runs of characters the user keeps putting together; after
//    kPhraseThreshold observations Learner promotes a run to a learned phrase.
class LearningModel {
public:
    static constexpr double kHalfLifeSeconds = 30.0 * 24 * 3600;
    static constexpr double kBoostScale = 0.6;  // boost = scale * log2(1 + decayed count)
    static constexpr double kMaxBoost = 2.5;    // in log10-probability units, like dictionary scores
    static constexpr int kPhraseThreshold = 3;
    static constexpr double kObservationLifetimeSeconds = 60.0 * 24 * 3600;

    void Apply(const LearnEvent& e);
    void Clear();

    // Score bonus for a candidate; 0 if it was never picked.
    double Boost(const std::string& reading, const std::string& value, double now) const;
    bool HasPicks(const std::string& reading) const { return picks_.count(reading) != 0; }

    int ObservationCount(const std::string& reading, const std::string& value) const;
    bool IsLearnedPhrase(const std::string& reading, const std::string& value) const;
    std::vector<std::string> LearnedPhrases(const std::string& reading) const;
    bool HasLearnedPhrases(const std::string& reading) const;

    // The shortest list of events that rebuilds the current state (drops what has decayed away).
    std::vector<LearnEvent> Snapshot(double now) const;

    static std::string FormatEvent(const LearnEvent& e);  // one line, with the trailing '\n'
    static bool ParseEvent(std::string_view line, LearnEvent* e);
    // Readings/values that may be learned: non-empty, reasonably short, no control characters,
    // and not one of the engine's internal readings (punctuation, literals start with '_').
    static bool Learnable(std::string_view reading, std::string_view value);

private:
    struct Pick {
        double count = 0;  // as of `time`
        double time = 0;
    };
    struct Phrase {
        double count = 0;
        double time = 0;
        bool learned = false;
    };
    static double Decayed(const Pick& p, double now);

    std::unordered_map<std::string, std::unordered_map<std::string, Pick>> picks_;      // reading -> value
    std::unordered_map<std::string, std::unordered_map<std::string, Phrase>> phrases_;  // reading -> value
};

}  // namespace zy
