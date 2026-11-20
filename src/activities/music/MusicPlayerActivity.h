#pragma once

#include <music/PlayQueue.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "audio/Mp3Player.h"
#include "audio/RlcdSpeaker.h"

// MP3 player for the RLCD-4.2 board: browse the SD card by folder, play a folder as a queue, and
// control it with the two board keys. Landscape. Decoding runs in Mp3Player's own task.
//
// Keys (BOOT = Up/Back, KEY = Down/Confirm; "hold" = about 0.4 s):
//   Library:     KEY next row, hold KEY previous row, BOOT open/play, hold BOOT up a folder
//   Now playing: KEY next control, BOOT use it, hold BOOT pause/resume, hold KEY next track
class MusicPlayerActivity final : public Activity {
 public:
  MusicPlayerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~MusicPlayerActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return player && player->playing(); }

 private:
  enum class Screen : uint8_t { Library, NowPlaying };
  // The on-screen controls, in grid order. Play/pause has no button: it is hold BOOT.
  enum class Action : uint8_t { Previous, Next, SeekBack, SeekForward, VolumeDown, VolumeUp, Shuffle, Library, COUNT };

  // Library
  void openDirectory(const std::string& path);
  std::string pathFor(const std::string& name) const;
  size_t rowCount() const;
  bool hasNowPlayingRow() const;
  void activateRow();
  void goUp();
  void startPlayback(size_t nameIndex);
  // Playback
  void playCurrent(uint32_t startMs);
  void advance(bool automatic);
  void previousTrack();
  void togglePause();
  void activate(Action action);
  void saveResume();
  void handleTrackEnd();
  // Input and drawing
  void loopLibrary();
  void loopNowPlaying();
  void renderLibrary();
  void renderNowPlaying();
  void drawFitted(int fontId, int x, int y, int maxWidth, const char* text, bool black = true,
                  EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawCenteredFitted(int fontId, int y, int maxWidth, const char* text, EpdFontFamily::Style style) const;
  int fitText(int fontId, EpdFontFamily::Style style, const char* text, int maxWidth, char* out, size_t cap) const;
  void drawHints(const char* line1, const char* line2) const;
  const char* actionLabel(Action action) const;

  // Heap-allocated so it can outlive this activity if the player task cannot be stopped (see onExit).
  std::unique_ptr<RlcdSpeaker> speaker;
  std::unique_ptr<Mp3Player> player;
  bool audioOk = false;

  Screen screen = Screen::Library;
  std::string dir = "/";
  std::vector<std::string> names;  // current folder: folders end with '/', files are .mp3 only
  size_t selected = 0;

  std::vector<std::string> playlist;  // full paths of the tracks being played
  music::PlayQueue queue;
  music::Rng rng{1};
  music::Repeat repeat = music::Repeat::Off;  // no control for it: a queue plays once through
  bool shuffle = false;
  uint8_t volume = 60;
  Action cursor = Action::Next;

  uint32_t handledOpen = UINT32_MAX;  // the open whose Ended/Failed result was already acted on
  size_t failuresInRow = 0;
  uint32_t shownGeneration = 0;
  unsigned long lastRedrawMs = 0;
  unsigned long lastSaveMs = 0;
  GfxRenderer::Orientation savedOrientation = GfxRenderer::Orientation::Portrait;
};
