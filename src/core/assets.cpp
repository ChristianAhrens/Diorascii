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
  pack.validate();
  return pack;
}

void AssetPack::addText(const std::string& sourceName, const std::string& text) {
  std::string err;
  if (sourceName.size() >= 6 && sourceName.compare(sourceName.size() - 6, 6, ".scene") == 0) {
    auto scene = SceneDef::parse(text, err);
    if (!scene) {
      errors_.push_back(sourceName + ": " + err);
      return;
    }
    scenes_[scene->name] = std::move(scene);
    return;
  }
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
    const auto ext = it->path().extension();
    if (it->is_regular_file() && (ext == ".sprite" || ext == ".scene")) files.push_back(it->path());
  }
  if (ec) errors_.push_back(dir.string() + ": " + ec.message());
  std::sort(files.begin(), files.end());
  struct Revalidate { AssetPack& p; ~Revalidate() { p.validate(); } } revalidate{*this};
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

std::shared_ptr<const SceneDef> AssetPack::scene(const std::string& name) const {
  auto it = scenes_.find(name);
  return it == scenes_.end() ? nullptr : it->second;
}

std::vector<std::shared_ptr<const SceneDef>> AssetPack::scenes() const {
  std::vector<std::shared_ptr<const SceneDef>> out;
  for (const auto& [name, sc] : scenes_) out.push_back(sc);
  std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    return a->order != b->order ? a->order < b->order : a->name < b->name;
  });
  return out;
}

void AssetPack::validate() {
  auto check = [&](const SceneDef& sc, const std::string& what, const std::string& sprite) {
    if (sprite.empty() || sprite[0] == '@' || sprites_.count(sprite)) return;
    const std::string msg = "scene '" + sc.name + "': " + what + " refers to unknown sprite '" + sprite + "'";
    if (std::find(errors_.begin(), errors_.end(), msg) == errors_.end()) errors_.push_back(msg);
  };
  for (const auto& [name, sc] : scenes_) {
    for (const auto& p : sc->population) check(*sc, "population", p.sprite);
    for (const auto& d : sc->decor) check(*sc, "decor", d.sprite);
    for (const auto& e : sc->events) check(*sc, "events", e.sprite);
    check(*sc, "action", sc->action.sprite);
    check(*sc, "sun-sprite", sc->sunSprite);
    check(*sc, "moon-sprite", sc->moonSprite);
  }
}

std::shared_ptr<const Sprite> AssetPack::find(const std::string& name) const {
  auto it = sprites_.find(name);
  return it == sprites_.end() ? nullptr : it->second;
}

}  // namespace dio
