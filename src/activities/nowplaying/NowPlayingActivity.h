#pragma once

#include <nowplaying/Protocol.h>

#include <memory>
#include <mutex>

#include "activities/Activity.h"

// Shows what the computer is playing (title, artist, album, cover art, progress) and sends
// play/pause, next and previous back to it. A program on the computer feeds it over the USB serial
// port (tools/nowplaying); the line protocol is in lib/NowPlaying. Horizontal screen.
//
// Keys: BOOT play/pause, KEY next, hold KEY previous, hold BOOT exit.
class NowPlayingActivity final : public Activity {
 public:
  NowPlayingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~NowPlayingActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return connected(); }

 private:
  static constexpr unsigned long HOST_TIMEOUT_MS = 6000;  // the computer program sends a state about every 3 s
  static constexpr int ART_SIZE = 200;                    // the cover is square; the computer program is told this size

  static void onSerial(void* self, const char* command);
  void handleLine(const char* line);
  void apply(const nowplaying::Message& m);
  bool connected() const;
  uint32_t displayPositionSec() const;
  void sendEvent(const char* name) const;

  std::mutex lock;  // the serial handler (main task) and render() (render task) share the fields below
  nowplaying::Message message;
  char title[nowplaying::kTextBytes] = "";
  char artist[nowplaying::kTextBytes] = "";
  char album[nowplaying::kTextBytes] = "";
  nowplaying::PlayState playState = nowplaying::PlayState::Stopped;
  uint32_t basePositionSec = 0;
  uint32_t durationSec = 0;
  unsigned long positionStampMs = 0;
  std::unique_ptr<uint8_t[]> art;       // the cover being shown
  std::unique_ptr<uint8_t[]> incoming;  // the cover being received
  uint16_t artWidth = 0, artHeight = 0;
  bool artValid = false;
  uint16_t incomingWidth = 0, incomingHeight = 0;
  uint32_t incomingTotal = 0, incomingNext = 0;
  bool receiving = false;

  volatile unsigned long lastHostMs = 0;
  bool wasConnected = false;
  unsigned long lastRedrawMs = 0;
  GfxRenderer::Orientation savedOrientation = GfxRenderer::Orientation::Portrait;
};
