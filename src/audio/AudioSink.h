#pragma once

#include <cstddef>
#include <cstdint>

// Where 16-bit stereo PCM goes. The board speaker is one sink; a USB DAC can be another.
class AudioSink {
 public:
  virtual ~AudioSink() = default;
  // Starts output at `sampleRate`. Returns false when the hardware is not available.
  virtual bool begin(uint32_t sampleRate) = 0;
  // Switches to another rate (restarts the output; a short gap is expected).
  virtual bool setSampleRate(uint32_t sampleRate) = 0;
  // Blocks until `frames` interleaved L/R pairs are queued. Returns the frames accepted.
  virtual size_t write(const int16_t* stereo, size_t frames) = 0;
  virtual void end() = 0;
  virtual bool active() const = 0;
};
