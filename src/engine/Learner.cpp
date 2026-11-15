#include "engine/Learner.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>

namespace zy {
namespace {

using Formosa::Gramambular2::LanguageModel;
using LM = Formosa::Gramambular2::LanguageModel;  // Learner::LanguageModel() hides the type inside members
using Formosa::Gramambular2::ReadingGrid;

// Same defaults as McBopomofo.
constexpr size_t kOverrideCapacity = 500;
constexpr double kOverrideHalfLifeSeconds = 5400.0;

constexpr size_t kMaxPhraseReadings = 4;      // learned phrases are 2-4 characters
constexpr double kLearnedPhraseScore = -3.0;  // log10 probability: beats character-by-character
constexpr size_t kMaxPending = 2000;          // if the journal can't be written, don't grow forever

constexpr char kHeaderPrefix[] = "# IdealismIME learning v1";
constexpr char kLegacyHeaderPrefix[] = "# ZhuyinIME learning v1";  // before the rename (same format)
constexpr wchar_t kMutexName[] = L"Local\\IdealismIME.LearningJournal";
constexpr DWORD kMutexWaitMs = 250;
constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

double SystemNow() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

std::string NewHeader() {
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    char line[96];
    std::snprintf(line, sizeof(line), "%s %08lx%016llx\n", kHeaderPrefix, GetCurrentProcessId(),
                  static_cast<unsigned long long>(counter.QuadPart));
    return line;
}

struct FileHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~FileHandle() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    explicit operator bool() const { return h != INVALID_HANDLE_VALUE; }
};

bool WriteAll(HANDLE h, const std::string& data) {
    size_t done = 0;
    while (done < data.size()) {
        DWORD written = 0;
        if (!WriteFile(h, data.data() + done, static_cast<DWORD>(data.size() - done), &written, nullptr) ||
            written == 0)
            return false;
        done += written;
    }
    return true;
}

bool ReadAt(HANDLE h, uint64_t offset, size_t size, std::string* out) {
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) return false;
    out->resize(size);
    size_t done = 0;
    while (done < size) {
        DWORD read = 0;
        if (!ReadFile(h, out->data() + done, static_cast<DWORD>(size - done), &read, nullptr)) return false;
        if (read == 0) break;
        done += read;
    }
    out->resize(done);
    return true;
}

// Cross-process lock for writers of the journal. Never waits long; a lock abandoned by a process
// that died still counts as acquired.
class JournalLock {
public:
    JournalLock() {
        mutex_ = CreateMutexW(nullptr, FALSE, kMutexName);
        if (!mutex_) return;
        const DWORD r = WaitForSingleObject(mutex_, kMutexWaitMs);
        held_ = r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
    }
    ~JournalLock() {
        if (held_) ReleaseMutex(mutex_);
        if (mutex_) CloseHandle(mutex_);
    }
    bool Held() const { return held_; }

private:
    HANDLE mutex_ = nullptr;
    bool held_ = false;
};

// The dictionary with learning applied: learned phrases added, picked candidates boosted.
class LearningLM : public LanguageModel {
public:
    LearningLM(std::shared_ptr<LanguageModel> base, Learner* learner)
        : base_(std::move(base)), learner_(learner) {}

    bool hasUnigrams(const std::string& reading) override {
        return base_->hasUnigrams(reading) || learner_->HasLearnedPhrases(reading);
    }
    std::vector<Unigram> getUnigrams(const std::string& reading) override {
        auto unigrams = base_->getUnigrams(reading);
        learner_->Adjust(reading, &unigrams);
        return unigrams;
    }

private:
    std::shared_ptr<LanguageModel> base_;
    Learner* learner_;
};

bool Internal(const std::string& reading) { return reading.empty() || reading[0] == '_'; }

}  // namespace

Learner::Learner(std::shared_ptr<LM> base, std::wstring journalPath)
    : base_(std::move(base)),
      journalPath_(std::move(journalPath)),
      clock_(&SystemNow),
      overrides_(kOverrideCapacity, kOverrideHalfLifeSeconds) {
    lm_ = std::make_shared<LearningLM>(base_, this);
}

// ---- Language model side (key path: memory only) ----

void Learner::Adjust(const std::string& reading, std::vector<LM::Unigram>* unigrams) {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const bool picks = model_.HasPicks(reading);
    const bool phrases = model_.HasLearnedPhrases(reading);
    if (!picks && !phrases) return;

    if (phrases) {
        double top = -std::numeric_limits<double>::infinity();
        for (const auto& u : *unigrams) top = std::max(top, u.score());
        // A learned phrase outranks dictionary phrases with the same reading, and the same
        // characters typed one by one.
        const double score = std::max(kLearnedPhraseScore, top + 0.5);
        for (const auto& value : model_.LearnedPhrases(reading)) {
            const bool known = std::any_of(unigrams->begin(), unigrams->end(),
                                           [&](const LM::Unigram& u) { return u.value() == value; });
            if (!known) unigrams->emplace_back(value, score);
        }
    }
    if (picks) {
        const double now = Now();
        for (auto& u : *unigrams) {
            const double boost = model_.Boost(reading, u.value(), now);
            if (boost > 0) u = LM::Unigram(u.value(), u.score() + boost, u.rawValue());
        }
    }
}

bool Learner::HasLearnedPhrases(const std::string& reading) {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return model_.HasLearnedPhrases(reading);
}

double Learner::Boost(const std::string& reading, const std::string& value) {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return model_.Boost(reading, value, Now());
}

bool Learner::IsLearnedPhrase(const std::string& reading, const std::string& value) {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return model_.IsLearnedPhrase(reading, value);
}

std::vector<LearnEvent> Learner::Snapshot() {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return model_.Snapshot(Now());
}

size_t Learner::PendingCount() {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return pending_.size();
}

// ---- UserModel (key path: memory only) ----

McBopomofo::UserOverrideModel::Suggestion Learner::Suggest(const ReadingGrid::WalkResult& walk, size_t cursor) {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return overrides_.suggest(walk, cursor, Now());
}

void Learner::Observe(const ReadingGrid::WalkResult& before, const ReadingGrid::WalkResult& after, size_t cursor) {
    const double now = Now();
    std::vector<LearnEvent> events;
    // (Pick counts are taken when the sentence is committed, see Commit.)
    std::string pickedReading, pickedValue;
    if (auto it = after.findNodeAt(cursor); it != after.nodes.cend()) {
        pickedReading = (*it)->reading();
        pickedValue = (*it)->currentUnigram().value();
    }
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        overrides_.observe(before, after, cursor, now);
        // Picking something else where a learned phrase stood means that phrase was wrong here:
        // unlearn it, or it would keep coming back.
        if (auto it = before.findNodeAt(cursor); it != before.nodes.cend()) {
            const std::string& reading = (*it)->reading();
            const std::string value = (*it)->currentUnigram().value();
            if ((reading != pickedReading || value != pickedValue) && model_.IsLearnedPhrase(reading, value))
                events.push_back({'F', now, reading, value, 0});
        }
    }
    Record(std::move(events));
}

void Learner::Commit(const ReadingGrid::WalkResult& walk) {
    // New phrases: look for runs of 2+ adjacent words (2-4 characters in all) that contain at least one choice
    // of the user's and are not a phrase yet. Each sighting counts; at the threshold the run
    // becomes a learned phrase.
    struct Run {
        size_t first, last;
        std::string reading, value;
        int count = 0;
    };
    std::vector<Run> runs;
    const auto& nodes = walk.nodes;
    for (size_t i = 0; i < nodes.size(); ++i) {
        std::string reading, value;
        size_t length = 0;
        bool chosen = false;
        for (size_t j = i; j < nodes.size(); ++j) {
            const auto& node = nodes[j];
            if (Internal(node->reading())) break;  // punctuation and literals end a run
            length += node->spanningLength();
            if (length > kMaxPhraseReadings) break;
            if (j > i) reading += ReadingGrid::kDefaultSeparator;
            reading += node->reading();
            value += node->currentUnigram().value();
            chosen = chosen || node->isOverridden();
            if (j == i || !chosen || !LearningModel::Learnable(reading, value)) continue;
            const auto known = lm_->getUnigrams(reading);
            if (std::any_of(known.begin(), known.end(), [&](const auto& u) { return u.value() == value; })) continue;
            runs.push_back({i, j, reading, value});
        }
    }

    const double now = Now();
    std::vector<LearnEvent> events;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        // Pick counts: every word the user chose, plus every word of a reading that has counts
        // already — the default being accepted must count too, or a word used one time in five
        // would overtake the one used the other four.
        for (const auto& node : nodes) {
            const std::string& reading = node->reading();
            if (!node->isOverridden() && !model_.HasPicks(reading)) continue;
            const std::string value = node->currentUnigram().value();
            if (LearningModel::Learnable(reading, value)) events.push_back({'P', now, reading, value, 0});
        }
        for (auto& run : runs) {
            run.count = model_.ObservationCount(run.reading, run.value) + 1;
            events.push_back({'N', now, run.reading, run.value, 0});
        }
    }
    for (const auto& run : runs) {
        if (run.count < LearningModel::kPhraseThreshold) continue;
        // "陳曉" and "曉明" inside "陳曉明" are explained by the longer run: learn only that one.
        const bool inside = std::any_of(runs.begin(), runs.end(), [&](const Run& other) {
            return &other != &run && other.count >= LearningModel::kPhraseThreshold && other.first <= run.first &&
                   run.last <= other.last;
        });
        if (!inside) events.push_back({'L', now, run.reading, run.value, 0});
    }
    Record(std::move(events));
}

void Learner::Forget(const std::string& reading, const std::string& value) {
    if (!LearningModel::Learnable(reading, value)) return;
    Record({{'F', Now(), reading, value, 0}});
}

void Learner::Record(std::vector<LearnEvent> events) {
    if (events.empty()) return;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        for (const auto& e : events) model_.Apply(e);
        if (journalPath_.empty()) return;  // memory only: nothing to write
        pending_.insert(pending_.end(), std::make_move_iterator(events.begin()),
                        std::make_move_iterator(events.end()));
        if (pending_.size() > kMaxPending)
            pending_.erase(pending_.begin(), pending_.begin() + static_cast<ptrdiff_t>(pending_.size() - kMaxPending));
    }
    if (scheduler_) scheduler_();
}

void Learner::Requeue(std::vector<LearnEvent> events) {
    std::lock_guard<std::mutex> lock(modelMutex_);
    pending_.insert(pending_.begin(), std::make_move_iterator(events.begin()), std::make_move_iterator(events.end()));
}

// ---- Journal (file I/O: never on the key path) ----

void Learner::Load() {
    if (journalPath_.empty()) return;
    std::lock_guard<std::mutex> io(ioMutex_);
    ReadResult r = ReadNew();
    if (!r.ok) return;
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (r.reset) model_.Clear();
    for (const auto& e : r.events) model_.Apply(e);
    if (r.reset)
        for (const auto& e : pending_) model_.Apply(e);
}

void Learner::Sync() {
    if (journalPath_.empty()) return;
    std::lock_guard<std::mutex> io(ioMutex_);

    std::vector<LearnEvent> mine;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        mine.swap(pending_);
    }
    // Writers take the cross-process lock, so that "read what is new, then append" is atomic and
    // nobody's events are lost when the journal is compacted. Readers don't need it.
    std::unique_ptr<JournalLock> journalLock;
    if (!mine.empty()) {
        journalLock = std::make_unique<JournalLock>();
        if (!journalLock->Held()) {
            Requeue(std::move(mine));  // busy: try again at the next Sync
            return;
        }
    }

    ReadResult r = ReadNew();
    if (r.ok) {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (r.reset) model_.Clear();
        for (const auto& e : r.events) model_.Apply(e);
        if (r.reset) {
            // The model was rebuilt from the file; put back what only we know about.
            for (const auto& e : mine) model_.Apply(e);
            for (const auto& e : pending_) model_.Apply(e);
        }
    }
    if (mine.empty()) return;
    if (!r.ok) {
        Requeue(std::move(mine));
        return;
    }

    std::string text;
    for (const auto& e : mine) text += LearningModel::FormatEvent(e);
    if (!Append(text)) {
        Requeue(std::move(mine));
        return;
    }
    if (offset_ > compactThreshold_) Compact();
}

Learner::ReadResult Learner::ReadNew() {
    ReadResult result;
    FileHandle file{CreateFileW(journalPath_.c_str(), GENERIC_READ, kShareAll, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file) {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return result;
        // No journal (never written, or the user deleted it to clear everything).
        result.ok = true;
        result.reset = !generation_.empty() || offset_ != 0;
        generation_.clear();
        offset_ = 0;
        return result;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file.h, &size)) return result;
    const uint64_t fileSize = static_cast<uint64_t>(size.QuadPart);

    // The first line identifies the file: compaction (or the user) replacing it changes the line.
    std::string head;
    if (!ReadAt(file.h, 0, static_cast<size_t>(std::min<uint64_t>(fileSize, 128)), &head)) return result;
    std::string generation = "(no header)";
    if (head.compare(0, sizeof(kHeaderPrefix) - 1, kHeaderPrefix) == 0 ||
        head.compare(0, sizeof(kLegacyHeaderPrefix) - 1, kLegacyHeaderPrefix) == 0) {
        const size_t eol = head.find('\n');
        if (eol == std::string::npos) return result;  // header still being written
        generation = head.substr(0, eol);
    }
    if (generation != generation_ || fileSize < offset_) {
        result.reset = true;
        generation_ = generation;
        offset_ = 0;
    }

    const uint64_t available = fileSize - offset_;
    if (available > (64u << 20)) return result;  // not ours, surely
    std::string data;
    if (!ReadAt(file.h, offset_, static_cast<size_t>(available), &data)) return result;
    // Only whole lines: a writer may be in the middle of one.
    const size_t end = data.rfind('\n');
    const size_t consumed = end == std::string::npos ? 0 : end + 1;
    size_t pos = 0;
    while (pos < consumed) {
        const size_t eol = data.find('\n', pos);
        LearnEvent e;
        if (LearningModel::ParseEvent(std::string_view(data).substr(pos, eol - pos), &e))
            result.events.push_back(std::move(e));
        pos = eol + 1;
    }
    offset_ += consumed;
    result.ok = true;
    return result;
}

// Caller holds the journal lock and has just read to the end of the file.
bool Learner::Append(const std::string& text) {
    const size_t slash = journalPath_.find_last_of(L"\\/");
    if (slash != std::wstring::npos) CreateDirectoryW(journalPath_.substr(0, slash).c_str(), nullptr);

    FileHandle file{CreateFileW(journalPath_.c_str(), GENERIC_READ | GENERIC_WRITE, kShareAll, nullptr, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file.h, &size)) return false;

    std::string out;
    if (size.QuadPart == 0) {
        out = NewHeader();
        generation_ = out.substr(0, out.size() - 1);
        offset_ = 0;
    } else {
        // A process killed in mid-write leaves half a line (which ReadNew skipped, so the file may
        // be longer than offset_); keep ours from merging with it.
        std::string last;
        if (!ReadAt(file.h, static_cast<uint64_t>(size.QuadPart) - 1, 1, &last)) return false;
        if (last != "\n") out = "\n";
    }
    out += text;
    LARGE_INTEGER zero = {};
    if (!SetFilePointerEx(file.h, zero, nullptr, FILE_END) || !WriteAll(file.h, out)) return false;
    offset_ = static_cast<uint64_t>(size.QuadPart) + out.size();
    return true;
}

// Rewrites the journal as the shortest list of events giving the same state. Caller holds the
// journal lock, and the file holds everything the model knows except pending_.
bool Learner::Compact() {
    std::string content = NewHeader();
    const std::string generation = content.substr(0, content.size() - 1);
    std::vector<LearnEvent> absorbed;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        for (const auto& e : model_.Snapshot(Now())) content += LearningModel::FormatEvent(e);
        absorbed.swap(pending_);  // the snapshot already includes these
    }
    const std::wstring temp = journalPath_ + L".tmp";
    bool written = false;
    {
        FileHandle file{CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                    nullptr)};
        written = file && WriteAll(file.h, content) && FlushFileBuffers(file.h);
    }
    // Fails harmlessly if another process has the journal open right now; we try again next time.
    if (written && MoveFileExW(temp.c_str(), journalPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        generation_ = generation;
        offset_ = content.size();
        return true;
    }
    DeleteFileW(temp.c_str());
    Requeue(std::move(absorbed));
    return false;
}

}  // namespace zy
