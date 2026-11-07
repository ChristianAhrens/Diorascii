/// @file core/menu.cpp
/// @brief Menu navigation and rendering.

#include "core/menu.h"

#include <algorithm>
#include <utility>

#include "core/utf8.h"

namespace dio {

namespace {

/// Width of a UTF-8 string in terminal columns (one per code point).
int cols(const std::string& s) { return static_cast<int>(decodeUtf8(s).size()); }

/// Pads s with spaces on the right up to width columns.
std::string pad(std::string s, int width) {
  const int n = cols(s);
  if (n < width) s.append(static_cast<size_t>(width - n), ' ');
  return s;
}

}  // namespace

void Menu::addChoice(std::string id, std::string label, std::vector<std::string> options, size_t value) {
  items_.push_back({std::move(id), std::move(label), std::move(options), value});
}

void Menu::addButton(std::string id, std::string label) {
  items_.push_back({std::move(id), std::move(label), {}, 0});
}

Menu::Item* Menu::find(const std::string& id) {
  for (auto& it : items_) {
    if (it.id == id) return &it;
  }
  return nullptr;
}

void Menu::setValue(const std::string& id, size_t value) {
  if (Item* it = find(id); it && !it->options.empty()) it->value = std::min(value, it->options.size() - 1);
}

void Menu::setLabel(const std::string& id, std::string label) {
  if (Item* it = find(id)) it->label = std::move(label);
}

size_t Menu::value(const std::string& id) const {
  for (const auto& it : items_) {
    if (it.id == id) return it.value;
  }
  return 0;
}

void Menu::step(Item& item, int delta) {
  const size_t n = item.options.size();
  item.value = (item.value + (delta > 0 ? 1 : n - 1)) % n;
  events_.push_back({item.id, item.value});
}

bool Menu::handle(Action a) {
  if (!open_) return false;
  if (items_.empty()) {
    if (a == Action::Back || a == Action::Menu) close();
    return a == Action::Back || a == Action::Menu;
  }
  Item& cur = items_[selected_];
  switch (a) {
    case Action::Up: selected_ = (selected_ + items_.size() - 1) % items_.size(); return true;
    case Action::Down: selected_ = (selected_ + 1) % items_.size(); return true;
    case Action::Left:
      if (!cur.options.empty()) step(cur, -1);
      return true;
    case Action::Right:
      if (!cur.options.empty()) step(cur, +1);
      return true;
    case Action::Select:
      if (cur.options.empty()) events_.push_back({cur.id, 0});
      else step(cur, +1);
      return true;
    case Action::Back:
    case Action::Menu: close(); return true;
    default: return false;
  }
}

std::vector<Menu::Event> Menu::takeEvents() {
  std::vector<Event> out;
  out.swap(events_);
  return out;
}

void Menu::render(Frame& f, Style style) const {
  if (!open_ || f.width() < 1 || f.height() < 1) return;

  // Row text for every item, without the selection marker.
  int labelW = 0;
  for (const auto& it : items_) labelW = std::max(labelW, cols(it.label));
  int optW = 0;
  for (const auto& it : items_) {
    for (const auto& o : it.options) optW = std::max(optW, cols(o));
  }
  std::vector<std::string> rows;
  for (const auto& it : items_) {
    std::string r = pad(it.label, labelW);
    if (!it.options.empty()) r += "  < " + pad(it.options[it.value], optW) + " >";
    rows.push_back(std::move(r));
  }

  const std::string hintLong = "arrows: move / change   enter: select   esc: close";
  const std::string hintShort = "esc: close";
  int innerW = std::max(cols(title_), 0);
  for (const auto& r : rows) innerW = std::max(innerW, cols(r) + 2);  // + selection marker
  const int boxW = innerW + 4;
  const bool longHint = f.width() >= std::max(boxW, cols(hintLong) + 4);
  const std::string& hint = longHint ? hintLong : hintShort;
  innerW = std::max({innerW, cols(hint)});
  const int boxWFinal = innerW + 4;
  const int boxH = static_cast<int>(rows.size()) + 6;  // border, title, blank, rows, blank, hint, border

  const bool plain = style != Style::Full;
  auto base = [&](Rgb fg, Rgb bg) {
    Cell c;
    c.fg = fg;
    c.bg = bg;
    c.defaultFg = c.defaultBg = plain;
    return c;
  };
  const Cell panel = base({225, 232, 245}, {18, 24, 44});
  const Cell border = base({110, 170, 220}, {18, 24, 44});
  Cell titleCell = base({255, 214, 120}, {18, 24, 44});
  titleCell.bold = true;
  Cell selCell = base({255, 255, 255}, {40, 90, 140});
  selCell.reverse = plain;

  auto put = [&](int x, int y, char32_t ch, Cell c) {
    if (!f.inBounds(x, y)) return;
    c.ch = ch;
    f.at(x, y) = c;
  };
  auto text = [&](int x, int y, const std::string& s, const Cell& c) {
    for (char32_t ch : decodeUtf8(s)) put(x++, y, ch, c);
  };

  // Too small for the box: show just the selected entry on one line.
  if (boxWFinal > f.width() || boxH > f.height()) {
    if (items_.empty()) return;
    std::string line = "[" + title_ + "] " + items_[selected_].label;
    if (!items_[selected_].options.empty()) {
      line += ": < " + items_[selected_].options[items_[selected_].value] + " >";
    }
    line.resize(std::min<size_t>(line.size(), static_cast<size_t>(f.width())));
    for (int x = 0; x < f.width(); ++x) put(x, f.height() / 2, U' ', selCell);
    text(0, f.height() / 2, line, selCell);
    return;
  }

  const int x0 = (f.width() - boxWFinal) / 2, y0 = (f.height() - boxH) / 2;
  for (int y = 0; y < boxH; ++y) {
    for (int x = 0; x < boxWFinal; ++x) put(x0 + x, y0 + y, U' ', panel);  // opaque panel
  }
  for (int x = 0; x < boxWFinal; ++x) {
    const bool corner = x == 0 || x == boxWFinal - 1;
    put(x0 + x, y0, corner ? U'+' : U'-', border);
    put(x0 + x, y0 + boxH - 1, corner ? U'+' : U'-', border);
  }
  for (int y = 1; y < boxH - 1; ++y) {
    put(x0, y0 + y, U'|', border);
    put(x0 + boxWFinal - 1, y0 + y, U'|', border);
  }

  text(x0 + (boxWFinal - cols(title_)) / 2, y0 + 1, title_, titleCell);
  for (size_t i = 0; i < rows.size(); ++i) {
    const int y = y0 + 3 + static_cast<int>(i);
    const bool sel = i == selected_;
    if (sel) {
      for (int x = 1; x < boxWFinal - 1; ++x) put(x0 + x, y, U' ', selCell);
    }
    text(x0 + 2, y, (sel ? "> " : "  ") + rows[i], sel ? selCell : panel);
  }
  text(x0 + (boxWFinal - cols(hint)) / 2, y0 + boxH - 2, hint, panel);
}

}  // namespace dio
