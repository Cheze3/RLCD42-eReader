#include "PlayQueue.h"

#include <utility>

namespace music {

uint32_t Rng::below(uint32_t n) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state % n;
}

void PlayQueue::shuffleAfterCurrent(Rng& rng) {
  // Fisher-Yates over positions 1..n-1, so the current track stays first.
  for (size_t i = order.size(); i > 2; --i) {
    const size_t j = 1 + rng.below(static_cast<uint32_t>(i - 1));
    std::swap(order[i - 1], order[j]);
  }
}

void PlayQueue::reset(size_t count, size_t startIndex, bool shuffle, Rng& rng) {
  order.clear();
  pos = 0;
  if (count == 0) return;
  if (startIndex >= count) startIndex = 0;
  order.reserve(count);
  for (size_t i = 0; i < count; ++i) order.push_back(static_cast<uint16_t>(i));
  if (shuffle) {
    std::swap(order[0], order[startIndex]);
    shuffleAfterCurrent(rng);
  } else {
    pos = startIndex;
  }
}

bool PlayQueue::next(Repeat repeat, bool automatic) {
  if (order.empty()) return false;
  if (automatic && repeat == Repeat::One) return true;
  if (pos + 1 < order.size()) {
    ++pos;
    return true;
  }
  if (automatic && repeat == Repeat::Off) return false;
  pos = 0;
  return true;
}

void PlayQueue::previous() {
  if (pos > 0) --pos;
}

void PlayQueue::setShuffle(bool on, Rng& rng) {
  if (order.empty()) return;
  const uint16_t cur = order[pos];
  if (on) {
    std::swap(order[0], order[pos]);
    pos = 0;
    shuffleAfterCurrent(rng);
  } else {
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<uint16_t>(i);
    pos = cur;
  }
}

}  // namespace music
