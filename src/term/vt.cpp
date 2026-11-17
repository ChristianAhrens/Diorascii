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

int rgbToAnsi16(Rgb c) {
  // xterm's default palette; index 0 (black) is deliberately left out.
  static const Rgb pal[16] = {{0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
                              {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
                              {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
                              {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255}};
  int best = 8, bestD = 1 << 30;
  for (int i = 1; i < 16; ++i) {
    const int dr = c.r - pal[i].r, dg = c.g - pal[i].g, db = c.b - pal[i].b;
    const int d = dr * dr + dg * dg + db * db;
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

namespace {

/// The Linux console's default palette, which differs from xterm's.
const Rgb kConsolePal[16] = {{0, 0, 0},       {170, 0, 0},     {0, 170, 0},     {170, 85, 0},
                             {0, 0, 170},     {170, 0, 170},   {0, 170, 170},   {170, 170, 170},
                             {85, 85, 85},    {255, 85, 85},   {85, 255, 85},   {255, 255, 85},
                             {85, 85, 255},   {255, 85, 255},  {85, 255, 255},  {255, 255, 255}};

int dist2(Rgb a, Rgb b) {
  const int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
  return dr * dr + dg * dg + db * db;
}

/// Nearest console palette entry in [first, last).
int consoleNearest(Rgb c, int first, int last) {
  int best = first, bestD = 1 << 30;
  for (int i = first; i < last; ++i) {
    const int d = dist2(c, kConsolePal[i]);
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

/// Rewrites a cell so it only uses colors the Linux console can show: backgrounds snap to
/// the nearest of the 8 dark colors. Shade glyphs for dithering are not an option, since
/// console fonts often lack them and draw '#' instead.
Cell toConsoleCell(Cell c) {
  if (!c.defaultFg) c.fg = kConsolePal[consoleNearest(c.fg, 1, 16)];
  if (!c.defaultBg) {
    // Backdrops are scaled down a little so daylight skies don't glare.
    c.bg = kConsolePal[consoleNearest(c.reverse ? c.bg : scale(c.bg, 0.8), 0, 8)];
  }
  return c;
}

/// SGR parameter that selects color c as foreground or background in the given mode.
std::string colorParam(bool fg, Rgb c, ColorMode mode) {
  switch (mode) {
    case ColorMode::TrueColor:
      return std::string(fg ? "38;2;" : "48;2;") + std::to_string(c.r) + ';' + std::to_string(c.g) +
             ';' + std::to_string(c.b);
    case ColorMode::Ansi256:
      return std::string(fg ? "38;5;" : "48;5;") + std::to_string(rgbTo256(c));
    case ColorMode::Console: {
      const int i = consoleNearest(c, fg ? 1 : 0, fg ? 16 : 8);
      return std::to_string((fg ? (i < 8 ? 30 : 90) : 40) + (i & 7));
    }
    case ColorMode::Ansi16: {
      const int i = rgbToAnsi16(c);
      const int base = fg ? (i < 8 ? 30 : 90) : (i < 8 ? 40 : 100);
      return std::to_string(base + (i & 7));
    }
  }
  return "";
}

/// True if both cells show the same foreground (defaults compare equal).
bool sameFg(const Cell& a, const Cell& b) {
  return a.defaultFg == b.defaultFg && (a.defaultFg || a.fg == b.fg);
}
/// True if both cells show the same background (defaults compare equal).
bool sameBg(const Cell& a, const Cell& b) {
  return a.defaultBg == b.defaultBg && (a.defaultBg || a.bg == b.bg);
}

}  // namespace

Style defaultStyleFor(ColorMode mode) {
  return mode == ColorMode::Console ? Style::Terminal : Style::Full;
}

std::string encodeFrame(const Frame* prev, const Frame& cur, ColorMode mode) {
  std::string out;
  const bool full = !prev || prev->width() != cur.width() || prev->height() != cur.height();
  if (full) out += "\x1b[0m\x1b[2J";

  bool haveStyle = false;
  Cell style;
  for (int y = 0; y < cur.height(); ++y) {
    int nextX = -1;  // x the cursor sits at after the last write on this row
    for (int x = 0; x < cur.width(); ++x) {
      if (!full && prev->at(x, y) == cur.at(x, y)) continue;
      const Cell c = mode == ColorMode::Console ? toConsoleCell(cur.at(x, y)) : cur.at(x, y);

      if (x != nextX) {
        out += "\x1b[" + std::to_string(y + 1) + ';' + std::to_string(x + 1) + 'H';
      }
      std::string params;
      auto add = [&params](const std::string& p) {
        if (!params.empty()) params += ';';
        params += p;
      };
      if (!haveStyle || style.bold != c.bold) add(c.bold ? "1" : "22");
      if (!haveStyle || style.reverse != c.reverse) add(c.reverse ? "7" : "27");
      if (!haveStyle || !sameFg(style, c)) add(c.defaultFg ? "39" : colorParam(true, c.fg, mode));
      if (!haveStyle || !sameBg(style, c)) add(c.defaultBg ? "49" : colorParam(false, c.bg, mode));
      if (!params.empty()) out += "\x1b[" + params + 'm';
      style = c;
      haveStyle = true;

      appendUtf8(out, c.ch);
      nextX = x + 1;
    }
  }
  if (haveStyle) out += "\x1b[0m";
  return out;
}

}  // namespace dio
