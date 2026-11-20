#include "analyzer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chord {

Analyzer::Analyzer(int smooth, int min_votes)
    : smooth_(std::min(std::max(smooth, 1), kMaxSmooth)), min_votes_(min_votes) {}

bool Analyzer::push(const float* samples, int count) {
    // Keep the most recent kFrameSize samples.
    if (count >= kFrameSize) {
        std::memcpy(buf_, samples + (count - kFrameSize), kFrameSize * sizeof(float));
    } else {
        std::memmove(buf_, buf_ + count, (kFrameSize - count) * sizeof(float));
        std::memcpy(buf_ + (kFrameSize - count), samples, count * sizeof(float));
    }
    filled_ = std::min(filled_ + count, kFrameSize);
    if (filled_ < kFrameSize) return false;

    Entry e;
    e.n_notes = detector_.analyze(buf_, e.notes, shown_.levels, &shown_.rms);
    describe(e.notes, e.n_notes, e.label, sizeof e.label, e.text, sizeof e.text);

    // Append to history, dropping the oldest once full.
    if (hist_len_ == smooth_) {
        for (int i = 1; i < hist_len_; ++i) hist_[i - 1] = hist_[i];
        --hist_len_;
    }
    hist_[hist_len_++] = e;

    // Majority vote over labels. Ties go to the label seen first (oldest), as in
    // Python's Counter.most_common(1).
    int top = 0, top_votes = 0;
    for (int i = 0; i < hist_len_; ++i) {
        bool seen_before = false;
        for (int j = 0; j < i; ++j) {
            if (std::strcmp(hist_[j].label, hist_[i].label) == 0) seen_before = true;
        }
        if (seen_before) continue;
        int votes = 0;
        for (int j = i; j < hist_len_; ++j) {
            if (std::strcmp(hist_[j].label, hist_[i].label) == 0) ++votes;
        }
        if (votes > top_votes) {
            top_votes = votes;
            top = i;
        }
    }
    if (top_votes >= min_votes_) {
        // Notes and text come from the newest entry carrying the winning label.
        int latest = top;
        for (int i = hist_len_ - 1; i >= 0; --i) {
            if (std::strcmp(hist_[i].label, hist_[top].label) == 0) {
                latest = i;
                break;
            }
        }
        std::snprintf(shown_.label, sizeof shown_.label, "%s", hist_[top].label);
        std::snprintf(shown_.text, sizeof shown_.text, "%s", hist_[latest].text);
        shown_.n_notes = hist_[latest].n_notes;
        for (int i = 0; i < shown_.n_notes; ++i) shown_.notes[i] = hist_[latest].notes[i];
    }
    return true;
}

}  // namespace chord
