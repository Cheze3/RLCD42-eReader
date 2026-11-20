#include "PluginSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/PluginEvents.h"

PluginSyncActivity::PluginSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath)
    : Activity("PluginSync", renderer, mappedInput), bookPath(std::move(bookPath)) {}

bool PluginSyncActivity::available() {
  for (size_t event = 0; event < static_cast<size_t>(pluginevents::Event::COUNT); ++event) {
    if (pluginevents::anySubscriber(static_cast<pluginevents::Event>(event))) return true;
  }
  return false;
}

void PluginSyncActivity::returnToReader() { activityManager.goToReader(bookPath); }

void PluginSyncActivity::onEnter() {
  Activity::onEnter();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  wifiActivated = true;

  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void PluginSyncActivity::onExit() {
  Activity::onExit();
  if (wifiActivated) {
    WiFi.disconnect(false);
    delay(30);
    silentRestartToReader();
  }
}

void PluginSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    returnToReader();
    return;
  }
  // Modem sleep can stall requests for seconds; WiFi is torn down when this activity exits.
  WiFi.setSleep(false);
  state = State::Syncing;
  requestUpdate(true);

  pluginevents::drain(&renderer);

  state = State::Done;
  autoReturnAt = millis() + AUTO_RETURN_DELAY_MS;
  requestUpdate(true);
}

void PluginSyncActivity::loop() {
  if (state != State::Done) return;
  if (millis() >= autoReturnAt || mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    returnToReader();
  }
}

void PluginSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), tr(STR_SYNC_PROGRESS));

  const int top = screen.y + screen.height / 2 - 40;
  UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, top,
                            state == State::Done ? tr(STR_ALREADY_SYNCED) : tr(STR_SYNC_PROGRESS), true,
                            EpdFontFamily::BOLD);
  if (state == State::Done) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
