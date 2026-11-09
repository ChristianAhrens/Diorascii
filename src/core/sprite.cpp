/// @file core/sprite.cpp
/// @brief Sprite file parsing and mirroring.

#include "core/sprite.h"

#include <algorithm>
#include <cstdlib>

#include "core/textutil.h"
#include "core/utf8.h"

namespace dio {

namespace {

/// Counterpart of a glyph when art is flipped left to right, or the glyph itself.
char32_t mirrorChar(char32_t c) {
  switch (c) {
    case U'/': return U'\\';
    case U'\\': return U'/';
    case U'(': return U')';
    case U')': return U'(';
    case U'<': return U'>';
    case U'>': return U'<';
    case U'[': return U']';
    case U']': return U'[';
    case U'{': return U'}';
    case U'}': return U'{';
    case U'`': return U'\'';
    case U'\'': return U'`';
    default: return c;
  }
}

/// Drops empty rows at the end of a frame.
void trimTrailingEmpty(std::vector<std::u32string>& v) {
  while (!v.empty() && v.back().empty()) v.pop_back();
}

}  // namespace

std::string Props::str(const std::string& key, const std::string& def) const {
  auto it = kv_.find(key);
  return it == kv_.end() ? def : it->second;
}

double Props::num(const std::string& key, double def) const {
  auto it = kv_.find(key);
  return it == kv_.end() ? def : std::atof(it->second.c_str());
}

std::pair<double, double> Props::range(const std::string& key,
                                       std::pair<double, double> def) const {
  auto it = kv_.find(key);
  if (it == kv_.end()) return def;
  const std::string& v = it->second;
  const size_t dots = v.find("..");
  if (dots == std::string::npos) {
    const double n = std::atof(v.c_str());
    return {n, n};
  }
  return {std::atof(v.substr(0, dots).c_str()), std::atof(v.substr(dots + 2).c_str())};
}

std::vector<std::string> Props::list(const std::string& key) const {
  auto it = kv_.find(key);
  return it == kv_.end() ? std::vector<std::string>{} : splitWs(it->second);
}

Rgb Props::color(const std::string& key, Rgb def) const {
  Rgb c;
  auto it = kv_.find(key);
  return (it != kv_.end() && parseHexColor(it->second, c)) ? c : def;
}

std::shared_ptr<const Sprite> Sprite::parse(std::string_view text, std::string& err) {
  auto sprite = std::shared_ptr<Sprite>(new Sprite());
  enum class Section { Header, Art, Mask } section = Section::Header;
  std::vector<SpriteFrame> frames;

  size_t pos = 0;
  int lineNo = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) nl = text.size();
    std::string_view line = text.substr(pos, nl - pos);
    pos = nl + 1;
    ++lineNo;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

    if (line.substr(0, 3) == "---") {
      const std::string kind = lower(trim(line.substr(3)));
      if (kind == "art") {
        frames.emplace_back();
        section = Section::Art;
      } else if (kind == "mask") {
        if (frames.empty()) {
          err = "line " + std::to_string(lineNo) + ": '--- mask' before any '--- art'";
          return nullptr;
        }
        section = Section::Mask;
      } else {
        err = "line " + std::to_string(lineNo) + ": unknown section '" + kind + "'";
        return nullptr;
      }
      continue;
    }

    switch (section) {
      case Section::Header: {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') break;
        const size_t colon = t.find(':');
        if (colon == std::string::npos) {
          err = "line " + std::to_string(lineNo) + ": expected 'key: value'";
          return nullptr;
        }
        sprite->props_.set(lower(trim(t.substr(0, colon))), trim(t.substr(colon + 1)));
        break;
      }
      case Section::Art:
        frames.back().art.push_back(decodeUtf8(line));
        break;
      case Section::Mask:
        frames.back().mask.push_back(decodeUtf8(line));
        break;
    }
  }

  sprite->name_ = sprite->props_.str("name");
  if (sprite->name_.empty()) {
    err = "missing 'name:' header";
    return nullptr;
  }
  if (frames.empty()) {
    err = "no '--- art' section";
    return nullptr;
  }
  sprite->frames_ = std::move(frames);
  sprite->finish();
  return sprite;
}

std::shared_ptr<const Sprite> Sprite::fromFrames(std::string name, std::vector<SpriteFrame> frames,
                                                 Props props) {
  auto sprite = std::shared_ptr<Sprite>(new Sprite());
  sprite->name_ = std::move(name);
  sprite->props_ = std::move(props);
  sprite->frames_ = std::move(frames);
  sprite->finish();
  return sprite;
}

void Sprite::finish() {
  // Normalize: trim trailing empty rows, give art and mask the same shape.
  for (auto& f : frames_) {
    trimTrailingEmpty(f.art);
    trimTrailingEmpty(f.mask);
    h_ = std::max<int>(h_, static_cast<int>(std::max(f.art.size(), f.mask.size())));
    for (auto& l : f.art) w_ = std::max<int>(w_, static_cast<int>(l.size()));
    for (auto& l : f.mask) w_ = std::max<int>(w_, static_cast<int>(l.size()));
  }
  for (auto& f : frames_) {
    f.art.resize(h_);
    f.mask.resize(h_);
    for (auto& l : f.art) l.resize(w_, U' ');
    for (auto& l : f.mask) l.resize(w_, U' ');
  }
  mirrored_.clear();
  auto letter = [](char32_t c) { return (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z'); };
  for (const auto& f : frames_) {
    SpriteFrame m;
    for (size_t row = 0; row < f.art.size(); ++row) {
      std::u32string r(f.art[row].rbegin(), f.art[row].rend());
      std::u32string k(f.mask[row].rbegin(), f.mask[row].rend());
      for (auto& c : r) c = mirrorChar(c);
      // Lettering (FIRE, TAXI) must stay readable: a run of letters is put back in reading order.
      for (size_t i = 0; i < r.size();) {
        if (!letter(r[i])) { ++i; continue; }
        size_t j = i;
        while (j < r.size() && letter(r[j])) ++j;
        if (j - i >= 3) {
          std::reverse(r.begin() + static_cast<std::ptrdiff_t>(i), r.begin() + static_cast<std::ptrdiff_t>(j));
          std::reverse(k.begin() + static_cast<std::ptrdiff_t>(i), k.begin() + static_cast<std::ptrdiff_t>(j));
        }
        i = j;
      }
      m.art.push_back(std::move(r));
      m.mask.push_back(std::move(k));
    }
    mirrored_.push_back(std::move(m));
  }
}

}  // namespace dio
