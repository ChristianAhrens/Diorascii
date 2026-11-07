/// @file term/vt.h
/// @brief Encoding frames as VT/ANSI escape sequences.

#pragma once
#include <string>

#include "core/frame.h"

namespace dio {

/// How colors are written: 24-bit, or the xterm 256-color palette.
enum class ColorMode { TrueColor, Ansi256 };

/// Encodes `cur` as VT/ANSI output. If `prev` is non-null and the same size, only
/// changed cells are emitted; otherwise the whole screen is repainted.
/// Pure function (no I/O), so it is unit-testable and shared by all platforms.
std::string encodeFrame(const Frame* prev, const Frame& cur, ColorMode mode);

/// Nearest color in the xterm 256-color palette (6 by 6 by 6 cube or gray ramp).
int rgbTo256(Rgb c);

}  // namespace dio
