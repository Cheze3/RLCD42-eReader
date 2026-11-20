#include "ChordDetectActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Wire.h>
#include <chord/canvas.h>
#include <chord/pipeline.h>
#include <chord/ui.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "fontIds.h"
#include "platform/RlcdI2c.h"

// Waveshare ESP32-S3-RLCD-4.2 wiring (from the schematic): the two microphones feed an ES7210 ADC
// that shares the I2S bus with the ES8311 speaker codec.
namespace {
constexpr int PIN_MCLK = 16, PIN_BCLK = 9, PIN_WS = 45, PIN_DIN = 10, PIN_DOUT = 8;
constexpr int PIN_PA = 46;  // speaker amplifier enable, kept low
constexpr uint8_t ES7210_ADDR = 0x40;
constexpr int SAMPLE_RATE = chord::kSampleRate;
constexpr int GAIN_DB = 30;  // 0..36 in 3 dB steps
constexpr float SILENCE_RMS = 0.004f;
constexpr float DIGITAL_GAIN = 1.0f;
constexpr unsigned long MIN_REDRAW_MS = 200;
// Native panel orientation. Use LandscapeClockwise instead if the screen shows upside down
// with the device's physical buttons at the top.
constexpr GfxRenderer::Orientation CHORD_ORIENTATION = GfxRenderer::Orientation::LandscapeCounterClockwise;

bool es7210Write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES7210_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

int es7210Read(uint8_t reg) {
  Wire.beginTransmission(ES7210_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(static_cast<int>(ES7210_ADDR), 1) != 1) return -1;
  return Wire.read();
}

// 16 kHz, MCLK = 256 * fs, I2S 16-bit, MIC1 + MIC2 (register sequence checked on the board).
bool es7210Start() {
  bool ok = true;
  ok &= es7210Write(0x00, 0xFF);
  delay(10);
  ok &= es7210Write(0x00, 0x41);
  ok &= es7210Write(0x01, 0x3F);
  ok &= es7210Write(0x09, 0x30);
  ok &= es7210Write(0x0A, 0x30);
  ok &= es7210Write(0x23, 0x2A);
  ok &= es7210Write(0x22, 0x0A);
  ok &= es7210Write(0x20, 0x0A);
  ok &= es7210Write(0x21, 0x2A);
  ok &= es7210Write(0x40, 0x43);
  ok &= es7210Write(0x41, 0x70);
  ok &= es7210Write(0x42, 0x70);
  ok &= es7210Write(0x07, 0x20);
  ok &= es7210Write(0x02, 0xC1);
  ok &= es7210Write(0x04, 0x01);
  ok &= es7210Write(0x05, 0x00);
  ok &= es7210Write(0x06, 0x04);
  ok &= es7210Write(0x11, 0x60);
  ok &= es7210Write(0x12, 0x00);
  const uint8_t gain = 0x10 | static_cast<uint8_t>(std::min(GAIN_DB / 3, 12));
  ok &= es7210Write(0x43, gain);
  ok &= es7210Write(0x44, gain);
  ok &= es7210Write(0x45, 0x00);
  ok &= es7210Write(0x46, 0x00);
  ok &= es7210Write(0x47, 0x08);
  ok &= es7210Write(0x48, 0x08);
  ok &= es7210Write(0x49, 0xFF);
  ok &= es7210Write(0x4A, 0xFF);
  ok &= es7210Write(0x4B, 0x00);
  ok &= es7210Write(0x4C, 0xFF);
  ok &= es7210Write(0x01, 0x34);
  ok &= es7210Write(0x06, 0x00);
  ok &= es7210Write(0x00, 0x71);
  ok &= es7210Write(0x00, 0x41);
  return ok;
}

void es7210Stop() {
  es7210Write(0x4B, 0xFF);  // mic 1/2 power down
  es7210Write(0x06, 0x07);  // chip power down
  es7210Write(0x01, 0x3F);  // clocks off
}
}  // namespace

ChordDetectActivity::ChordDetectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("ChordDetect", renderer, mappedInput) {}

ChordDetectActivity::~ChordDetectActivity() = default;

bool ChordDetectActivity::startMic() {
  pinMode(PIN_PA, OUTPUT);
  digitalWrite(PIN_PA, LOW);

  rlcd::ensureI2c();

  i2s.setPins(PIN_BCLK, PIN_WS, PIN_DOUT, PIN_DIN, PIN_MCLK);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    LOG_ERR("CHORD", "I2S begin failed");
    return false;
  }
  delay(50);  // MCLK must be running before the codec is configured
  const int id1 = es7210Read(0x3D), id2 = es7210Read(0x3E);
  if (id1 != 0x72 || id2 != 0x10) {
    LOG_ERR("CHORD", "ES7210 not found (id %02X %02X)", id1, id2);
    i2s.end();
    return false;
  }
  if (!es7210Start()) {
    LOG_ERR("CHORD", "ES7210 init failed");
    i2s.end();
    return false;
  }
  return true;
}

void ChordDetectActivity::stopMic() {
  if (micOk) {
    es7210Stop();
    i2s.end();
    micOk = false;
  }
}

void ChordDetectActivity::audioTaskEntry(void* self) {
  static_cast<ChordDetectActivity*>(self)->audioTask();
  vTaskDelete(nullptr);
}

void ChordDetectActivity::audioTask() {
  constexpr int HOP = chord::kHop;
  auto* raw = static_cast<int16_t*>(malloc(sizeof(int16_t) * 2 * HOP));
  auto* mono = static_cast<float*>(malloc(sizeof(float) * HOP));
  if (raw && mono) {
    while (!stopRequested) {
      const size_t got = i2s.readBytes(reinterpret_cast<char*>(raw), sizeof(int16_t) * 2 * HOP);
      if (got < sizeof(int16_t) * 2 * HOP) continue;
      for (int i = 0; i < HOP; ++i) {
        const float v = DIGITAL_GAIN * 0.5f * (raw[2 * i] + raw[2 * i + 1]) / 32768.0f;
        mono[i] = std::max(-1.0f, std::min(1.0f, v));
      }
      if (xSemaphoreTake(lock, portMAX_DELAY) == pdTRUE) {
        pipeline->feed(mono, HOP);
        xSemaphoreGive(lock);
      }
      framesAnalyzed = framesAnalyzed + 1;
    }
  }
  free(raw);
  free(mono);
  taskDone = true;
}

void ChordDetectActivity::onEnter() {
  Activity::onEnter();
  savedOrientation = renderer.getOrientation();
  renderer.setOrientation(CHORD_ORIENTATION);

  pipeline = makeUniqueNoThrow<chord::Pipeline>();
  canvas = makeUniqueNoThrow<chord::Canvas>();
  snapshot = makeUniqueNoThrow<chord::Scene>();
  lock = xSemaphoreCreateMutex();
  if (!pipeline || !canvas || !snapshot || !lock) {
    LOG_ERR("CHORD", "OOM");
    requestUpdate();
    return;
  }
  pipeline->set_silence_rms(SILENCE_RMS);

  micOk = startMic();
  if (micOk) {
    stopRequested = false;
    taskDone = false;
    xTaskCreatePinnedToCore(&ChordDetectActivity::audioTaskEntry, "chord_audio", 8192, this, 2, &task, 0);
  }
  requestUpdate();
}

void ChordDetectActivity::onExit() {
  stopRequested = true;
  for (int i = 0; i < 100 && !taskDone; ++i) delay(10);
  task = nullptr;
  stopMic();
  if (lock) {
    vSemaphoreDelete(lock);
    lock = nullptr;
  }
  pipeline.reset();
  canvas.reset();
  snapshot.reset();
  renderer.setOrientation(savedOrientation);
  Activity::onExit();
}

void ChordDetectActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  const unsigned long now = millis();
  if (framesAnalyzed != framesDrawn && now - lastRequestMs >= MIN_REDRAW_MS) {
    lastRequestMs = now;
    requestUpdate();
  }
}

void ChordDetectActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (!pipeline || !canvas || !snapshot) {
    renderer.displayBuffer();
    return;
  }
  framesDrawn = framesAnalyzed;
  if (xSemaphoreTake(lock, portMAX_DELAY) == pdTRUE) {
    pipeline->tick();
    *snapshot = pipeline->scene();
    xSemaphoreGive(lock);
  }
  chord::render(*canvas, chord::Mode::Wheel, *snapshot);

  const int w = std::min(renderer.getScreenWidth(), chord::kScreenW);
  const int h = std::min(renderer.getScreenHeight(), chord::kScreenH);
  const uint8_t* data = canvas->data();
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = data + y * chord::Canvas::kStride;
    for (int bx = 0; bx < (w + 7) / 8; ++bx) {
      const uint8_t b = row[bx];
      if (b == 0xFF) continue;  // all paper
      for (int bit = 0; bit < 8; ++bit) {
        const int x = bx * 8 + bit;
        if (x < w && !(b & (0x80 >> bit))) renderer.drawPixel(x, y, true);
      }
    }
  }
  if (!micOk) {
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 - 8, "Microphone not available", true);
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
