/// @file core/shading.h
/// @brief How the light of the time of day and the weather changes colors, shared by the
/// renderers of the world.

#pragma once
#include <cstdint>

#include "core/color.h"
#include "core/world.h"

namespace dio {

/// Backdrop color at the given light: fades towards a dark blue at night and towards the haze
/// color in a sandstorm.
inline Rgb backdrop(Rgb c, const Light& l) {
  Rgb out = c;
  if (l.day < 1.0) out = lerp(lerp(c, {6, 9, 30}, 0.82), c, l.day);
  return l.haze > 0 ? lerp(out, l.hazeColor, l.haze) : out;
}

/// Color of a thing in the world at the given light: darker and bluer at night, dusty in a
/// sandstorm (less so than the backdrop, so animals stay visible).
inline Rgb shade(Rgb c, const Light& l) {
  Rgb out = c;
  if (l.day < 1.0) {
    const Rgb night{static_cast<uint8_t>(c.r * 0.72), static_cast<uint8_t>(c.g * 0.78),
                    static_cast<uint8_t>(c.b * 0.95)};
    out = lerp(night, c, l.day);
  }
  return l.haze > 0 ? lerp(out, l.hazeColor, l.haze * 0.6) : out;
}

/// Cheap deterministic hash of a cell position, for static noise such as stars.
inline uint32_t hash2(int x, int y) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

}  // namespace dio
