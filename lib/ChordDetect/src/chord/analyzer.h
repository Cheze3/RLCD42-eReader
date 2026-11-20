// Rolling-window analyzer with label smoothing. C++ port of LiveAnalyzer in chord_detect.py.
// Platform-free: feed it 16 kHz mono float samples from any source (microphone, file).
#pragma once

#include "detector.h"

namespace chord {

// What a UI needs to draw one frame.
struct Shown {
    char label[kMaxNotes * 5 + 8] = "-";  // smoothed chord name, note text, or "-"
    char text[kMaxNotes * 5 + 8] = "";    // note names behind the label, e.g. "C4 E4 G4"
    int notes[kMaxNotes] = {};            // MIDI notes behind the label, ascending
    int n_notes = 0;
    float rms = 0.0f;                     // RMS of the latest frame
    float levels[kNumKeys] = {};          // latest per-key spectrum levels, 0..1
};

class Analyzer {
public:
    // The label shown changes only when it wins at least min_votes of the last
    // `smooth` frames (smooth <= kMaxSmooth).
    explicit Analyzer(int smooth = 5, int min_votes = 3);

    // Appends samples (any count). Once a full frame (kFrameSize samples) is buffered,
    // analyzes the latest frame and returns true; call it with one hop (kHop samples)
    // at a time for the same update rate as the Python version. Returns false while the
    // buffer is still filling.
    bool push(const float* samples, int count);

    const Shown& shown() const { return shown_; }
    Detector& detector() { return detector_; }

private:
    static constexpr int kMaxSmooth = 8;
    struct Entry {
        char label[kMaxNotes * 5 + 8];
        char text[kMaxNotes * 5 + 8];
        int notes[kMaxNotes];
        int n_notes;
    };

    Detector detector_;
    float buf_[kFrameSize] = {};
    int filled_ = 0;
    Entry hist_[kMaxSmooth];
    int hist_len_ = 0;  // entries used, oldest first in hist_[0..hist_len_)
    int smooth_;
    int min_votes_;
    Shown shown_;
};

}  // namespace chord
