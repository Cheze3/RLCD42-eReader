#pragma once

#include <cstdint>

// Pure quiz logic for the note ear-training activity: round generation and answer checking.
// No hardware or heap use, so it also builds in the host tests.
namespace notequiz {

enum class Mode : uint8_t { Chord, Random };

constexpr int kMaxNotes = 4;
constexpr int kKeys = 12;  // one octave, C..B

// Every played note lies in this MIDI range (D4..G5, about 294-784 Hz): the small speaker distorts
// outside it. It is wider than an octave, so every pitch class still appears in two octaves.
constexpr uint8_t kRangeLow = 62;
constexpr uint8_t kRangeHigh = 79;

// Small deterministic generator (xorshift32); the caller seeds it from the hardware RNG.
class Rng {
 public:
  explicit Rng(uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}
  uint32_t next();
  uint32_t below(uint32_t n) { return next() % n; }

 private:
  uint32_t state;
};

struct Round {
  uint8_t midi[kMaxNotes];  // notes to play, in MIDI numbers
  uint8_t count;            // 1..kMaxNotes
  uint16_t pitchMask;       // bit n set = pitch class n (0 = C) is part of the answer
};

constexpr uint8_t pitchClass(uint8_t midi) { return midi % kKeys; }

// Builds a round of `count` notes (clamped to 1..kMaxNotes). Chord mode picks a random chord type
// with `count` notes, a random root and a random inversion; Random mode picks distinct pitch
// classes in random octaves. Pitch classes never repeat within a round.
Round makeRound(Rng& rng, uint8_t count, Mode mode);

// Octave does not matter: the answer is right when the chosen keys are exactly the round's pitch classes.
inline bool isCorrect(const Round& round, uint16_t answerMask) { return answerMask == round.pitchMask; }

int popCount(uint16_t mask);
float midiToHz(uint8_t midi);
const char* noteName(uint8_t pitchClassIndex);  // "C", "C#", ... "B"
bool isBlackKey(uint8_t pitchClassIndex);

}  // namespace notequiz
