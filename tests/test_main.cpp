// Dependency-free test runner: each TEST registers itself, main() runs them all.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "core/assets.h"
#include "core/world.h"
#include "term/vt.h"

using namespace dio;

namespace {
std::vector<std::pair<const char*, std::function<void()>>>& registry() {
  static std::vector<std::pair<const char*, std::function<void()>>> r;
  return r;
}
int gFailures = 0;
struct Reg {
  Reg(const char* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); }
};
}  // namespace

#define TEST(name) \
  static void name(); \
  static Reg reg_##name(#name, name); \
  static void name()
#define CHECK(cond) \
  do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)

static std::string runScene(const AssetPack& pack, int w, int h, uint64_t seed, int steps) {
  World world(pack, w, h, seed);
  for (int i = 0; i < steps; ++i) world.step(0.05);
  Frame f;
  world.render(f);
  return f.toText();
}

TEST(sprite_parses_frames_and_mask) {
  std::string err;
  auto s = Sprite::parse("name: t\nspeed: 2..5\n--- art\nab\nc\n--- mask\n12\n--- art\nxy\n", err);
  CHECK(s != nullptr);
  CHECK(s->name() == "t");
  CHECK(s->frameCount() == 2);
  CHECK(s->width() == 2 && s->height() == 2);
  CHECK(s->props().range("speed", {0, 0}).second == 5);
  CHECK(s->frame(false, 0).art[1] == U"c ");  // short rows are padded
}

TEST(sprite_rejects_bad_input) {
  std::string err;
  CHECK(Sprite::parse("--- art\nx\n", err) == nullptr);       // no name
  CHECK(Sprite::parse("name: t\n", err) == nullptr);          // no art
  CHECK(Sprite::parse("name: t\n--- mask\nx\n", err) == nullptr);
  CHECK(Sprite::parse("name: t\n--- bogus\n", err) == nullptr);
}

TEST(sprite_mirrors_glyphs_and_mask) {
  std::string err;
  auto s = Sprite::parse("name: t\n--- art\n<=(o\n--- mask\n12 W\n", err);
  CHECK(s != nullptr);
  CHECK(s->frame(true, 0).art[0] == U"o)=>");
  CHECK(s->frame(true, 0).mask[0] == U"W 21");
}

TEST(embedded_assets_all_load) {
  AssetPack pack = AssetPack::embedded();
  for (const auto& e : pack.errors()) std::printf("  asset error: %s\n", e.c_str());
  CHECK(pack.errors().empty());
  CHECK(pack.find("darter") && pack.find("tang") && pack.find("grouper"));
  CHECK(pack.find("jelly") && pack.find("bubble"));
}

TEST(world_is_deterministic_per_seed) {
  AssetPack pack = AssetPack::embedded();
  CHECK(runScene(pack, 100, 30, 7, 200) == runScene(pack, 100, 30, 7, 200));
  CHECK(runScene(pack, 100, 30, 7, 200) != runScene(pack, 100, 30, 8, 200));
}

TEST(world_survives_tiny_and_odd_sizes) {
  AssetPack pack = AssetPack::embedded();
  for (int w : {1, 2, 7, 40, 300}) {
    for (int h : {1, 2, 3, 6, 12, 50}) {
      const std::string out = runScene(pack, w, h, 3, 400);
      CHECK(!out.empty());
    }
  }
}

TEST(world_keeps_population_over_time) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, 120, 40, 11);
  const size_t start = world.entityCount();
  CHECK(start > 10);
  for (int i = 0; i < 2000; ++i) world.step(0.05);  // creatures leave and respawn
  CHECK(world.entityCount() > start / 2);
  CHECK(world.entityCount() < start * 4);
}

TEST(vt_diff_is_minimal) {
  Frame a(10, 3), b(10, 3);
  CHECK(!encodeFrame(nullptr, a, ColorMode::TrueColor).empty());  // first frame: full paint
  CHECK(encodeFrame(&a, b, ColorMode::TrueColor).empty());        // identical: nothing
  b.at(4, 1).ch = U'X';
  const std::string d = encodeFrame(&a, b, ColorMode::TrueColor);
  CHECK(d.find("\x1b[2;5H") != std::string::npos);
  CHECK(d.find('X') != std::string::npos);
  CHECK(d.find("\x1b[2J") == std::string::npos);  // no full clear
}

TEST(rgb_to_256_hits_cube_and_gray) {
  CHECK(rgbTo256({255, 0, 0}) == 196);
  CHECK(rgbTo256({0, 0, 0}) == 16 || rgbTo256({0, 0, 0}) == 232);
  CHECK(rgbTo256({128, 128, 128}) >= 232);
}

int main() {
  for (auto& [name, fn] : registry()) {
    const int before = gFailures;
    fn();
    std::printf("%s %s\n", gFailures == before ? "ok  " : "FAIL", name);
  }
  std::printf("%d failure(s)\n", gFailures);
  return gFailures ? 1 : 0;
}
