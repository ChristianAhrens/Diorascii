/// @file term/vt.h
/// @brief Encoding frames as VT/ANSI escape sequences.

#pragma once
#include <string>

#include "core/frame.h"

namespace dio {

/// How colors are written. Cells flagged defaultFg/defaultBg always use the
/// terminal's own colors, whatever the mode.
///   TrueColor - 24-bit
///   Ansi256   - xterm 256-color palette
///   Ansi16    - the 16 named ANSI colors (follow the user's terminal theme)
enum class ColorMode { TrueColor, Ansi256, Ansi16 };

/// Encodes `cur` as VT/ANSI output. If `prev` is non-null and the same size, only
/// changed cells are emitted; otherwise the whole screen is repainted.
/// Pure function (no I/O), so it is unit-testable and shared by all platforms.
std::string encodeFrame(const Frame* prev, const Frame& cur, ColorMode mode);

/// Nearest color in the xterm 256-color palette (6 by 6 by 6 cube or gray ramp).
int rgbTo256(Rgb c);
/// Nearest of the 16 ANSI colors, returned as 1..15. Black (0) is never chosen:
/// it would vanish on the dark backgrounds most terminals use.
int rgbToAnsi16(Rgb c);

}  // namespace dio
