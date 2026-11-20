#include "ThermometerActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Wire.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "ThermoStore.h"
#include "fontIds.h"
#include "platform/RlcdI2c.h"

namespace {
constexpr uint8_t SHTC3_ADDR = 0x70;
// SHTC3 commands (16-bit, big-endian on the wire).
constexpr uint16_t CMD_WAKE = 0x3517;
constexpr uint16_t CMD_SLEEP = 0xB098;
constexpr uint16_t CMD_MEASURE = 0x7866;  // temperature first, normal power, no clock stretching
constexpr uint16_t CMD_READ_ID = 0xEFC8;

constexpr unsigned long SAMPLE_INTERVAL_MS = 500;
constexpr unsigned long MIN_MEASURE_MS = 10000;
constexpr unsigned long MAX_MEASURE_MS = 30000;
constexpr unsigned long REDRAW_EVERY_MS = 2000;  // progress only; the panel needs no faster updates
constexpr int MAX_FAILURES = 6;                  // consecutive bad reads before giving up

constexpr int MARGIN = 14;
constexpr int BIG_FONT = NOTOSANS_18_FONT_ID;  // scaled up for the large digits
}  // namespace

ThermometerActivity::ThermometerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Thermometer", renderer, mappedInput) {}

namespace {
bool writeCommand(uint16_t cmd) {
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write(static_cast<uint8_t>(cmd >> 8));
  Wire.write(static_cast<uint8_t>(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

bool readBytes(uint8_t* out, size_t n) {
  if (Wire.requestFrom(static_cast<int>(SHTC3_ADDR), static_cast<int>(n)) != static_cast<int>(n)) return false;
  for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(Wire.read());
  return true;
}
}  // namespace

// The shared I2C bus is never stopped (see platform/RlcdI2c.h).
bool ThermometerActivity::probe() {
  uint8_t id[3];
  bool ok = writeCommand(CMD_WAKE);
  delayMicroseconds(400);
  ok = ok && writeCommand(CMD_READ_ID) && readBytes(id, 3) && thermo::isShtc3Id(id);
  writeCommand(CMD_SLEEP);
  return ok;
}

// One reading: wake the sensor, measure, sleep it again. Sleeping between readings keeps the
// sensor's own self-heating negligible.
bool ThermometerActivity::sample(float& tempC, float& rhPercent) {
  uint8_t raw[6];
  bool ok = writeCommand(CMD_WAKE);
  delayMicroseconds(400);
  ok = ok && writeCommand(CMD_MEASURE);
  delay(15);  // normal-power measurement takes up to about 13 ms
  ok = ok && readBytes(raw, 6);
  writeCommand(CMD_SLEEP);
  return ok && thermo::decodeMeasurement(raw, tempC, rhPercent);
}

void ThermometerActivity::onEnter() {
  Activity::onEnter();
  savedOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  THERMO_STORE.loadFromFile();
  rlcd::ensureI2c();
  if (!probe()) {
    LOG_ERR("THERMO", "SHTC3 not found");
    state = State::NoSensor;
    requestUpdate();
    return;
  }
  startMeasuring();
}

void ThermometerActivity::onExit() {
  renderer.setOrientation(savedOrientation);
  Activity::onExit();
}

void ThermometerActivity::startMeasuring() {
  averager.reset();
  failures = 0;
  startedMs = millis();
  lastSampleMs = 0;
  lastRedrawMs = 0;
  state = State::Measuring;
  requestUpdate();
}

void ThermometerActivity::finish(bool settled) {
  const float offset = THERMO_STORE.offsetC;
  const float measuredT = averager.meanTemp();
  resultTemp = measuredT + offset;
  resultRh = thermo::ambientHumidity(averager.meanRh(), measuredT, resultTemp);
  resultDew = thermo::dewPointC(resultTemp, resultRh);
  resultSpread = averager.tempRange();
  resultSeconds = static_cast<uint32_t>((millis() - startedMs) / 1000);
  resultSettled = settled;
  state = State::Result;
  LOG_INF("THERMO", "T %.2f C (raw %.2f, offset %.2f) RH %.1f %% after %u s, settled %d", resultTemp, measuredT, offset,
          resultRh, static_cast<unsigned>(resultSeconds), settled);
  requestUpdate();
}

void ThermometerActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {  // hold BOOT
    onGoHome();
    return;
  }
  if (state != State::Measuring) {
    if (state == State::Result && (mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Down))) {
      startMeasuring();
    }
    return;
  }

  const unsigned long now = millis();
  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    float t, h;
    if (sample(t, h)) {
      failures = 0;
      averager.add(t, h);
    } else if (++failures >= MAX_FAILURES) {
      state = State::NoSensor;
      requestUpdate();
      return;
    }
  }
  const unsigned long elapsed = now - startedMs;
  if (elapsed >= MAX_MEASURE_MS || (elapsed >= MIN_MEASURE_MS && averager.settled())) {
    finish(averager.settled());
    return;
  }
  if (now - lastRedrawMs >= REDRAW_EVERY_MS) {
    lastRedrawMs = now;
    requestUpdate();
  }
}

// ----- Drawing ---------------------------------------------------------------------------------

// Draws `text` (digits, '.', '-') as large scaled glyphs, centered on centerX, with the top of the
// digits at topY. The scale shrinks until the text fits maxWidth. Returns the bottom edge and the
// right edge of what was drawn.
void ThermometerActivity::drawBigNumber(const char* text, int centerX, int topY, int maxWidth, int& bottomY,
                                        int& rightX) const {
  constexpr auto style = EpdFontFamily::BOLD;
  int scale = 768;  // 3x
  int width = 0;
  int digitTop = 0;
  for (; scale >= 256; scale -= 32) {
    int64_t advanceFP = 0;
    for (const char* p = text; *p; ++p) {
      int32_t adv = 0;
      int top = 0;
      if (renderer.getCodepointMetrics(BIG_FONT, static_cast<uint8_t>(*p), style, adv, top)) advanceFP += adv;
    }
    width = static_cast<int>(advanceFP * scale / (256 * 16));
    if (width <= maxWidth) break;
  }
  int32_t adv0 = 0;
  renderer.getCodepointMetrics(BIG_FONT, '0', style, adv0, digitTop);
  const int baseline = topY + digitTop * scale / 256;
  int x = centerX - width / 2;
  for (const char* p = text; *p; ++p)
    x += renderer.drawScaledCodepoint(BIG_FONT, static_cast<uint8_t>(*p), style, x, baseline, scale);
  bottomY = baseline;
  rightX = x;
}

void ThermometerActivity::renderMeasuring() {
  const int w = renderer.getScreenWidth();
  renderer.drawCenteredText(UI_12_FONT_ID, 70, "Measuring...", true, EpdFontFamily::BOLD);
  const unsigned long elapsed = millis() - startedMs;

  char buf[64];
  if (averager.count() > 0) {
    snprintf(buf, sizeof(buf),
             "%.1f \xC2\xB0"
             "C    %.0f %%",
             static_cast<double>(averager.meanTemp()), static_cast<double>(averager.meanRh()));
    renderer.drawCenteredText(UI_12_FONT_ID, 120, buf, true);
    snprintf(buf, sizeof(buf),
             "spread %.2f \xC2\xB0"
             "C",
             static_cast<double>(averager.tempRange()));
    renderer.drawCenteredText(SMALL_FONT_ID, 152, buf, true);
  }

  // Progress toward the 30 s limit; it may finish sooner once the readings settle.
  const int barW = w - 2 * 30;
  renderer.drawRect(30, 200, barW, 14, 2, true);
  renderer.fillRect(30, 200,
                    static_cast<int>(static_cast<uint64_t>(barW) * std::min(elapsed, MAX_MEASURE_MS) / MAX_MEASURE_MS),
                    14, true);
  snprintf(buf, sizeof(buf), "%u s", static_cast<unsigned>(elapsed / 1000));
  renderer.drawCenteredText(UI_10_FONT_ID, 224, buf, true);
  renderer.drawCenteredText(SMALL_FONT_ID, 262, "Stops early when the reading settles", true);
  renderer.drawCenteredText(SMALL_FONT_ID, 280, "Keep the device still", true);
}

void ThermometerActivity::renderResult() {
  const int w = renderer.getScreenWidth();
  const int cx = w / 2;
  char buf[48];
  int bottom = 0, right = 0;

  renderer.drawCenteredText(SMALL_FONT_ID, 34, "Temperature", true);
  snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(resultTemp));
  drawBigNumber(buf, cx - 18, 54, w - 2 * MARGIN - 56, bottom, right);
  renderer.drawText(UI_12_FONT_ID, right + 6, bottom - renderer.getLineHeight(UI_12_FONT_ID) - 4,
                    "\xC2\xB0"
                    "C",
                    true, EpdFontFamily::BOLD);

  const int humidityLabelY = bottom + 30;
  renderer.drawCenteredText(SMALL_FONT_ID, humidityLabelY, "Humidity", true);
  snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(resultRh));
  drawBigNumber(buf, cx - 14, humidityLabelY + 20, w - 2 * MARGIN - 48, bottom, right);
  renderer.drawText(UI_12_FONT_ID, right + 6, bottom - renderer.getLineHeight(UI_12_FONT_ID) - 4, "%", true,
                    EpdFontFamily::BOLD);

  snprintf(buf, sizeof(buf),
           "Dew point %.1f \xC2\xB0"
           "C",
           static_cast<double>(resultDew));
  renderer.drawCenteredText(UI_10_FONT_ID, bottom + 30, buf, true);
  if (resultSettled) {
    snprintf(buf, sizeof(buf), "Settled after %u s", static_cast<unsigned>(resultSeconds));
  } else {
    snprintf(buf, sizeof(buf),
             "Still drifting (%.2f \xC2\xB0"
             "C) - approximate",
             static_cast<double>(resultSpread));
  }
  renderer.drawCenteredText(SMALL_FONT_ID, bottom + 58, buf, true);
  if (THERMO_STORE.offsetC == 0.0f) renderer.drawCenteredText(SMALL_FONT_ID, bottom + 76, "Not calibrated", true);
}

void ThermometerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_10_FONT_ID, 6, "Thermometer", true, EpdFontFamily::BOLD);
  renderer.drawLine(MARGIN, 28, w - MARGIN, 28, true);

  if (state == State::NoSensor) {
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 30, "Sensor not found", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, h / 2, "The SHTC3 did not answer", true);
  } else if (state == State::Measuring) {
    renderMeasuring();
  } else {
    renderResult();
  }

  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const char* hint = state == State::Result ? "BOOT or KEY: measure again" : "";
  if (hint[0] != '\0') renderer.drawCenteredText(SMALL_FONT_ID, h - 2 * lineHeight - 6, hint, true);
  renderer.drawCenteredText(SMALL_FONT_ID, h - lineHeight - 4, "Hold BOOT: exit", true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
