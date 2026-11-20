#include "RlcdSpeaker.h"

#include <Arduino.h>
#include <Logging.h>
#include <Wire.h>

#include <algorithm>

#include "platform/RlcdI2c.h"

// Pins from the board schematic: the ES8311 shares the I2S bus with the ES7210 microphone ADC.
namespace {
constexpr int PIN_MCLK = 16, PIN_BCLK = 9, PIN_WS = 45, PIN_DIN = 10, PIN_DOUT = 8;
constexpr int PIN_PA = 46;
constexpr uint8_t ES8311_ADDR = 0x18;
constexpr uint8_t DAC_VOLUME_REG = 0xBF;  // 0 dB; volume is applied in software by the callers

bool es8311Write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

int es8311Read(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(static_cast<int>(ES8311_ADDR), 1) != 1) return -1;
  return Wire.read();
}

// Playback bring-up following Espressif's es8311 driver (slave mode, 16-bit I2S, DAC only, MCLK =
// 256 x fs). Only the DAC oversampling register depends on the rate. Not yet verified on the board
// at rates other than 16 kHz.
bool es8311Start(uint32_t rate) {
  struct RegVal {
    uint8_t reg, val;
  };
  const uint8_t dacOsr = rate <= 16000 ? 0x20 : 0x10;
  const RegVal init[] = {
      {0x44, 0x08},
      {0x44, 0x08},  // clear I2C noise
      {0x01, 0x30},
      {0x02, 0x00},
      {0x03, 0x10},
      {0x16, 0x24},
      {0x04, 0x10},
      {0x05, 0x00},
      {0x0B, 0x00},
      {0x0C, 0x00},
      {0x10, 0x1F},
      {0x11, 0x7F},
      {0x00, 0x80},  // reset released, slave mode, power on
      {0x01, 0x3F},  // all clocks on, MCLK from the pin
      // pre-div 1, x1, dividers 1, LRCK 256, BCLK div 4.
      {0x02, 0x00},
      {0x05, 0x00},
      {0x03, 0x10},
      {0x04, dacOsr},
      {0x07, 0x00},
      {0x08, 0xFF},
      {0x06, 0x03},
      {0x09, 0x0C},
      {0x0A, 0x0C},  // I2S, 16-bit
      {0x0D, 0x01},
      {0x0E, 0x02},
      {0x12, 0x00},
      {0x13, 0x10},
      {0x1B, 0x0A},
      {0x1C, 0x6A},
      {0x37, 0x08},  // DAC: bypass equalizer
      {0x31, 0x00},  // unmute
      {0x32, DAC_VOLUME_REG},
  };
  bool ok = true;
  for (const RegVal& r : init) ok &= es8311Write(r.reg, r.val);
  return ok;
}

void es8311Stop() {
  es8311Write(0x31, 0x60);  // mute
  es8311Write(0x12, 0x02);  // DAC power down
  es8311Write(0x0D, 0xFA);  // analog power down
  es8311Write(0x01, 0x30);  // clocks off
}
}  // namespace

bool RlcdSpeaker::begin(uint32_t sampleRate) {
  if (running) end();
  pinMode(PIN_PA, OUTPUT);
  digitalWrite(PIN_PA, LOW);

  rlcd::ensureI2c();

  // Transmit only. About 90 ms of DMA buffer (6 buffers) at any rate rides out short SD-card read
  // pauses; auto_clear sends silence, not a replay of old samples, if the buffer ever runs dry.
  constexpr uint32_t kBuffers = 6;
  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chanCfg.dma_desc_num = kBuffers;
  chanCfg.dma_frame_num = std::clamp<uint32_t>(sampleRate * 90 / 1000 / kBuffers, 128, 1000);
  chanCfg.auto_clear = true;
  if (i2s_new_channel(&chanCfg, &tx, nullptr) != ESP_OK) {
    LOG_ERR("SPK", "I2S channel alloc failed");
    tx = nullptr;
    end();
    return false;
  }
  i2s_std_config_t std = {};
  std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
  std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;  // codec expects MCLK = 256 x fs
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  std.gpio_cfg.mclk = static_cast<gpio_num_t>(PIN_MCLK);
  std.gpio_cfg.bclk = static_cast<gpio_num_t>(PIN_BCLK);
  std.gpio_cfg.ws = static_cast<gpio_num_t>(PIN_WS);
  std.gpio_cfg.dout = static_cast<gpio_num_t>(PIN_DOUT);
  std.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(tx, &std) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) {
    LOG_ERR("SPK", "I2S init failed");
    i2s_del_channel(tx);
    tx = nullptr;
    end();
    return false;
  }
  running = true;  // the channel is up; end() now tears it down

  delay(50);  // MCLK must be running before the codec is configured
  const int id1 = es8311Read(0xFD), id2 = es8311Read(0xFE);
  if (id1 != 0x83 || id2 != 0x11) {
    LOG_ERR("SPK", "ES8311 not found (id %02X %02X)", id1, id2);
    end();
    return false;
  }
  if (!es8311Start(sampleRate)) {
    LOG_ERR("SPK", "ES8311 init failed");
    end();
    return false;
  }
  digitalWrite(PIN_PA, HIGH);
  rate = sampleRate;
  return true;
}

bool RlcdSpeaker::setSampleRate(uint32_t sampleRate) {
  if (running && sampleRate == rate) return true;
  end();
  return begin(sampleRate);
}

size_t RlcdSpeaker::write(const int16_t* stereo, size_t frames) {
  if (!running || tx == nullptr) return 0;
  size_t written = 0;
  i2s_channel_write(tx, stereo, frames * 2 * sizeof(int16_t), &written, 1000);
  return written / (2 * sizeof(int16_t));
}

void RlcdSpeaker::end() {
  digitalWrite(PIN_PA, LOW);
  if (running) es8311Stop();
  if (tx != nullptr) {
    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    tx = nullptr;
  }
  running = false;
}
