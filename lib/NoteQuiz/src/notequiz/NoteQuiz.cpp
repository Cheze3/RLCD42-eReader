#include "NoteQuiz.h"

#include <cmath>

namespace notequiz {

namespace {

struct ChordType {
  uint8_t size;
  uint8_t intervals[kMaxNotes];
};

// Two notes: minor/major third, fourth, fifth. Three: major, minor, diminished, augmented, sus2, sus4.
// Four: major 7, minor 7, dominant 7, half-diminished, diminished 7.
constexpr ChordType kChords[] = {
    {2, {0, 3, 0, 0}},  {2, {0, 4, 0, 0}},  {2, {0, 5, 0, 0}},   {2, {0, 7, 0, 0}},    {3, {0, 4, 7, 0}},
    {3, {0, 3, 7, 0}},  {3, {0, 3, 6, 0}},  {3, {0, 4, 8, 0}},   {3, {0, 2, 7, 0}},    {3, {0, 5, 7, 0}},
    {4, {0, 4, 7, 11}}, {4, {0, 3, 7, 10}}, {4, {0, 4, 7, 10}},  {4, {0, 3, 6, 10}},   {4, {0, 3, 6, 9}},
};
constexpr int kChordCount = sizeof(kChords) / sizeof(kChords[0]);

uint8_t clampCount(uint8_t count) {
  if (count < 1) return 1;
  return count > kMaxNotes ? kMaxNotes : count;
}

void addNote(Round& round, uint8_t midi) {
  round.midi[round.count++] = midi;
  round.pitchMask |= static_cast<uint16_t>(1u << pitchClass(midi));
}

// Picks the chord's inversion, then places its lowest note so the whole chord (span <= 11
// semitones) fits in the playable range.
void makeChordRound(Rng& rng, Round& round, uint8_t count) {
  const ChordType* matches[kChordCount];
  int found = 0;
  for (const ChordType& chord : kChords) {
    if (chord.size == count) matches[found++] = &chord;
  }
  const ChordType& chord = *matches[rng.below(found)];
  const uint8_t inversion = rng.below(count);
  uint8_t offsets[kMaxNotes];
  uint8_t span = 0;
  for (uint8_t j = 0; j < count; ++j) {
    const uint8_t idx = (inversion + j) % count;
    offsets[j] = chord.intervals[idx] + (idx < inversion ? kKeys : 0) - chord.intervals[inversion];
    span = offsets[j] > span ? offsets[j] : span;
  }
  const uint8_t lowest = kRangeLow + rng.below(kRangeHigh - span - kRangeLow + 1);
  for (uint8_t j = 0; j < count; ++j) addNote(round, lowest + offsets[j]);
}

void makeRandomRound(Rng& rng, Round& round, uint8_t count) {
  while (round.count < count) {
    const uint8_t pc = rng.below(kKeys);
    if (round.pitchMask & (1u << pc)) continue;
    uint8_t midi = kRangeLow + (pc + kKeys - pitchClass(kRangeLow)) % kKeys;
    if (midi + kKeys <= kRangeHigh && rng.below(2)) midi += kKeys;
    addNote(round, midi);
  }
}

}  // namespace

uint32_t Rng::next() {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

Round makeRound(Rng& rng, uint8_t count, Mode mode) {
  count = clampCount(count);
  Round round{};
  if (mode == Mode::Chord && count > 1) {
    makeChordRound(rng, round, count);
  } else {
    makeRandomRound(rng, round, count);
  }
  return round;
}

int popCount(uint16_t mask) {
  int n = 0;
  for (; mask; mask &= mask - 1) ++n;
  return n;
}

float midiToHz(uint8_t midi) { return 440.0f * std::pow(2.0f, (static_cast<int>(midi) - 69) / 12.0f); }

const char* noteName(uint8_t pitchClassIndex) {
  static constexpr const char* kNames[kKeys] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return kNames[pitchClassIndex % kKeys];
}

bool isBlackKey(uint8_t pitchClassIndex) {
  constexpr uint16_t kBlackMask = (1u << 1) | (1u << 3) | (1u << 6) | (1u << 8) | (1u << 10);
  return (kBlackMask >> (pitchClassIndex % kKeys)) & 1u;
}

}  // namespace notequiz
