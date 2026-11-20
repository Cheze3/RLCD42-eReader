#pragma once

#include <thermo/Thermo.h>

#include "activities/Activity.h"

// Temperature and humidity meter using the board's SHTC3. A measurement takes 10-30 s: it samples
// twice a second and stops as soon as the last 10 s have settled, because averaging beats one
// reading and the board's own heat makes the first seconds drift. Vertical screen.
//
// Keys: BOOT or KEY (short) measure again, hold BOOT to exit.
class ThermometerActivity final : public Activity {
 public:
  ThermometerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Measuring; }

 private:
  enum class State : uint8_t { Measuring, Result, NoSensor };

  bool probe();
  bool sample(float& tempC, float& rhPercent);
  void startMeasuring();
  void finish(bool settled);
  void renderMeasuring();
  void renderResult();
  void drawBigNumber(const char* text, int centerX, int topY, int maxWidth, int& bottomY, int& rightX) const;

  State state = State::Measuring;
  thermo::Averager averager;
  unsigned long startedMs = 0;
  unsigned long lastSampleMs = 0;
  unsigned long lastRedrawMs = 0;
  int failures = 0;
  // Final values for the result screen.
  float resultTemp = 0;
  float resultRh = 0;
  float resultDew = 0;
  float resultSpread = 0;
  uint32_t resultSeconds = 0;
  bool resultSettled = false;
  GfxRenderer::Orientation savedOrientation = GfxRenderer::Orientation::Portrait;
};
