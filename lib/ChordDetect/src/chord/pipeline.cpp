#include "pipeline.h"

#include <algorithm>

namespace chord {

void Pipeline::set_silence_rms(float rms) {
    analyzer_.detector().set_silence_rms(rms);
    ui_.set_silence_rms(rms);
}

void Pipeline::feed(const float* samples, int count) {
    while (count > 0) {
        const int take = std::min(count, kHop - fifo_n_);
        std::copy(samples, samples + take, fifo_ + fifo_n_);
        fifo_n_ += take;
        samples += take;
        count -= take;
        if (fifo_n_ == kHop) {
            if (analyzer_.push(fifo_, kHop)) ui_.update(analyzer_.shown());
            fifo_n_ = 0;
        }
    }
}

}  // namespace chord
