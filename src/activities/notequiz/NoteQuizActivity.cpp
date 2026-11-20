#include "NoteQuizActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <esp_random.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "NoteQuizStore.h"
#include "audio/RlcdSpeaker.h"
#include "fontIds.h"

using notequiz::kKeys;
using notequiz::Mode;

namespace {
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr float PLAY_SECONDS = 1.2f;
constexpr float FADE_SECONDS = 0.1f;
constexpr float OUTPUT_LEVEL = 0.35f;
// Same native orientation as Chord Detect.
constexpr GfxRenderer::Orientation QUIZ_ORIENTATION = GfxRenderer::Orientation::LandscapeCounterClockwise;

constexpr int MARGIN = 14;
constexpr int KEYBOARD_TOP = 78;
constexpr int KEYBOARD_HEIGHT = 150;
constexpr int BLACK_KEY_WIDTH = 32;
constexpr int BLACK_KEY_HEIGHT = 92;
// White-key slot of each pitch class (-1 = black key) and, for black keys, the white slot to its left.
constexpr int8_t WHITE_SLOT[kKeys] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
constexpr int8_t BLACK_AFTER[kKeys] = {-1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1};

constexpr float TWO_PI_F = 6.28318530718f;
}  // namespace

NoteQuizActivity::NoteQuizActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("NoteQuiz", renderer, mappedInput) {}

NoteQuizActivity::~NoteQuizActivity() = default;

bool NoteQuizActivity::startAudio() { return speaker.begin(SAMPLE_RATE); }

void NoteQuizActivity::stopAudio() {
  speaker.end();
  audioOk = false;
}

void NoteQuizActivity::audioTaskEntry(void* self) {
  static_cast<NoteQuizActivity*>(self)->audioTask();
  vTaskDelete(nullptr);
}

// Streams a decaying sum of sines (fundamental + a soft 2nd harmonic) when a note is requested, and
// silence otherwise. The silence matters: the I2S hardware keeps cycling its buffers, so a note
// written into an idle output can land in a buffer that is already half played, clipping its start.
// Writing continuously keeps the writer in step with the hardware.
void NoteQuizActivity::audioTask() {
  const int totalFrames = static_cast<int>(PLAY_SECONDS * SAMPLE_RATE);
  const int fadeFrames = static_cast<int>(FADE_SECONDS * SAMPLE_RATE);
  while (!stopRequested) {
    if (ulTaskNotifyTake(pdTRUE, 0) == 0) {
      std::fill(pcm, pcm + PLAY_CHUNK_FRAMES * 2, static_cast<int16_t>(0));
      // Paced by the hardware: returns when a buffer frees up. It returns 0 at once if the output has
      // failed, and then this loop must not spin.
      if (speaker.write(pcm, PLAY_CHUNK_FRAMES) == 0) vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    playing = true;
    float phase[notequiz::kMaxNotes] = {};
    float step[notequiz::kMaxNotes];
    for (int n = 0; n < playCount; ++n) step[n] = TWO_PI_F * playFreq[n] / SAMPLE_RATE;
    const float gain = OUTPUT_LEVEL / (playCount * 1.3f);
    int frame = 0;
    while (frame < totalFrames && !stopRequested) {
      const int chunk = std::min(PLAY_CHUNK_FRAMES, totalFrames - frame);
      for (int i = 0; i < chunk; ++i, ++frame) {
        const float t = static_cast<float>(frame) / SAMPLE_RATE;
        float env = std::min(1.0f, t / 0.01f) * std::exp(-t * 1.8f);
        if (frame > totalFrames - fadeFrames) env *= static_cast<float>(totalFrames - frame) / fadeFrames;
        float sum = 0;
        for (int n = 0; n < playCount; ++n) {
          sum += std::sin(phase[n]) + 0.3f * std::sin(2.0f * phase[n]);
          phase[n] += step[n];
          if (phase[n] > TWO_PI_F) phase[n] -= TWO_PI_F;
        }
        const int16_t s = static_cast<int16_t>(sum * env * gain * 32767.0f);
        pcm[2 * i] = s;
        pcm[2 * i + 1] = s;
      }
      speaker.write(pcm, chunk);
    }
    playing = false;
  }
  LOG_INF("QUIZ", "audio task stack high water %u", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  taskDone = true;
}

void NoteQuizActivity::playRound() {
  if (!audioOk || playing || !task) return;
  playCount = round.count;
  for (int n = 0; n < round.count; ++n) playFreq[n] = notequiz::midiToHz(round.midi[n]);
  xTaskNotifyGive(task);
}

void NoteQuizActivity::onEnter() {
  Activity::onEnter();
  savedOrientation = renderer.getOrientation();
  renderer.setOrientation(QUIZ_ORIENTATION);

  NOTE_QUIZ_STORE.loadFromFile();
  notes = NOTE_QUIZ_STORE.lastNotes;
  mode = NOTE_QUIZ_STORE.lastRandomMode ? Mode::Random : Mode::Chord;
  rng = notequiz::Rng(esp_random());
  screen = Screen::Menu;
  menuRow = 0;

  audioOk = startAudio();
  if (audioOk) {
    stopRequested = false;
    taskDone = false;
    xTaskCreatePinnedToCore(&NoteQuizActivity::audioTaskEntry, "quiz_audio", 4096, this, 2, &task, 0);
  }
  requestUpdate();
}

void NoteQuizActivity::onExit() {
  saveBest();
  stopRequested = true;
  for (int i = 0; i < 150 && audioOk && !taskDone; ++i) delay(10);
  task = nullptr;
  stopAudio();
  renderer.setOrientation(savedOrientation);
  Activity::onExit();
}

void NoteQuizActivity::startSession() {
  if (NOTE_QUIZ_STORE.lastNotes != notes || NOTE_QUIZ_STORE.lastRandomMode != (mode == Mode::Random ? 1 : 0)) {
    NOTE_QUIZ_STORE.lastNotes = notes;
    NOTE_QUIZ_STORE.lastRandomMode = mode == Mode::Random ? 1 : 0;
    NOTE_QUIZ_STORE.saveToFile();
  }
  streak = 0;
  lostStreak = 0;
  newBest = false;
  nextRound();
}

void NoteQuizActivity::nextRound() {
  round = notequiz::makeRound(rng, notes, mode);
  picked = 0;
  cursor = 0;
  screen = Screen::Listening;
  playRound();
  requestUpdate();
}

// The game is continuous: a wrong answer ends the streak, not the game. The score is the current
// streak, and the best score is the longest streak (kept per difficulty).
void NoteQuizActivity::submit() {
  lastCorrect = notequiz::isCorrect(round, picked);
  newBest = false;
  if (lastCorrect) {
    ++streak;
    uint16_t& best = NOTE_QUIZ_STORE.best[notes - 1];
    if (streak > best) {
      best = streak;
      newBest = true;
      bestDirty = true;
    }
  } else {
    lostStreak = streak;
    streak = 0;
    saveBest();  // the streak is over, so its length is final
  }
  screen = Screen::Feedback;
  requestUpdate();
}

// Writes a new longest streak to the card. Called when a streak ends, on quitting and on leaving the app.
void NoteQuizActivity::saveBest() {
  if (!bestDirty) return;
  NOTE_QUIZ_STORE.saveToFile();
  bestDirty = false;
}

void NoteQuizActivity::goToMenu() {
  saveBest();
  screen = Screen::Menu;
  requestUpdate();
}

// Two-button board: short BOOT = Up, short KEY = Down, hold BOOT = Back, hold KEY = Confirm.
void NoteQuizActivity::loop() {
  switch (screen) {
    case Screen::Menu:
      loopMenu();
      break;
    case Screen::Listening:
      loopListening();
      break;
    case Screen::Feedback:
      loopFeedback();
      break;
  }
}

void NoteQuizActivity::loopMenu() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    onGoHome();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    startSession();
  } else if (mappedInput.wasReleased(Button::Down)) {
    menuRow = (menuRow + 1) % 3;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Up)) {
    if (menuRow == 0) {
      notes = notes % notequiz::kMaxNotes + 1;
    } else if (menuRow == 1) {
      mode = mode == Mode::Chord ? Mode::Random : Mode::Chord;
    } else {
      startSession();
      return;
    }
    requestUpdate();
  }
}

void NoteQuizActivity::loopListening() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    goToMenu();
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    playRound();
  } else if (mappedInput.wasReleased(Button::Down)) {
    cursor = (cursor + 1) % kKeys;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Up)) {
    picked ^= static_cast<uint16_t>(1u << cursor);
    if (notequiz::popCount(picked) == round.count) {
      submit();
    } else {
      requestUpdate();
    }
  }
}

void NoteQuizActivity::loopFeedback() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    goToMenu();
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    playRound();
  } else if (mappedInput.wasReleased(Button::Down) || mappedInput.wasReleased(Button::Up)) {
    nextRound();
  }
}

void NoteQuizActivity::keyRect(uint8_t pc, int& x, int& y, int& w, int& h) const {
  const int whiteWidth = (renderer.getScreenWidth() - 2 * MARGIN) / 7;
  y = KEYBOARD_TOP;
  if (WHITE_SLOT[pc] >= 0) {
    x = MARGIN + WHITE_SLOT[pc] * whiteWidth;
    w = whiteWidth;
    h = KEYBOARD_HEIGHT;
  } else {
    x = MARGIN + (BLACK_AFTER[pc] + 1) * whiteWidth - BLACK_KEY_WIDTH / 2;
    w = BLACK_KEY_WIDTH;
    h = BLACK_KEY_HEIGHT;
  }
}

void NoteQuizActivity::drawHints(const char* line1, const char* line2) {
  const int h = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawCenteredText(SMALL_FONT_ID, h - 2 * lineHeight - 6, line1, true);
  renderer.drawCenteredText(SMALL_FONT_ID, h - lineHeight - 4, line2, true);
}

void NoteQuizActivity::renderHeader() {
  char buf[32];
  const int w = renderer.getScreenWidth();
  snprintf(buf, sizeof(buf), "Streak %u", static_cast<unsigned>(streak));
  renderer.drawText(UI_12_FONT_ID, MARGIN, 8, buf, true);
  snprintf(buf, sizeof(buf), "Best %u", static_cast<unsigned>(NOTE_QUIZ_STORE.best[notes - 1]));
  renderer.drawText(UI_12_FONT_ID, w - MARGIN - renderer.getTextWidth(UI_12_FONT_ID, buf), 8, buf, true);
}

void NoteQuizActivity::renderKeyboard(bool showAnswer) {
  // White keys first, then black keys on top.
  for (int pass = 0; pass < 2; ++pass) {
    for (uint8_t pc = 0; pc < kKeys; ++pc) {
      const bool black = notequiz::isBlackKey(pc);
      if (black != (pass == 1)) continue;
      int x, y, kw, kh;
      keyRect(pc, x, y, kw, kh);
      if (black) {
        renderer.fillRect(x, y, kw, kh, true);
      } else {
        renderer.drawRect(x, y, kw, kh, 2, true);
      }
      const char* name = notequiz::noteName(pc);
      const int labelX = x + (kw - renderer.getTextWidth(SMALL_FONT_ID, name)) / 2;
      const int labelY = y + kh - renderer.getLineHeight(SMALL_FONT_ID) - 4;
      renderer.drawText(SMALL_FONT_ID, labelX, labelY, name, !black);
    }
  }
  // Markers: filled square = picked; diamond = a correct note (feedback); X = picked but wrong.
  for (uint8_t pc = 0; pc < kKeys; ++pc) {
    int x, y, kw, kh;
    keyRect(pc, x, y, kw, kh);
    const bool black = notequiz::isBlackKey(pc);
    const bool inkOnBlack = black;  // draw white ink on black keys
    const int cx = x + kw / 2;
    const int cy = black ? y + 24 : y + kh - 56;
    const bool isPicked = picked & (1u << pc);
    const bool isAnswer = showAnswer && (round.pitchMask & (1u << pc));
    if (isAnswer) {
      const int px[4] = {cx, cx + 11, cx, cx - 11};
      const int py[4] = {cy - 11, cy, cy + 11, cy};
      renderer.fillPolygon(px, py, 4, !inkOnBlack);
    } else if (isPicked && showAnswer) {
      renderer.drawLine(cx - 8, cy - 8, cx + 8, cy + 8, 3, !inkOnBlack);
      renderer.drawLine(cx - 8, cy + 8, cx + 8, cy - 8, 3, !inkOnBlack);
    } else if (isPicked) {
      renderer.fillRect(cx - 8, cy - 8, 16, 16, !inkOnBlack);
    }
  }
  if (!showAnswer) {
    // Cursor: a triangle under the keyboard plus a frame on the key itself.
    int x, y, kw, kh;
    keyRect(cursor, x, y, kw, kh);
    const bool black = notequiz::isBlackKey(cursor);
    renderer.drawRect(x + 4, y + 4, kw - 8, kh - 8, 3, !black);
    const int cx = x + kw / 2;
    const int baseY = KEYBOARD_TOP + KEYBOARD_HEIGHT + 16;
    const int px[3] = {cx, cx - 10, cx + 10};
    const int py[3] = {baseY - 14, baseY, baseY};
    renderer.fillPolygon(px, py, 3, true);
  }
}

void NoteQuizActivity::renderMenu() {
  const int w = renderer.getScreenWidth();
  renderer.drawCenteredText(UI_12_FONT_ID, 14, "Note Quiz", true, EpdFontFamily::BOLD);
  char rows[3][40];
  snprintf(rows[0], sizeof(rows[0]), "Notes:  %u", static_cast<unsigned>(notes));
  snprintf(rows[1], sizeof(rows[1]), "Notes are:  %s", mode == Mode::Chord ? "a chord" : "random");
  snprintf(rows[2], sizeof(rows[2]), "Start");
  const int rowHeight = renderer.getLineHeight(UI_12_FONT_ID) + 14;
  for (int i = 0; i < 3; ++i) {
    const int y = 70 + i * rowHeight;
    const bool selected = i == menuRow;
    if (selected) renderer.fillRect(MARGIN, y - 4, w - 2 * MARGIN, rowHeight - 4, true);
    renderer.drawText(UI_12_FONT_ID, MARGIN + 12, y + 2, rows[i], !selected);
  }
  char best[40];
  snprintf(best, sizeof(best), "Longest streak (%u note%s): %u", static_cast<unsigned>(notes), notes == 1 ? "" : "s",
           static_cast<unsigned>(NOTE_QUIZ_STORE.best[notes - 1]));
  renderer.drawCenteredText(UI_10_FONT_ID, 70 + 3 * rowHeight + 16, best, true);
  drawHints("KEY: next row    BOOT: change / start", "Hold KEY: start    Hold BOOT: exit");
}

void NoteQuizActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (screen == Screen::Menu) {
    renderMenu();
  } else {
    renderHeader();
    char buf[64];
    if (screen == Screen::Listening) {
      snprintf(buf, sizeof(buf), "Pick %u note%s (%d/%u)", static_cast<unsigned>(round.count),
               round.count == 1 ? "" : "s", notequiz::popCount(picked), static_cast<unsigned>(round.count));
      renderer.drawCenteredText(UI_10_FONT_ID, 42, buf, true);
      renderKeyboard(false);
      drawHints("KEY: next key    BOOT: select", "Hold KEY: replay    Hold BOOT: quit");
    } else {
      char names[40] = "";
      for (uint8_t pc = 0; pc < kKeys; ++pc) {
        if (!(round.pitchMask & (1u << pc))) continue;
        strncat(names, notequiz::noteName(pc), sizeof(names) - strlen(names) - 2);
        strncat(names, " ", sizeof(names) - strlen(names) - 1);
      }
      if (lastCorrect) {
        snprintf(buf, sizeof(buf), newBest ? "Correct!  %s  New best!" : "Correct!  %s", names);
      } else {
        snprintf(buf, sizeof(buf), "Wrong - answer: %s", names);
      }
      renderer.drawCenteredText(UI_10_FONT_ID, 42, buf, true);
      if (!lastCorrect && lostStreak > 0) {
        snprintf(buf, sizeof(buf), "Streak of %u ended", static_cast<unsigned>(lostStreak));
        renderer.drawCenteredText(SMALL_FONT_ID, 60, buf, true);
      }
      renderKeyboard(true);
      drawHints("Any key: next round", "Hold KEY: replay    Hold BOOT: menu");
    }
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
