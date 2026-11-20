// The three UIs (wheel, poster, tape), ported from visualizer.py. Platform-free: draws a
// Scene onto a Canvas, so the same code runs in the SDL2 window and on the ESP32.
#pragma once

#include "analyzer.h"
#include "canvas.h"

namespace chord {

enum class Mode { Wheel = 0, Poster = 1, Tape = 2 };
constexpr int kModeCount = 3;
const char* mode_name(Mode mode);

constexpr int kHistoryMax = 8;   // chord names remembered
constexpr int kTapeRows = 16;    // chroma rows shown by the tape UI

// Everything one frame needs.
struct Scene {
    int notes[kMaxNotes] = {};
    int n_notes = 0;
    char label[40] = "-";                    // chord name, note text or "-"
    char text[48] = "";                      // note names, e.g. "C4 E4 G4"
    char history[kHistoryMax][40] = {};      // recent labels, oldest first
    int n_history = 0;
    float levels[kNumKeys] = {};             // spectrum level per key, 0..1 (smoothed for display)
    float rms = 0.0f;                        // input level (smoothed for display)
    double silence_rms = 0.01;               // where the meter's "min" tick sits (double: it lands on a pixel edge)
    float rows[kTapeRows][12] = {};          // chroma history, oldest first
};

// Draws the scene. fifths arranges the wheel in circle-of-fifths order.
void render(Canvas& canvas, Mode mode, const Scene& scene, bool fifths = false);

// What the window loop keeps between frames. Call tick() once per displayed frame (the
// bars fall) and update() whenever the analyzer produced a new frame.
class UiState {
public:
    UiState();
    void tick();
    void update(const Shown& shown);
    void set_silence_rms(float rms) { scene_.silence_rms = rms; }
    const Scene& scene() const { return scene_; }

private:
    Scene scene_;
    char last_label_[40] = "-";
};

}  // namespace chord
