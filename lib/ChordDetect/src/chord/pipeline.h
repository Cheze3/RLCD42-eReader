// Audio in, Scene out: the analyzer, the display smoothing and the hop buffering, in one place
// so the SDL window, the preview tool and (later) the ESP32 firmware drive it the same way.
#pragma once

#include "analyzer.h"
#include "ui.h"

namespace chord {

class Pipeline {
public:
    Pipeline() {}

    // RMS below which input counts as silence (also moves the meter's "min" tick).
    void set_silence_rms(float rms);

    // Call once per displayed frame, before feed(): the bars fall a little.
    void tick() { ui_.tick(); }

    // Any amount of 16 kHz mono audio. The analyzer runs once per kHop samples.
    void feed(const float* samples, int count);

    const Scene& scene() const { return ui_.scene(); }

private:
    Analyzer analyzer_;
    UiState ui_;
    float fifo_[kHop] = {};
    int fifo_n_ = 0;
};

}  // namespace chord
