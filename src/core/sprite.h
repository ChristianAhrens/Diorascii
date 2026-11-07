/// @file core/sprite.h
/// @brief Sprites: art, color masks and header metadata.

#pragma once
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/color.h"

namespace dio {

/// Free-form `key: value` metadata from a sprite file header.
class Props {
 public:
  /// Stores a value, replacing any earlier one for the key.
  void set(std::string key, std::string value) { kv_[std::move(key)] = std::move(value); }
  /// True if the key is present.
  bool has(const std::string& key) const { return kv_.count(key) != 0; }
  /// Value of the key, or def if it is absent.
  std::string str(const std::string& key, const std::string& def = "") const;
  /// Value of the key parsed as a number, or def if it is absent.
  double num(const std::string& key, double def = 0) const;
  /// "6..14" -> {6,14}; a single number gives {n,n}.
  std::pair<double, double> range(const std::string& key, std::pair<double, double> def) const;
  std::vector<std::string> list(const std::string& key) const;  ///< whitespace separated
  Rgb color(const std::string& key, Rgb def) const;             ///< "#rrggbb"

 private:
  /// Raw header values by lower-case key.
  std::map<std::string, std::string> kv_;
};

/// One animation frame. `mask` has the same shape as `art`:
///   art ' ' + mask ' '        -> transparent
///   art ' ' + mask '_'        -> opaque (hides what is behind, keeps background)
///   art ' ' + mask color char -> solid block of that color
///   art glyph + mask char     -> glyph drawn in the mask's color
/// Mask chars: digits 1-9 pick the entity's randomized palette, letters are
/// fixed colors (see world.cpp), ' ' means the entity's default color.
struct SpriteFrame {
  /// Glyph rows.
  std::vector<std::u32string> art;
  /// Color rows, same shape as art.
  std::vector<std::u32string> mask;
};

/// An immutable multi-frame sprite: art and color masks plus the header
/// metadata that tells the world how it behaves.
class Sprite {
 public:
  /// Parses the text sprite format. Returns nullptr and sets `err` on failure.
  static std::shared_ptr<const Sprite> parse(std::string_view text, std::string& err);
  /// Builds a sprite in code (used for procedural sprites such as seaweed).
  static std::shared_ptr<const Sprite> fromFrames(std::string name, std::vector<SpriteFrame> frames,
                                                  Props props = {});

  /// Name from the header.
  const std::string& name() const { return name_; }
  /// All header metadata.
  const Props& props() const { return props_; }
  /// Width in cells. Every frame is padded to the widest one.
  int width() const { return w_; }
  /// Height in cells. Every frame is padded to the tallest one.
  int height() const { return h_; }
  /// Number of animation frames.
  size_t frameCount() const { return frames_.size(); }
  /// `mirrored` returns the left/right flipped version of the frame.
  const SpriteFrame& frame(bool mirrored, size_t i) const {
    return (mirrored ? mirrored_ : frames_)[i < frames_.size() ? i : frames_.size() - 1];
  }

 private:
  /// Pads all frames to a common size and builds the mirrored versions.
  void finish();
  /// Sprite name.
  std::string name_;
  /// Header metadata.
  Props props_;
  /// Size in cells.
  int w_ = 0, h_ = 0;
  /// The frames as authored, and the left/right flipped copies.
  std::vector<SpriteFrame> frames_, mirrored_;
};

}  // namespace dio
