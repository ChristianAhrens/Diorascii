/// @file term/vt.cpp
/// @brief VT/ANSI frame encoder and color quantization.

#include "term/vt.h"

#include <cstdlib>

#include "core/utf8.h"

namespace dio {

int rgbTo256(Rgb c) {
  auto level = [](int v) { return v < 48 ? 0 : v < 115 ? 1 : (v - 35) / 40; };
  const int r = level(c.r), g = level(c.g), b = level(c.b);
  static const int steps[] = {0, 95, 135, 175, 215, 255};
  const int cubeDist = (c.r - steps[r]) * (c.r - steps[r]) + (c.g - steps[g]) * (c.g - steps[g]) +
                       (c.b - steps[b]) * (c.b - steps[b]);

  const int avg = (c.r + c.g + c.b) / 3;
  const int gi = avg < 8 ? 0 : avg > 238 ? 23 : (avg - 8) / 10;
  const int gv = 8 + gi * 10;
  const int grayDist = (c.r - gv) * (c.r - gv) + (c.g - gv) * (c.g - gv) + (c.b - gv) * (c.b - gv);

  return grayDist < cubeDist ? 232 + gi : 16 + 36 * r + 6 * g + b;
}

namespace {

/// Appends the decimal text of v.
void appendNum(std::string& s, int v) { s += std::to_string(v); }

/// Appends the SGR parameters that select color c as foreground or background.
void appendColor(std::string& s, bool fg, Rgb c, ColorMode mode) {
  if (mode == ColorMode::TrueColor) {
    s += fg ? "38;2;" : "48;2;";
    appendNum(s, c.r); s += ';';
    appendNum(s, c.g); s += ';';
    appendNum(s, c.b);
  } else {
    s += fg ? "38;5;" : "48;5;";
    appendNum(s, rgbTo256(c));
  }
}

}  // namespace

std::string encodeFrame(const Frame* prev, const Frame& cur, ColorMode mode) {
  std::string out;
  const bool full = !prev || prev->width() != cur.width() || prev->height() != cur.height();
  if (full) out += "\x1b[0m\x1b[2J";

  bool haveStyle = false;
  Cell style;
  for (int y = 0; y < cur.height(); ++y) {
    int nextX = -1;  // x the cursor sits at after the last write on this row
    for (int x = 0; x < cur.width(); ++x) {
      const Cell& c = cur.at(x, y);
      if (!full && prev->at(x, y) == c) continue;

      if (x != nextX) {
        out += "\x1b[";
        appendNum(out, y + 1); out += ';';
        appendNum(out, x + 1); out += 'H';
      }
      if (!haveStyle || style.fg != c.fg || style.bg != c.bg || style.bold != c.bold) {
        out += "\x1b[";
        if (!haveStyle || style.bold != c.bold) out += c.bold ? "1;" : "22;";
        if (!haveStyle || style.fg != c.fg) { appendColor(out, true, c.fg, mode); out += ';'; }
        if (!haveStyle || style.bg != c.bg) { appendColor(out, false, c.bg, mode); out += ';'; }
        out.back() = 'm';
        style = c;
        haveStyle = true;
      }
      appendUtf8(out, c.ch);
      nextX = x + 1;
    }
  }
  if (haveStyle) out += "\x1b[0m";
  return out;
}

}  // namespace dio
