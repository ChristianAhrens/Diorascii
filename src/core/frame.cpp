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

void drawText(Frame& f, int x, int y, std::string_view utf8, Rgb fg, Rgb bg) {
  for (char32_t ch : decodeUtf8(utf8)) {
    if (f.inBounds(x, y)) {
      Cell& c = f.at(x, y);
      c.ch = ch;
      c.fg = fg;
      c.bg = bg;
      c.bold = false;
    }
    ++x;
  }
}

}  // namespace dio
