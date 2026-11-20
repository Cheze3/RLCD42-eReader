#pragma once

#include <string>

#include "activities/Activity.h"

// Reader "Sync" action: joins WiFi, delivers the queued plugin events, then returns to the book.
class PluginSyncActivity final : public Activity {
 public:
  PluginSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath);

  // True when at least one installed plugin subscribes to an event, so Sync has something to do.
  static bool available();

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Syncing; }

 private:
  enum class State : uint8_t { Syncing, Done };
  static constexpr unsigned long AUTO_RETURN_DELAY_MS = 1200;

  void onWifiSelectionComplete(bool success);
  void returnToReader();

  std::string bookPath;
  State state = State::Syncing;
  unsigned long autoReturnAt = 0;
  // Set once WiFi is in use, so onExit knows to tear it down and restart cleanly.
  bool wifiActivated = false;
};
