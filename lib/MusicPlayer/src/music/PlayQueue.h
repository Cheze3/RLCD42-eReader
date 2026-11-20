#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace music {

enum class Repeat : uint8_t { Off, All, One };

// Small deterministic generator (xorshift32) for shuffling; the caller seeds it.
class Rng {
 public:
  explicit Rng(uint32_t seed = 1) : state(seed ? seed : 0x9E3779B9u) {}
  uint32_t below(uint32_t n);

 private:
  uint32_t state;
};

// Order in which a list of `count` tracks is played, with optional shuffle and repeat. Holds
// indices only; the caller owns the track list.
class PlayQueue {
 public:
  // Starts at `startIndex`. With shuffle, the other tracks follow in random order.
  void reset(size_t count, size_t startIndex, bool shuffle, Rng& rng);

  bool empty() const { return order.empty(); }
  size_t size() const { return order.size(); }
  size_t current() const { return order.empty() ? 0 : order[pos]; }
  size_t position() const { return pos; }  // 0-based place in play order

  // Moves to the next track. Returns false when playback should stop: end of list with repeat off
  // and `automatic` (a track finishing by itself). A user "next" at the end wraps to the start.
  bool next(Repeat repeat, bool automatic);
  // Moves to the previous track, staying on the first one at the start of the list.
  void previous();
  // Turns shuffle on or off, keeping the current track current.
  void setShuffle(bool on, Rng& rng);

 private:
  void shuffleAfterCurrent(Rng& rng);
  std::vector<uint16_t> order;
  size_t pos = 0;
};

}  // namespace music
