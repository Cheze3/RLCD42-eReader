// Note and chord detector. C++ port of chord_detect.py (detect_notes / name_chord).
// No Arduino or ESP-IDF headers here: input is a float frame, output is plain data.
//
// Keep the constants in sync with the top of chord_detect.py.
#pragma once

#include <cstddef>

#include "fft.h"

namespace chord {

constexpr int kSampleRate = 16000;
constexpr int kFrameSize = kFftSize;  // N_FFT
constexpr int kHop = 2048;            // HOP
constexpr int kNoteLo = 48;           // NOTE_LO (C3)
constexpr int kNoteHi = 84;           // NOTE_HI (C6)
constexpr int kHarmonics = 6;         // N_HARM
constexpr int kMaxNotes = 6;          // MAX_NOTES
constexpr float kWeightPow = 0.5f;    // WEIGHT_POW
constexpr float kFundFrac = 0.15f;    // FUND_FRAC
constexpr float kNoteThreshold = 0.3f;  // NOTE_THRESHOLD
constexpr float kSilenceRms = 0.01f;    // SILENCE_RMS
constexpr float kSpecFloorDb = -60.0f;  // SPEC_FLOOR_DB
constexpr float kSpecCeilDb = -6.0f;    // SPEC_CEIL_DB
constexpr int kNumKeys = kNoteHi - kNoteLo + 1;  // 37 keys, C3..C6

class Detector {
public:
    Detector();

    // frame: kFrameSize samples in [-1, 1]. Writes up to kMaxNotes MIDI notes,
    // sorted ascending, into notes_out. Returns how many were found.
    int detect_notes(const float* frame, int* notes_out);

    // One FFT per frame, everything a UI needs: frame RMS, per-key spectrum levels
    // (kNumKeys values in 0..1, for display) and the detected notes. Levels and RMS
    // are filled even for silent frames; notes are empty below the silence threshold.
    int analyze(const float* frame, int* notes_out, float* levels_out, float* rms_out);

    // RMS below which a frame counts as silence (default kSilenceRms).
    void set_silence_rms(float rms) { silence_rms_ = rms; }
    float silence_rms() const { return silence_rms_; }

    // Hann window (symmetric, same as numpy.hanning), for testing.
    const float* window() const { return window_; }

private:
    static float frame_rms(const float* frame);
    void compute_spectrum(const float* frame);
    void key_levels(float* levels_out) const;
    int search_notes(int* notes_out);

    float silence_rms_ = kSilenceRms;
    float window_[kFrameSize];
    float windowed_[kFrameSize];
    float spec_[kFrameSize / 2 + 1];
    // bands_[note - kNoteLo][harmonic - 1] = {begin, end} bin range (end exclusive)
    short bands_[kNoteHi - kNoteLo + 1][kHarmonics][2];
};

// "C4" style name of a MIDI note. out needs at least 5 bytes.
void midi_name(int midi, char* out, size_t out_size);

// Splits notes into display text ("C4 E4 G4") and a label: the chord name if the
// notes form a known chord, else the text itself, or "-" (text "") for no notes.
// label and text each need room for kMaxNotes * 5 characters.
void describe(const int* notes, int count, char* label, size_t label_size, char* text,
              size_t text_size);

// Names the chord formed by the notes (MIDI numbers), e.g. "C", "Am/C", "G7/B".
// Returns false if fewer than 3 notes or no known chord matches.
bool name_chord(const int* notes, int count, char* out, size_t out_size);

}  // namespace chord
