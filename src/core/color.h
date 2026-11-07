/// @file core/color.h
/// @brief RGB colors and simple color math.

#pragma once
#include <cstdint>

namespace dio {

/// An 8-bit-per-channel RGB color.
struct Rgb {
  /// Red, green and blue channels, 0 to 255.
  uint8_t r = 0, g = 0, b = 0;
  /// Exact channel-wise equality.
  friend constexpr bool operator==(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
};

/// Linear blend from a to b.
/// @param t 0 gives a, 1 gives b (expected within 0 to 1).
inline Rgb lerp(Rgb a, Rgb b, double t) {
  auto mix = [t](uint8_t x, uint8_t y) { return static_cast<uint8_t>(x + (y - x) * t + 0.5); };
  return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
}

/// Multiplies every channel by k, saturating at 255.
inline Rgb scale(Rgb c, double k) {
  auto s = [k](uint8_t v) {
    double r = v * k;
    return static_cast<uint8_t>(r > 255 ? 255 : r);
  };
  return {s(c.r), s(c.g), s(c.b)};
}

}  // namespace dio
