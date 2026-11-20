#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <notequiz/NoteQuiz.h>

#include "activities/Activity.h"
#include "audio/RlcdSpeaker.h"

// Ear-training quiz: the device plays 1-4 notes through the ES8311 codec and the user picks their
// pitch classes on a 12-key piano with the two board keys. Landscape. A worker task on core 0
// synthesizes and streams the audio so the UI loop never blocks on the I2S DMA.
class NoteQuizActivity final : public Activity {
 public:
  NoteQuizActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~NoteQuizActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum class Screen : uint8_t { Menu, Listening, Feedback };
  static constexpr int PLAY_CHUNK_FRAMES = 256;

  static void audioTaskEntry(void* self);
  void audioTask();
  bool startAudio();
  void stopAudio();
  void playRound();

  void startSession();
  void nextRound();
  void submit();
  void saveBest();
  void goToMenu();

  void loopMenu();
  void loopListening();
  void loopFeedback();
  void renderMenu();
  void renderKeyboard(bool showAnswer);
  void renderHeader();
  void drawHints(const char* line1, const char* line2);
  void keyRect(uint8_t pitchClass, int& x, int& y, int& w, int& h) const;

  RlcdSpeaker speaker;
  TaskHandle_t task = nullptr;
  volatile bool stopRequested = false;
  volatile bool taskDone = true;
  volatile bool playing = false;
  bool audioOk = false;
  float playFreq[notequiz::kMaxNotes] = {};
  uint8_t playCount = 0;
  int16_t pcm[PLAY_CHUNK_FRAMES * 2] = {};

  notequiz::Rng rng{1};
  notequiz::Round round{};
  Screen screen = Screen::Menu;
  uint8_t menuRow = 0;
  uint8_t notes = 1;
  notequiz::Mode mode = notequiz::Mode::Chord;
  uint8_t cursor = 0;
  uint16_t picked = 0;
  uint16_t streak = 0;      // consecutive correct answers; a wrong one resets it
  uint16_t lostStreak = 0;  // the streak the last wrong answer ended
  bool bestDirty = false;   // a new longest streak not yet written to the card
  bool lastCorrect = false;
  bool newBest = false;
  GfxRenderer::Orientation savedOrientation = GfxRenderer::Orientation::Portrait;
};
