#pragma once

#include <cstdint>

namespace tr::game {

// Small deterministic PRNG. Two lanes seeded alike produce the same target
// sequence, which is how duel mode stays fair.
class Xorshift32 {
 public:
  Xorshift32() = default;
  explicit Xorshift32(uint32_t seed) { reseed(seed); }

  void reseed(uint32_t seed) { state_ = seed == 0 ? 0x9E3779B9u : seed; }

  uint32_t next() {
    uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return x;
  }

  // Uniform-enough integer in [lo, hi].
  uint32_t range(uint32_t lo, uint32_t hi) {
    if (hi <= lo) return lo;
    return lo + next() % (hi - lo + 1);
  }

 private:
  uint32_t state_ = 1;
};

}  // namespace tr::game
