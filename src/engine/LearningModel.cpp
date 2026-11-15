#include "engine/LearningModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace zy {
namespace {

// Splits on tabs; returns the number of fields found (at most max).
size_t SplitTabs(std::string_view line, std::string_view* fields, size_t max) {
    size_t n = 0;
    while (n < max) {
        const size_t tab = line.find('\t');
        fields[n++] = line.substr(0, tab);
        if (tab == std::string_view::npos) break;
        line.remove_prefix(tab + 1);
    }
    return n;
}

bool ParseNumber(std::string_view s, double* out) {
    if (s.empty() || s.size() > 32) return false;
    const std::string copy(s);
    char* end = nullptr;
    const double v = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(v) || v < 0) return false;
    *out = v;
    return true;
}

}  // namespace

bool LearningModel::Learnable(std::string_view reading, std::string_view value) {
    if (reading.empty() || value.empty() || reading.size() > 200 || value.size() > 100) return false;
    if (reading[0] == '_' || reading.find("-_") != std::string_view::npos) return false;
    for (std::string_view s : {reading, value})
        for (unsigned char c : s)
            if (c < 0x20 || c == 0x7F) return false;
    // Bopomofo typed as text (ㄅ..ㄩ are U+3105..U+3129: E3 84 85 .. E3 84 A9) is not a habit.
    for (size_t i = 0; i + 2 < value.size(); ++i)
        if (static_cast<unsigned char>(value[i]) == 0xE3 && static_cast<unsigned char>(value[i + 1]) == 0x84 &&
            static_cast<unsigned char>(value[i + 2]) >= 0x85 && static_cast<unsigned char>(value[i + 2]) <= 0xA9)
            return false;
    return true;
}

std::string LearningModel::FormatEvent(const LearnEvent& e) {
    char head[40];
    std::snprintf(head, sizeof(head), "%c\t%.0f\t", e.type, e.time);
    std::string line = head;
    line += e.reading;
    line += '\t';
    line += e.value;
    if (e.type == 'A' || e.type == 'M') {
        char count[32];
        std::snprintf(count, sizeof(count), "\t%.3f", e.count);
        line += count;
    }
    line += '\n';
    return line;
}

bool LearningModel::ParseEvent(std::string_view line, LearnEvent* e) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    if (line.empty() || line[0] == '#') return false;
    std::string_view f[5];
    const size_t n = SplitTabs(line, f, 5);
    if (n < 4 || f[0].size() != 1) return false;
    const char type = f[0][0];
    const bool hasCount = type == 'A' || type == 'M';
    if (!hasCount && type != 'P' && type != 'N' && type != 'L' && type != 'F') return false;
    if (n != (hasCount ? 5u : 4u)) return false;
    LearnEvent out;
    out.type = type;
    if (!ParseNumber(f[1], &out.time)) return false;
    if (!Learnable(f[2], f[3])) return false;
    out.reading = std::string(f[2]);
    out.value = std::string(f[3]);
    if (hasCount && !ParseNumber(f[4], &out.count)) return false;
    *e = std::move(out);
    return true;
}

double LearningModel::Decayed(const Pick& p, double now) {
    const double age = now - p.time;
    if (age <= 0) return p.count;  // clock went backwards: don't inflate
    return p.count * std::exp2(-age / kHalfLifeSeconds);
}

void LearningModel::Apply(const LearnEvent& e) {
    if (!Learnable(e.reading, e.value)) return;
    switch (e.type) {
        case 'P': {
            Pick& p = picks_[e.reading][e.value];
            p.count = Decayed(p, e.time) + 1.0;
            p.time = std::max(p.time, e.time);
            break;
        }
        case 'A': {
            Pick& p = picks_[e.reading][e.value];
            p.count = e.count;
            p.time = e.time;
            break;
        }
        case 'N': {
            Phrase& ph = phrases_[e.reading][e.value];
            ph.count += 1.0;
            ph.time = std::max(ph.time, e.time);
            break;
        }
        case 'M': {
            Phrase& ph = phrases_[e.reading][e.value];
            ph.count = e.count;
            ph.time = e.time;
            break;
        }
        case 'L': {
            Phrase& ph = phrases_[e.reading][e.value];
            ph.learned = true;
            ph.time = std::max(ph.time, e.time);
            break;
        }
        case 'F': {
            if (auto it = picks_.find(e.reading); it != picks_.end()) {
                it->second.erase(e.value);
                if (it->second.empty()) picks_.erase(it);
            }
            if (auto it = phrases_.find(e.reading); it != phrases_.end()) {
                it->second.erase(e.value);
                if (it->second.empty()) phrases_.erase(it);
            }
            break;
        }
        default:
            break;
    }
}

void LearningModel::Clear() {
    picks_.clear();
    phrases_.clear();
}

double LearningModel::Boost(const std::string& reading, const std::string& value, double now) const {
    auto it = picks_.find(reading);
    if (it == picks_.end()) return 0;
    auto vt = it->second.find(value);
    if (vt == it->second.end()) return 0;
    return std::min(kMaxBoost, kBoostScale * std::log2(1.0 + Decayed(vt->second, now)));
}

int LearningModel::ObservationCount(const std::string& reading, const std::string& value) const {
    auto it = phrases_.find(reading);
    if (it == phrases_.end()) return 0;
    auto vt = it->second.find(value);
    return vt == it->second.end() ? 0 : static_cast<int>(vt->second.count);
}

bool LearningModel::IsLearnedPhrase(const std::string& reading, const std::string& value) const {
    auto it = phrases_.find(reading);
    if (it == phrases_.end()) return false;
    auto vt = it->second.find(value);
    return vt != it->second.end() && vt->second.learned;
}

bool LearningModel::HasLearnedPhrases(const std::string& reading) const {
    auto it = phrases_.find(reading);
    if (it == phrases_.end()) return false;
    for (const auto& [value, ph] : it->second)
        if (ph.learned) return true;
    return false;
}

std::vector<std::string> LearningModel::LearnedPhrases(const std::string& reading) const {
    std::vector<std::string> out;
    auto it = phrases_.find(reading);
    if (it == phrases_.end()) return out;
    for (const auto& [value, ph] : it->second)
        if (ph.learned) out.push_back(value);
    std::sort(out.begin(), out.end());  // stable order whatever the hash table does
    return out;
}

std::vector<LearnEvent> LearningModel::Snapshot(double now) const {
    std::vector<LearnEvent> out;
    for (const auto& [reading, values] : picks_)
        for (const auto& [value, p] : values)
            if (Decayed(p, now) >= 0.05) out.push_back({'A', p.time, reading, value, p.count});
    for (const auto& [reading, values] : phrases_)
        for (const auto& [value, ph] : values) {
            const bool fresh = now - ph.time < kObservationLifetimeSeconds;
            if (ph.count > 0 && (ph.learned || fresh)) out.push_back({'M', ph.time, reading, value, ph.count});
            if (ph.learned) out.push_back({'L', ph.time, reading, value, 0});
        }
    // Sorted, so the file is stable and pleasant to read.
    std::sort(out.begin(), out.end(), [](const LearnEvent& a, const LearnEvent& b) {
        if (a.reading != b.reading) return a.reading < b.reading;
        if (a.value != b.value) return a.value < b.value;
        return a.type > b.type;  // M before L
    });
    return out;
}

}  // namespace zy
