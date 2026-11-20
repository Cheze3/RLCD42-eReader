#include "MusicPlayerActivity.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "MusicStore.h"
#include "fontIds.h"
#include "platform/RlcdI2c.h"
#include "util/TextFit.h"

using music::Repeat;

namespace {
constexpr GfxRenderer::Orientation MUSIC_ORIENTATION = GfxRenderer::Orientation::Portrait;
constexpr int MARGIN = 14;
constexpr int HEADER_HEIGHT = 34;
constexpr int NAME_BUFFER_BYTES = 200;
constexpr int32_t SEEK_STEP_MS = 15000;
constexpr uint8_t VOLUME_STEP = 10;
constexpr unsigned long REDRAW_EVERY_MS = 5000;
constexpr unsigned long SAVE_EVERY_MS = 60000;
constexpr uint32_t RESTART_THRESHOLD_MS = 3000;  // "previous" restarts the track after this much playing

// "dir/name.mp3" -> "name"
void baseName(const std::string& path, char* out, size_t cap) {
  const size_t slash = path.rfind('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.rfind('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  snprintf(out, cap, "%s", name.c_str());
}

void formatTime(uint32_t ms, char* out, size_t cap) {
  const uint32_t total = ms / 1000;
  if (total >= 3600) {
    snprintf(out, cap, "%u:%02u:%02u", static_cast<unsigned>(total / 3600), static_cast<unsigned>(total / 60 % 60),
             static_cast<unsigned>(total % 60));
  } else {
    snprintf(out, cap, "%u:%02u", static_cast<unsigned>(total / 60), static_cast<unsigned>(total % 60));
  }
}
}  // namespace

MusicPlayerActivity::MusicPlayerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("MusicPlayer", renderer, mappedInput) {}

MusicPlayerActivity::~MusicPlayerActivity() = default;

void MusicPlayerActivity::onEnter() {
  Activity::onEnter();
  savedOrientation = renderer.getOrientation();
  renderer.setOrientation(MUSIC_ORIENTATION);

  MUSIC_STORE.loadFromFile();
  volume = MUSIC_STORE.volume;
  shuffle = MUSIC_STORE.shuffle != 0;
  rng = music::Rng(esp_random());

  rlcd::ensureI2c();  // start the shared bus here, on the activity task, before the worker uses it
  speaker = makeUniqueNoThrow<RlcdSpeaker>();
  player = makeUniqueNoThrow<Mp3Player>();
  audioOk = speaker && player && player->begin(*speaker);
  if (audioOk) player->setVolume(volume);

  std::string start = MUSIC_STORE.lastDir;
  if (start.empty() || !Storage.exists(start.c_str())) start = Storage.exists("/Music") ? "/Music" : "/";
  openDirectory(start);
  // Put the cursor on the track played last time, if it is in this folder.
  const size_t slash = MUSIC_STORE.lastFile.rfind('/');
  if (slash != std::string::npos && MUSIC_STORE.lastFile.compare(0, slash == 0 ? 1 : slash, dir) == 0) {
    const std::string last = MUSIC_STORE.lastFile.substr(slash + 1);
    const auto it = std::find(names.begin(), names.end(), last);
    if (it != names.end()) selected = static_cast<size_t>(it - names.begin());
  }
  lastSaveMs = millis();
  requestUpdate();
}

void MusicPlayerActivity::onExit() {
  saveResume();
  if (player && !player->end()) {
    // The worker is stuck (see Mp3Player::end): it still uses the player and the speaker, so leak both.
    (void)player.release();
    (void)speaker.release();
  }
  player.reset();  // the player goes first: it holds a pointer to the speaker
  speaker.reset();
  renderer.setOrientation(savedOrientation);
  Activity::onExit();
}

// ----- Library ---------------------------------------------------------------------------------

std::string MusicPlayerActivity::pathFor(const std::string& name) const {
  std::string path = dir == "/" ? std::string("/") : dir + "/";
  path += name;
  if (!path.empty() && path.back() == '/') path.pop_back();
  return path;
}

void MusicPlayerActivity::openDirectory(const std::string& path) {
  names.clear();
  selected = 0;
  dir = path;
  auto root = Storage.open(path.c_str());
  if (!root || !root.isDirectory()) {
    if (path != "/") openDirectory("/");
    return;
  }
  auto nameBuf = makeUniqueNoThrow<char[]>(NAME_BUFFER_BYTES);
  if (!nameBuf) return;

  root.rewindDirectory();
  size_t entryCount = 0;
  for (auto f = root.openNextFile(); f; f = root.openNextFile()) ++entryCount;
  root.rewindDirectory();
  names.reserve(entryCount);

  for (auto f = root.openNextFile(); f; f = root.openNextFile()) {
    const size_t len = f.getName(nameBuf.get(), NAME_BUFFER_BYTES);
    if (len == 0 || len >= NAME_BUFFER_BYTES - 1 || nameBuf[0] == '.') continue;  // hidden, or too long to open
    if (strcmp(nameBuf.get(), "System Volume Information") == 0) continue;
    if (f.isDirectory()) {
      names.emplace_back(std::string(nameBuf.get()) + "/");
    } else if (FsHelpers::checkFileExtension(std::string_view{nameBuf.get(), len}, ".mp3")) {
      names.emplace_back(nameBuf.get());
    }
  }
  root.close();
  FsHelpers::sortFileList(names);
}

bool MusicPlayerActivity::hasNowPlayingRow() const {
  return player && player->state() != Mp3Player::State::Idle && !playlist.empty();
}

size_t MusicPlayerActivity::rowCount() const { return names.size() + (hasNowPlayingRow() ? 1 : 0); }

void MusicPlayerActivity::goUp() {
  if (dir == "/") {
    onGoHome();
    return;
  }
  const size_t slash = dir.rfind('/');
  const std::string child = dir.substr(slash + 1) + "/";
  openDirectory(slash == 0 ? "/" : dir.substr(0, slash));
  const auto it = std::find(names.begin(), names.end(), child);
  // Rows are shifted by one while the "Now playing" row is shown above the folder's entries.
  if (it != names.end()) selected = static_cast<size_t>(it - names.begin()) + (hasNowPlayingRow() ? 1 : 0);
  requestUpdate();
}

void MusicPlayerActivity::activateRow() {
  size_t row = selected;
  if (hasNowPlayingRow()) {
    if (row == 0) {
      screen = Screen::NowPlaying;
      requestUpdate();
      return;
    }
    --row;
  }
  if (row >= names.size()) return;
  if (names[row].back() == '/') {
    openDirectory(pathFor(names[row]));
    MUSIC_STORE.lastDir = dir;
    requestUpdate();
  } else {
    startPlayback(row);
  }
}

void MusicPlayerActivity::startPlayback(size_t nameIndex) {
  if (!audioOk) return;
  playlist.clear();
  size_t start = 0;
  for (size_t i = 0; i < names.size(); ++i) {
    if (names[i].back() == '/') continue;
    if (i == nameIndex) start = playlist.size();
    playlist.push_back(pathFor(names[i]));
  }
  if (playlist.empty()) return;
  queue.reset(playlist.size(), start, shuffle, rng);
  MUSIC_STORE.lastDir = dir;
  failuresInRow = 0;
  screen = Screen::NowPlaying;  // a user choice; automatic advances leave the screen alone
  // Coming back to the track left mid-way resumes it; anything else starts from the top.
  const std::string& path = playlist[queue.current()];
  const uint32_t startMs =
      (path == MUSIC_STORE.lastFile && MUSIC_STORE.positionSec >= 3) ? MUSIC_STORE.positionSec * 1000 : 0;
  playCurrent(startMs);
}

// ----- Playback --------------------------------------------------------------------------------

void MusicPlayerActivity::playCurrent(uint32_t startMs) {
  if (!player || playlist.empty()) return;
  player->play(playlist[queue.current()], startMs);
  MUSIC_STORE.lastFile = playlist[queue.current()];
  MUSIC_STORE.positionSec = startMs / 1000;
  requestUpdate();
}

void MusicPlayerActivity::advance(bool automatic) {
  if (!queue.next(repeat, automatic)) {
    player->stop();
    MUSIC_STORE.positionSec = 0;
    MUSIC_STORE.saveToFile();
    requestUpdate();
    return;
  }
  playCurrent(0);
}

void MusicPlayerActivity::previousTrack() {
  if (player->positionMs() > RESTART_THRESHOLD_MS) {
    playCurrent(0);
    return;
  }
  queue.previous();
  playCurrent(0);
}

void MusicPlayerActivity::saveResume() {
  if (!player || playlist.empty()) return;
  const auto state = player->state();
  const uint32_t sec =
      (state == Mp3Player::State::Playing || state == Mp3Player::State::Paused) ? player->positionMs() / 1000 : 0;
  const bool changed = MUSIC_STORE.positionSec != sec || MUSIC_STORE.volume != volume ||
                       MUSIC_STORE.shuffle != (shuffle ? 1 : 0) || MUSIC_STORE.repeat != static_cast<uint8_t>(repeat);
  MUSIC_STORE.positionSec = sec;
  MUSIC_STORE.volume = volume;
  MUSIC_STORE.shuffle = shuffle ? 1 : 0;
  MUSIC_STORE.repeat = static_cast<uint8_t>(repeat);
  if (changed) MUSIC_STORE.saveToFile();
  lastSaveMs = millis();
}

void MusicPlayerActivity::handleTrackEnd() {
  const auto state = player->state();
  if (state == Mp3Player::State::Failed) {
    // Skip an unreadable file, but stop if every track in the list fails.
    if (++failuresInRow >= playlist.size()) {
      player->stop();
      requestUpdate();
      return;
    }
  } else {
    failuresInRow = 0;
    MUSIC_STORE.positionSec = 0;
  }
  advance(true);
}

void MusicPlayerActivity::togglePause() {
  if (!player) return;
  const auto state = player->state();
  if (state == Mp3Player::State::Playing) {
    player->pause();
    saveResume();
  } else if (state == Mp3Player::State::Paused) {
    player->resume();
  } else if (!playlist.empty()) {
    playCurrent(0);
  }
  requestUpdate();
}

void MusicPlayerActivity::activate(Action action) {
  switch (action) {
    case Action::Previous:
      if (!playlist.empty()) previousTrack();
      break;
    case Action::Next:
      if (!playlist.empty()) advance(false);
      break;
    case Action::SeekBack:
      player->seekBy(-SEEK_STEP_MS);
      break;
    case Action::SeekForward:
      player->seekBy(SEEK_STEP_MS);
      break;
    case Action::VolumeDown:
      volume = volume > VOLUME_STEP ? volume - VOLUME_STEP : 0;
      player->setVolume(volume);
      break;
    case Action::VolumeUp:
      volume = std::min<uint8_t>(100, volume + VOLUME_STEP);
      player->setVolume(volume);
      break;
    case Action::Shuffle:
      shuffle = !shuffle;
      queue.setShuffle(shuffle, rng);
      break;
    case Action::Library:
      screen = Screen::Library;
      break;
    case Action::COUNT:
      break;
  }
  requestUpdate();
}

// ----- Input -----------------------------------------------------------------------------------

// BOOT short = Up, KEY short = Down, hold BOOT = Back, hold KEY = Confirm.
void MusicPlayerActivity::loop() {
  if (player) {
    // Read the completion count first: the worker bumps it after setting the state, so a state read
    // afterwards is never older than the count.
    const uint32_t done = player->openCompleted();
    const auto state = player->state();
    // An Ended/Failed state is final only when no open is pending; act on each result once.
    if ((state == Mp3Player::State::Ended || state == Mp3Player::State::Failed) && !playlist.empty() &&
        done == player->openRequests() && done != handledOpen) {
      handledOpen = done;
      handleTrackEnd();
    }
    const unsigned long now = millis();
    if (screen == Screen::NowPlaying &&
        (player->generation() != shownGeneration ||
         (state == Mp3Player::State::Playing && now - lastRedrawMs >= REDRAW_EVERY_MS))) {
      requestUpdate();
    }
    if (state == Mp3Player::State::Playing && now - lastSaveMs >= SAVE_EVERY_MS) saveResume();
  }
  if (screen == Screen::Library) {
    loopLibrary();
  } else {
    loopNowPlaying();
  }
}

void MusicPlayerActivity::loopLibrary() {
  using Button = MappedInputManager::Button;
  const size_t rows = rowCount();
  // The row count changes when playback starts or stops, so keep the cursor on a real row.
  if (rows > 0 && selected >= rows) {
    selected = rows - 1;
    requestUpdate();
  }
  if (mappedInput.wasReleased(Button::Back)) {
    goUp();
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    if (rows > 0) selected = (selected + rows - 1) % rows;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Down)) {
    if (rows > 0) selected = (selected + 1) % rows;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Up)) {
    activateRow();
  }
}

void MusicPlayerActivity::loopNowPlaying() {
  using Button = MappedInputManager::Button;
  constexpr uint8_t kActions = static_cast<uint8_t>(Action::COUNT);
  if (mappedInput.wasReleased(Button::Back)) {
    togglePause();
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    activate(Action::Next);
  } else if (mappedInput.wasReleased(Button::Down)) {
    cursor = static_cast<Action>((static_cast<uint8_t>(cursor) + 1) % kActions);
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Up)) {
    activate(cursor);
  }
}

// ----- Drawing ---------------------------------------------------------------------------------

// Copies `text` into `out`, cut with "..." (on a UTF-8 boundary) until it fits `maxWidth`. Returns its width.
int MusicPlayerActivity::fitText(int fontId, EpdFontFamily::Style style, const char* text, int maxWidth, char* out,
                                 size_t cap) const {
  return textfit::fit(renderer, fontId, style, text, maxWidth, out, cap);
}

void MusicPlayerActivity::drawFitted(int fontId, int x, int y, int maxWidth, const char* text, bool black,
                                     EpdFontFamily::Style style) const {
  char buf[132];
  fitText(fontId, style, text, maxWidth, buf, sizeof(buf));
  renderer.drawText(fontId, x, y, buf, black, style);
}

void MusicPlayerActivity::drawCenteredFitted(int fontId, int y, int maxWidth, const char* text,
                                             EpdFontFamily::Style style) const {
  char buf[132];
  const int width = fitText(fontId, style, text, maxWidth, buf, sizeof(buf));
  renderer.drawText(fontId, (renderer.getScreenWidth() - width) / 2, y, buf, true, style);
}

void MusicPlayerActivity::drawHints(const char* line1, const char* line2) const {
  const int h = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int maxWidth = renderer.getScreenWidth() - 2 * MARGIN;
  drawCenteredFitted(SMALL_FONT_ID, h - 2 * lineHeight - 6, maxWidth, line1, EpdFontFamily::REGULAR);
  drawCenteredFitted(SMALL_FONT_ID, h - lineHeight - 4, maxWidth, line2, EpdFontFamily::REGULAR);
}

void MusicPlayerActivity::renderLibrary() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  // The folder and track names below can be Korean, and only the regular UI fonts have Korean glyphs.
  drawFitted(UI_10_FONT_ID, MARGIN, 8, w - 2 * MARGIN, dir.c_str(), true, EpdFontFamily::REGULAR);
  renderer.drawLine(MARGIN, HEADER_HEIGHT - 4, w - MARGIN, HEADER_HEIGHT - 4, true);

  const int rowHeight = renderer.getLineHeight(UI_10_FONT_ID) + 10;
  const int hintsHeight = 2 * renderer.getLineHeight(SMALL_FONT_ID) + 12;
  const size_t visible = static_cast<size_t>(std::max(1, (h - HEADER_HEIGHT - hintsHeight) / rowHeight));
  const size_t rows = rowCount();
  if (rows == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 - 10,
                              audioOk ? "No MP3 files or folders here" : "Audio output not available", true);
  }
  const size_t top = selected >= visible ? selected - visible + 1 : 0;
  const bool nowRow = hasNowPlayingRow();
  for (size_t i = 0; i < visible && top + i < rows; ++i) {
    const size_t row = top + i;
    const int y = HEADER_HEIGHT + static_cast<int>(i) * rowHeight;
    const bool isSelected = row == selected;
    if (isSelected) renderer.fillRect(MARGIN, y, w - 2 * MARGIN, rowHeight - 2, true);

    char label[NAME_BUFFER_BYTES + 8];
    if (nowRow && row == 0) {
      snprintf(label, sizeof(label), ">> Now playing");
    } else {
      const std::string& name = names[row - (nowRow ? 1 : 0)];
      if (name.back() == '/') {
        snprintf(label, sizeof(label), "[%.*s]", static_cast<int>(name.size() - 1), name.c_str());
      } else {
        baseName(name, label, sizeof(label));
      }
    }
    drawFitted(UI_10_FONT_ID, MARGIN + 8, y + 4, w - 2 * MARGIN - 16, label, !isSelected);
  }
  if (rows > visible) {
    char pos[24];
    snprintf(pos, sizeof(pos), "%u/%u", static_cast<unsigned>(selected + 1), static_cast<unsigned>(rows));
    renderer.drawText(SMALL_FONT_ID, w - MARGIN - renderer.getTextWidth(SMALL_FONT_ID, pos), 10, pos, true);
  }
  drawHints("KEY: next   BOOT: open / play", "Hold KEY: prev   Hold BOOT: back");
}

const char* MusicPlayerActivity::actionLabel(Action action) const {
  switch (action) {
    case Action::Previous:
      return "Prev";
    case Action::Next:
      return "Next";
    case Action::SeekBack:
      return "-15s";
    case Action::SeekForward:
      return "+15s";
    case Action::VolumeDown:
      return "Vol -";
    case Action::VolumeUp:
      return "Vol +";
    case Action::Shuffle:
      return shuffle ? "Shuf: on" : "Shuf: off";
    case Action::Library:
      return "Library";
    case Action::COUNT:
      break;
  }
  return "";
}

void MusicPlayerActivity::renderNowPlaying() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int inner = w - 2 * MARGIN;
  char buf[96];

  snprintf(buf, sizeof(buf), "%u / %u", static_cast<unsigned>(queue.position() + 1),
           static_cast<unsigned>(queue.size()));
  renderer.drawText(UI_10_FONT_ID, MARGIN, 8, buf, true);
  snprintf(buf, sizeof(buf), "Vol %u%%", static_cast<unsigned>(volume));
  renderer.drawText(UI_10_FONT_ID, w - MARGIN - renderer.getTextWidth(UI_10_FONT_ID, buf), 8, buf, true);

  music::TrackInfo tag;
  const bool haveTag = player && player->trackTag(tag);
  char title[96];
  if (haveTag && tag.title[0] != '\0') {
    snprintf(title, sizeof(title), "%s", tag.title);
  } else if (!playlist.empty()) {
    baseName(playlist[queue.current()], title, sizeof(title));
  } else {
    title[0] = '\0';
  }
  drawCenteredFitted(UI_12_FONT_ID, 44, inner, title, EpdFontFamily::REGULAR);
  if (haveTag && tag.artist[0] != '\0')
    drawCenteredFitted(UI_10_FONT_ID, 78, inner, tag.artist, EpdFontFamily::REGULAR);
  if (haveTag && tag.album[0] != '\0') drawCenteredFitted(SMALL_FONT_ID, 104, inner, tag.album, EpdFontFamily::REGULAR);

  // Progress bar and times.
  const uint32_t pos = player ? player->positionMs() : 0;
  const uint32_t dur = player ? player->durationMs() : 0;
  const int barY = 140;
  renderer.drawRect(MARGIN, barY, inner, 10, 2, true);
  if (dur > 0)
    renderer.fillRect(MARGIN, barY, static_cast<int>(static_cast<uint64_t>(inner) * std::min(pos, dur) / dur), 10,
                      true);
  formatTime(pos, buf, sizeof(buf));
  renderer.drawText(SMALL_FONT_ID, MARGIN, barY + 16, buf, true);
  const auto state = player ? player->state() : Mp3Player::State::Idle;
  if (state == Mp3Player::State::Failed) {
    renderer.drawCenteredText(SMALL_FONT_ID, barY + 16, "Cannot play this file", true);
  } else if (state == Mp3Player::State::Idle || state == Mp3Player::State::Ended) {
    renderer.drawCenteredText(SMALL_FONT_ID, barY + 16, "Stopped", true);
  }
  formatTime(dur, buf, sizeof(buf));
  renderer.drawText(SMALL_FONT_ID, w - MARGIN - renderer.getTextWidth(SMALL_FONT_ID, buf), barY + 16, buf, true);

  // Controls: a grid, two columns on the narrow vertical screen. Cells share the space between the
  // progress bar and the hints; the highlighted one is what BOOT activates.
  const int columns = w >= 360 ? 5 : 2;
  const int rows = (static_cast<int>(Action::COUNT) + columns - 1) / columns;
  const int gap = 8;
  const int gridTop = barY + 50;
  const int hintsTop = h - 2 * renderer.getLineHeight(SMALL_FONT_ID) - 6;
  const int cellH = std::clamp((hintsTop - 12 - gridTop - (rows - 1) * gap) / rows, 28, 56);
  const int cellW = inner / columns;
  for (int i = 0; i < static_cast<int>(Action::COUNT); ++i) {
    const int x = MARGIN + (i % columns) * cellW;
    const int y = gridTop + (i / columns) * (cellH + gap);
    const bool isCursor = static_cast<Action>(i) == cursor;
    const char* label = actionLabel(static_cast<Action>(i));
    if (isCursor) {
      renderer.fillRect(x + 3, y, cellW - 6, cellH, true);
    } else {
      renderer.drawRect(x + 3, y, cellW - 6, cellH, 2, true);
    }
    const int tx = x + (cellW - renderer.getTextWidth(UI_10_FONT_ID, label)) / 2;
    renderer.drawText(UI_10_FONT_ID, tx, y + (cellH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, label, !isCursor);
  }
  drawHints("KEY: next   BOOT: use it", "Hold BOOT: pause   Hold KEY: next");
  if (player) shownGeneration = player->generation();
  lastRedrawMs = millis();
}

void MusicPlayerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (screen == Screen::Library) {
    renderLibrary();
  } else {
    renderNowPlaying();
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
