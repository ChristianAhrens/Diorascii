/// @file core/frame.h
/// @brief Styled character cells and the frame buffer frontends render from.

#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "core/color.h"

namespace dio {

/// One character cell of a frame: a glyph, its colors and attributes.
struct Cell {
  /// Unicode code point drawn in the cell (a space for an empty cell).
  char32_t ch = U' ';
  /// Foreground color, ignored when defaultFg is set.
  Rgb fg{200, 200, 200};
  /// Background color, ignored when defaultBg is set.
  Rgb bg{0, 0, 0};
  /// Bold weight.
  bool bold = false;
  /// Compares what would be drawn: colors only count when not defaulted.
  friend bool operator==(const Cell& a, const Cell& b) {
    return a.ch == b.ch && a.fg == b.fg && a.bg == b.bg && a.bold == b.bold;
  }
};

/// The only thing frontends ever see: a grid of styled cells.
class Frame {
 public:
  /// Creates an empty frame of size 0 by 0.
  Frame() = default;
  /// Creates a frame of w columns by h rows filled with default cells.
  Frame(int w, int h) { resize(w, h); }

  /// Resizes to w by h cells and resets every cell. Negative sizes become 0.
  void resize(int w, int h);
  /// Number of columns.
  int width() const { return w_; }
  /// Number of rows.
  int height() const { return h_; }
  /// True if column x, row y lies inside the frame.
  bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
  /// Cell at column x, row y. The position must be in bounds.
  Cell& at(int x, int y) { return cells_[static_cast<size_t>(y) * w_ + x]; }
  /// Read-only cell at column x, row y. The position must be in bounds.
  const Cell& at(int x, int y) const { return cells_[static_cast<size_t>(y) * w_ + x]; }

  /// Characters only, rows joined by '\n' - used by tests and `--dump`.
  std::string toText() const;

 private:
  /// Size in columns and rows.
  int w_ = 0, h_ = 0;
  /// Cells in row-major order.
  std::vector<Cell> cells_;
};

/// Draws UTF-8 text with a solid background (used by the UI layer).
void drawText(Frame& f, int x, int y, std::string_view utf8, Rgb fg, Rgb bg);

}  // namespace dio
