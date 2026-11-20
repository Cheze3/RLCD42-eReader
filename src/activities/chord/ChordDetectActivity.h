#pragma once

#include <ESP_I2S.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <memory>

#include "activities/Activity.h"

namespace chord {
class Pipeline;
class Canvas;
struct Scene;
}  // namespace chord

// Listens to the board microphones (ES7210 ADC) and shows the detected notes and chord on the
// wheel UI. Landscape; the audio task runs on core 0 and feeds the detector, the activity loop
// redraws at a capped rate.
class ChordDetectActivity final : public Activity {
 public:
  ChordDetectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~ChordDetectActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  static void audioTaskEntry(void* self);
  void audioTask();
  bool startMic();
  void stopMic();

  I2SClass i2s;
  std::unique_ptr<chord::Pipeline> pipeline;
  std::unique_ptr<chord::Canvas> canvas;
  std::unique_ptr<chord::Scene> snapshot;
  SemaphoreHandle_t lock = nullptr;
  TaskHandle_t task = nullptr;
  volatile bool stopRequested = false;
  volatile bool taskDone = true;
  volatile uint32_t framesAnalyzed = 0;
  uint32_t framesDrawn = 0;
  unsigned long lastRequestMs = 0;
  bool micOk = false;
  GfxRenderer::Orientation savedOrientation = GfxRenderer::Orientation::Portrait;
};
