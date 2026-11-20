#pragma once

#include <driver/i2s_std.h>

#include "AudioSink.h"

// Waveshare ESP32-S3-RLCD-4.2 speaker: ES8311 codec on the shared I2S bus, amplifier enable on
// GPIO46. The codec is configured for 16-48 kHz with an external MCLK of 256 x the sample rate.
class RlcdSpeaker final : public AudioSink {
 public:
  ~RlcdSpeaker() override { end(); }

  bool begin(uint32_t sampleRate) override;
  bool setSampleRate(uint32_t sampleRate) override;
  size_t write(const int16_t* stereo, size_t frames) override;
  void end() override;
  bool active() const override { return running; }

 private:
  i2s_chan_handle_t tx = nullptr;
  bool running = false;
  uint32_t rate = 0;
};
