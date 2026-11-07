/// @file core/rng.h
/// @brief Small deterministic random number generator.

#pragma once
#include <cstdint>

namespace dio {

/// xoshiro256** seeded through splitmix64. Deterministic across platforms, which
/// keeps golden-frame tests stable (std::uniform_*_distribution is not portable).
class Rng {
 public:
  /// Seeds the generator. The same seed always produces the same sequence.
  explicit Rng(uint64_t seed = 1) {
    for (auto& v : s_) {
      seed += 0x9E3779B97F4A7C15ull;
      uint64_t z = seed;
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
      v = z ^ (z >> 31);
    }
  }

  /// Next raw 64-bit value.
  uint64_t next() {
    const uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
  }

  /// Uniform value in the range from 0 up to (not including) 1.
  double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
  /// Uniform value from lo up to (not including) hi.
  double range(double lo, double hi) { return lo + (hi - lo) * uniform(); }
  int rangeInt(int lo, int hi) {  ///< inclusive
    if (hi <= lo) return lo;
    return lo + static_cast<int>(uniform() * (hi - lo + 1));
  }
  /// True with probability p (0 to 1).
  bool chance(double p) { return uniform() < p; }

 private:
  /// Rotates x left by k bits.
  static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  /// Generator state.
  uint64_t s_[4];
};

}  // namespace dio
