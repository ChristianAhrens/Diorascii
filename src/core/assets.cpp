/// @file core/assets.cpp
/// @brief Asset pack loading and validation.

#include "core/assets.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace dio {

AssetPack AssetPack::embedded() {
  AssetPack pack;
  for (size_t i = 0; i < kEmbeddedAssetCount; ++i) {
    const auto& a = kEmbeddedAssets[i];
    pack.addText(a.name, std::string(reinterpret_cast<const char*>(a.data), a.size));
  }
  return pack;
}

void AssetPack::addText(const std::string& sourceName, const std::string& text) {
  std::string err;
  auto sprite = Sprite::parse(text, err);
  if (!sprite) {
    errors_.push_back(sourceName + ": " + err);
    return;
  }
  sprites_[sprite->name()] = std::move(sprite);
}

size_t AssetPack::addDirectory(const std::filesystem::path& dir) {
  namespace fs = std::filesystem;
  size_t added = 0;
  std::error_code ec;
  std::vector<fs::path> files;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file() && it->path().extension() == ".sprite") files.push_back(it->path());
  }
  if (ec) errors_.push_back(dir.string() + ": " + ec.message());
  std::sort(files.begin(), files.end());
  for (const auto& p : files) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const size_t before = errors_.size();
    addText(p.string(), ss.str());
    if (errors_.size() == before) ++added;
  }
  return added;
}

std::shared_ptr<const Sprite> AssetPack::find(const std::string& name) const {
  auto it = sprites_.find(name);
  return it == sprites_.end() ? nullptr : it->second;
}

}  // namespace dio
