/// @file core/assets.h
/// @brief Loading and looking up sprites and scenes.

#pragma once
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/sprite.h"

namespace dio {

/// One file compiled into the binary.
struct EmbeddedAsset {
  /// Path relative to the assets directory.
  const char* name;
  /// File bytes, followed by a terminating zero.
  const unsigned char* data;
  /// Size in bytes, without the terminator.
  size_t size;
};
/// Generated at build time by cmake/embed.cmake.
extern const EmbeddedAsset kEmbeddedAssets[];
/// Number of entries in kEmbeddedAssets.
extern const size_t kEmbeddedAssetCount;

/// All sprites known to the program. Later additions replace earlier ones with
/// the same name, so a user directory can override or extend the built-in set.
class AssetPack {
 public:
  /// Loads everything that was compiled into the binary.
  static AssetPack embedded();

  /// Parses and registers one sprite from text. sourceName is used in error messages.
  void addText(const std::string& sourceName, const std::string& text);
  /// Loads every *.sprite below `dir`. Returns the number of sprites added.
  size_t addDirectory(const std::filesystem::path& dir);

  /// All sprites by name.
  const std::map<std::string, std::shared_ptr<const Sprite>>& sprites() const { return sprites_; }
  /// Sprite by name, or nullptr.
  std::shared_ptr<const Sprite> find(const std::string& name) const;
  /// Problems found while loading, one message each.
  const std::vector<std::string>& errors() const { return errors_; }

 private:
  /// Sprites by name.
  std::map<std::string, std::shared_ptr<const Sprite>> sprites_;
  /// Load and validation problems.
  std::vector<std::string> errors_;
};

}  // namespace dio
