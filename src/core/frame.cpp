/// @file core/frame.cpp
/// @brief Frame buffer and text drawing helpers.

#include "core/frame.h"

#include "core/utf8.h"

namespace dio {

void Frame::resize(int w, int h) {
  w_ = w < 0 ? 0 : w;
  h_ = h < 0 ? 0 : h;
  cells_.assign(static_cast<size_t>(w_) * h_, Cell{});
}

std::string Frame::toText() const {
  std::string out;
  for (int y = 0; y < h_; ++y) {
    for (int x = 0; x < w_; ++x) appendUtf8(out, at(x, y).ch);
    if (y + 1 < h_) out.push_back('\n');
  }
  return out;
}

void crossfade(const Frame& from, Frame& to, double t) {
  if (from.width() != to.width() || from.height() != to.height()) return;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  for (int y = 0; y < to.height(); ++y) {
    for (int x = 0; x < to.width(); ++x) {
      // A fixed pseudo-random threshold in [0, 1) for the cell.
      const uint32_t h = (static_cast<uint32_t>(x) * 73856093u) ^ (static_cast<uint32_t>(y) * 19349663u);
      const double threshold = ((h ^ (h >> 13)) * 2654435761u >> 8) / 16777216.0;
      const Cell& a = from.at(x, y);
      Cell& b = to.at(x, y);
      const bool plain = !a.defaultFg && !a.defaultBg && !b.defaultFg && !b.defaultBg && !a.reverse && !b.reverse;
      const Cell shown = t > threshold ? b : a;
      if (plain) {
        Cell c = shown;
        c.fg = lerp(a.fg, b.fg, t);
        c.bg = lerp(a.bg, b.bg, t);
        b = c;
      } else {
        b = shown;
      }
    }
  }
}

void drawText(Frame& f, int x, int y, std::string_view utf8, Rgb fg, Rgb bg) {
  for (char32_t ch : decodeUtf8(utf8)) {
    if (f.inBounds(x, y)) {
      Cell& c = f.at(x, y);
      c.ch = ch;
      c.fg = fg;
      c.bg = bg;
      c.bold = false;
      c.defaultFg = c.defaultBg = c.reverse = false;
    }
    ++x;
  }
}

void drawLabel(Frame& f, int x, int y, std::string_view utf8, Style style) {
  if (style == Style::Full) {
    drawText(f, x, y, utf8, {240, 240, 240}, {20, 20, 30});
    return;
  }
  for (char32_t ch : decodeUtf8(utf8)) {
    if (f.inBounds(x, y)) {
      Cell c;
      c.ch = ch;
      c.defaultFg = c.defaultBg = c.reverse = true;
      f.at(x, y) = c;
    }
    ++x;
  }
}

}  // namespace dio
