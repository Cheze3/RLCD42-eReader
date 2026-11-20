#include <gtest/gtest.h>

#include <notequiz/NoteQuiz.h>

using namespace notequiz;

namespace {
uint16_t maskOf(const Round& r) {
  uint16_t m = 0;
  for (int i = 0; i < r.count; ++i) m |= 1u << pitchClass(r.midi[i]);
  return m;
}
}  // namespace

TEST(NoteQuiz, RoundHasRequestedDistinctNotesInBothModes) {
  Rng rng(12345);
  for (const Mode mode : {Mode::Chord, Mode::Random}) {
    for (uint8_t n = 1; n <= kMaxNotes; ++n) {
      for (int i = 0; i < 500; ++i) {
        const Round r = makeRound(rng, n, mode);
        EXPECT_EQ(r.count, n);
        EXPECT_EQ(popCount(r.pitchMask), n) << "pitch classes must not repeat";
        EXPECT_EQ(r.pitchMask, maskOf(r));
        for (int k = 0; k < r.count; ++k) {
          EXPECT_GE(r.midi[k], kRangeLow);
          EXPECT_LE(r.midi[k], kRangeHigh);
        }
      }
    }
  }
}

TEST(NoteQuiz, ChordModeUsesKnownChordShapes) {
  // Every 3-note chord is one of six shapes; normalise by checking the interval set from some rotation.
  Rng rng(7);
  for (int i = 0; i < 500; ++i) {
    const Round r = makeRound(rng, 3, Mode::Chord);
    bool matched = false;
    for (int root = 0; root < kKeys && !matched; ++root) {
      if (!(r.pitchMask & (1u << root))) continue;
      uint16_t rel = 0;
      for (int pc = 0; pc < kKeys; ++pc) {
        if (r.pitchMask & (1u << pc)) rel |= 1u << ((pc - root + kKeys) % kKeys);
      }
      for (const uint16_t shape : {0x091, 0x089, 0x049, 0x111, 0x085, 0x0A1}) matched |= rel == shape;
    }
    EXPECT_TRUE(matched) << "mask " << r.pitchMask;
  }
}

TEST(NoteQuiz, AnswerIgnoresOctave) {
  Round r{};
  r.count = 2;
  r.midi[0] = 84;  // C6
  r.midi[1] = 67;  // G4
  r.pitchMask = (1u << 0) | (1u << 7);
  EXPECT_TRUE(isCorrect(r, (1u << 0) | (1u << 7)));
  EXPECT_FALSE(isCorrect(r, 1u << 0));
  EXPECT_FALSE(isCorrect(r, (1u << 0) | (1u << 6)));
  EXPECT_FALSE(isCorrect(r, (1u << 0) | (1u << 7) | (1u << 9)));
}

TEST(NoteQuiz, ChordModeFallsBackToSingleNoteForOne) {
  Rng rng(99);
  EXPECT_EQ(makeRound(rng, 1, Mode::Chord).count, 1);
}

TEST(NoteQuiz, CountIsClamped) {
  Rng rng(1);
  EXPECT_EQ(makeRound(rng, 0, Mode::Random).count, 1);
  EXPECT_EQ(makeRound(rng, 9, Mode::Chord).count, kMaxNotes);
}

TEST(NoteQuiz, NotesAndFrequencies) {
  EXPECT_NEAR(midiToHz(69), 440.0f, 0.01f);
  EXPECT_NEAR(midiToHz(60), 261.63f, 0.05f);
  EXPECT_STREQ(noteName(0), "C");
  EXPECT_STREQ(noteName(11), "B");
  EXPECT_TRUE(isBlackKey(1));
  EXPECT_FALSE(isBlackKey(4));
}
