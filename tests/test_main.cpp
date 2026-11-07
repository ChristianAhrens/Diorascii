// Dependency-free test runner: each TEST registers itself, main() runs them all.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "core/assets.h"
#include "core/menu.h"
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

static std::string runScene(const AssetPack& pack, const std::string& scene, int w, int h,
                            uint64_t seed, int steps) {
  World world(pack, pack.scene(scene), w, h, seed);
  for (int i = 0; i < steps; ++i) world.step(0.05);
  Frame f;
  world.render(f);
  return f.toText();
}

// A tiny hand-written pack: lets tests exercise one rule at a time.
static AssetPack miniPack(const std::string& sceneBody, const std::vector<std::string>& sprites) {
  AssetPack pack;
  int i = 0;
  for (const auto& s : sprites) pack.addText("t" + std::to_string(i++) + ".sprite", s);
  pack.addText("t.scene", "name: t\nmedium-colors: #102030 #001020\nsky-fraction: 0.2\nground-rows: 2\n" + sceneBody);
  pack.validate();
  return pack;
}

// ------------------------------------------------------------ sprite format ---

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

TEST(props_parse_colors) {
  std::string err;
  auto s = Sprite::parse("name: t\ncolor: #ff8000\nbad: #zzzzzz\n--- art\nx\n", err);
  CHECK(s != nullptr);
  CHECK(s->props().color("color", {1, 2, 3}) == (Rgb{255, 128, 0}));
  CHECK(s->props().color("bad", {1, 2, 3}) == (Rgb{1, 2, 3}));
  CHECK(s->props().color("missing", {9, 9, 9}) == (Rgb{9, 9, 9}));
}


// A mask color on a blank art cell paints a solid block. That is only intended
// for sprites that say so (`solid: yes`); anywhere else it is a mask typo, such
// as a mask row that is shifted against the art.
TEST(sprite_masks_only_color_blank_cells_when_declared_solid) {
  AssetPack pack = AssetPack::embedded();
  for (const auto& [name, sprite] : pack.sprites()) {
    if (sprite->props().str("solid") == "yes") continue;
    for (size_t fi = 0; fi < sprite->frameCount(); ++fi) {
      const SpriteFrame& f = sprite->frame(false, fi);
      for (int r = 0; r < sprite->height(); ++r) {
        for (int c = 0; c < sprite->width(); ++c) {
          const bool blank = f.art[r][c] == U' ';
          const char32_t m = f.mask[r][c];
          if (blank && m != U' ' && m != U'_') {
            std::printf("  sprite '%s' frame %zu row %d col %d: mask '%c' on blank art\n", name.c_str(),
                        fi, r, c, static_cast<char>(m));
            CHECK(false);
          }
        }
      }
    }
  }
}

// ------------------------------------------------------------- scene format ---

TEST(scene_parses_all_sections) {
  std::string err;
  auto sc = SceneDef::parse(
      "name: s\ntitle: Test Scene\norder: 5\nmedium: no\nsky: #000000 #ffffff\nstars: 12\nfeed: f\n"
      "event-interval: 3..9\naction: impact meteorite\naction-label: Strike\ngravity: 2.5\ndust-settle: 12\n--- population\n# comment\nfish per-cells 200\nweed per-cols 15\nrock count 2\n"
      "--- decor\ncastle right 2\nsun center 0 y 3\n--- events\nship 2\n", err);
  CHECK(sc != nullptr);
  CHECK(sc->title == "Test Scene" && sc->order == 5 && !sc->hasMedium);
  CHECK(sc->skyLow == (Rgb{255, 255, 255}));
  CHECK(sc->population.size() == 3);
  CHECK(sc->population[0].kind == SceneDef::Population::Kind::PerCells && sc->population[0].n == 200);
  CHECK(sc->population[2].kind == SceneDef::Population::Kind::Count);
  CHECK(sc->decor.size() == 2 && sc->decor[1].hasY && sc->decor[1].y == 3);
  CHECK(sc->events.size() == 1 && sc->events[0].weight == 2);
  CHECK(sc->eventInterval.second == 9);
  CHECK(sc->action.kind == SceneDef::Action::Kind::Impact && sc->action.sprite == "meteorite");
  CHECK(sc->action.label == "Strike" && sc->gravity == 2.5 && sc->dustSettle == 12);
}

TEST(scene_rejects_bad_input) {
  std::string err;
  CHECK(SceneDef::parse("title: x\n", err) == nullptr);                          // no name
  CHECK(SceneDef::parse("name: s\n--- population\nfish often 3\n", err) == nullptr);
  CHECK(SceneDef::parse("name: s\n--- population\nfish count\n", err) == nullptr);
  CHECK(SceneDef::parse("name: s\n--- decor\nrock middle 2\n", err) == nullptr);
  CHECK(SceneDef::parse("name: s\nsky: #12345\n", err) == nullptr);
  CHECK(SceneDef::parse("name: s\nsurface-glyphs: ~~\n", err) == nullptr);
  CHECK(SceneDef::parse("name: s\n--- nonsense\n", err) == nullptr);
  CHECK(SceneDef::parse("name: s\naction: explode\n", err) == nullptr);
  CHECK(SceneDef::parse("name: s\naction: drop\n", err) == nullptr);
  CHECK(SceneDef::parse("name: s\nriver-colors: #123456\n", err) == nullptr);
}

TEST(pack_reports_unknown_sprite_references) {
  AssetPack pack;
  pack.addText("a.scene", "name: s\naction: impact boom\n--- population\nghost count 1\n");
  pack.validate();
  pack.validate();  // idempotent: no duplicate messages
  CHECK(pack.errors().size() == 2);
}

TEST(embedded_assets_all_load) {
  AssetPack pack = AssetPack::embedded();
  for (const auto& e : pack.errors()) std::printf("  asset error: %s\n", e.c_str());
  CHECK(pack.errors().empty());
  for (const char* n : {"darter", "guppy", "tang", "angel", "puffer", "eel", "grouper", "jelly",
                        "crab", "shark", "ship", "whale", "castle", "coral", "bubble", "food",
                        "splat", "earth", "lander", "rover", "meteor", "meteorite", "flash", "debris",
                        "dust", "flag", "rock", "pine_big", "pine_small", "oak", "bush", "cloud_big",
                        "cloud_small", "flower", "reeds", "bird", "deer", "rabbit", "duck", "eagle",
                        "raindrop", "splash", "sun_big", "moon_big", "firefly", "butterfly"})
    CHECK(pack.find(n) != nullptr);
  const auto scenes = pack.scenes();
  CHECK(scenes.size() >= 2);
  CHECK(scenes[0]->order <= scenes[1]->order);
}

// ---------------------------------------------------------------- simulation ---

TEST(every_scene_is_deterministic_per_seed) {
  AssetPack pack = AssetPack::embedded();
  for (const auto& sc : pack.scenes()) {
    CHECK(runScene(pack, sc->name, 100, 30, 7, 200) == runScene(pack, sc->name, 100, 30, 7, 200));
    CHECK(runScene(pack, sc->name, 100, 30, 7, 200) != runScene(pack, sc->name, 100, 30, 8, 200));
  }
}

TEST(every_scene_survives_tiny_and_odd_sizes) {
  AssetPack pack = AssetPack::embedded();
  for (const auto& sc : pack.scenes()) {
    for (int w : {1, 2, 7, 40, 300}) {
      for (int h : {1, 2, 3, 6, 12, 50}) {
        CHECK(!runScene(pack, sc->name, w, h, 3, 600).empty());
      }
    }
  }
}

TEST(aquarium_keeps_population_over_time) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("aquarium"), 120, 40, 11);
  const size_t start = world.entityCount();
  CHECK(start > 10);
  for (int i = 0; i < 4000; ++i) world.step(0.05);  // creatures leave and respawn
  CHECK(world.entityCount() > start / 2);
  CHECK(world.entityCount() < start * 4);
}

TEST(events_appear_on_schedule_and_one_at_a_time) {
  AssetPack pack = miniPack(
      "event-first: 1..2\nevent-interval: 1..2\n--- events\nping 1\n",
      {"name: ping\nhabitat: medium\nspeed: 4..4\ntag: ping\nbehaviors: swim\n--- art\n[]\n"});
  World world(pack, pack.scene("t"), 60, 20, 1);
  CHECK(world.countTag("ping") == 0);
  size_t maxAlive = 0;
  bool seen = false;
  for (int i = 0; i < 600; ++i) {
    world.step(0.05);
    maxAlive = std::max(maxAlive, world.countTag("ping"));
    seen |= world.countTag("ping") > 0;
  }
  CHECK(seen);
  CHECK(maxAlive == 1);
}

TEST(feeding_drops_a_clump_caps_at_thirty_and_fish_eat_it) {
  AssetPack pack = miniPack(
      "action: drop food\n--- population\nfish count 4\n",
      {"name: fish\nhabitat: medium\nspeed: 3..3\ndepth: 5\nbehaviors: seek_food swim\n--- art\n<o>\n",
       "name: food\ntag: food\nbehaviors: sink\n--- art\n*\n"});
  World world(pack, pack.scene("t"), 60, 24, 5);
  CHECK(world.interact() == 8);
  CHECK(world.countTag("food") == 8);
  for (int i = 0; i < 6; ++i) world.interact();
  CHECK(world.countTag("food") == 30);  // never more than 30 at once
  for (int i = 0; i < 1200; ++i) world.step(0.05);  // 60 s
  CHECK(world.countTag("food") == 0);               // eaten or decayed on the ground
  CHECK(world.foodEatenCount() > 0);
}

TEST(actions_do_nothing_without_a_matching_scene_setup) {
  AssetPack a = miniPack("--- population\n", {});
  World w1(a, a.scene("t"), 60, 20, 1);
  CHECK(w1.interact() == 0);  // the scene has no action

  AssetPack b = miniPack("medium: no\nsky-fraction: 1\naction: drop food\n",
                         {"name: food\ntag: food\nbehaviors: sink\n--- art\n*\n"});
  World w2(b, b.scene("t"), 60, 20, 1);
  CHECK(w2.interact() == 0);  // food needs water to sink through
}

TEST(each_scene_has_its_own_action) {
  AssetPack pack = AssetPack::embedded();
  World aq(pack, pack.scene("aquarium"), 80, 30, 1);
  CHECK(aq.interact() == 8);  // a clump of eight flakes of food
  World moon(pack, pack.scene("moon"), 80, 30, 1);
  CHECK(moon.interact() == 1);  // one meteorite
  CHECK(moon.interact() == 0);  // ...only one at a time
  CHECK(pack.scene("aquarium")->action.label != pack.scene("moon")->action.label);
}

TEST(predators_eat_prey_and_prey_respawns) {
  AssetPack pack = miniPack(
      "--- population\nprey count 60\nbiter count 1\n",
      {"name: prey\nhabitat: medium\nspeed: 0..0\ndepth: 5\ntag: prey\nbehaviors: swim\n--- art\n<>\n",
       "name: splat\nfps: 6\nloop: no\ndie-after-animation: yes\n--- art\n*\n--- art\n.\n",
       "name: biter\nfacing: right\nhabitat: medium\nspeed: 25..25\ndepth: 2\nbehaviors: swim hunt\n"
       "mouth-row: 4\n--- art\n       |\n       |\n       |\n       |\n =====>\n       |\n       |\n"});
  World world(pack, pack.scene("t"), 80, 16, 2);
  const size_t prey = world.countTag("prey");
  CHECK(prey == 60);
  for (int i = 0; i < 1200; ++i) world.step(0.05);
  CHECK(world.eatenCount() > 0);
  CHECK(world.countTag("prey") >= prey - 10);  // eaten fish are replaced
}

TEST(moon_has_no_medium_and_ground_walkers_stand_on_the_ground) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("moon"), 100, 30, 4);
  CHECK(!world.hasMedium());
  CHECK(world.surfaceY() == world.seabedY());
  Frame f;
  world.render(f);
  CHECK(f.height() == 30);
}


// ---------------------------------------------------------------------- menu ---

static Menu sampleMenu() {
  Menu m("T");
  m.addChoice("scene", "Scene", {"A", "B", "C"}, 0);
  m.addChoice("style", "Style", {"x", "y"}, 0);
  m.addButton("quit", "Quit");
  return m;
}

TEST(menu_is_inert_until_opened) {
  Menu m = sampleMenu();
  CHECK(!m.isOpen());
  CHECK(!m.handle(Action::Down));
  CHECK(m.takeEvents().empty());
  Frame f(60, 20);
  m.render(f, Style::Full);
  CHECK(f.toText().find("Scene") == std::string::npos);
}

TEST(menu_navigates_wraps_and_reports_changes) {
  Menu m = sampleMenu();
  m.open();
  CHECK(m.handle(Action::Right));  // scene A -> B
  auto ev = m.takeEvents();
  CHECK(ev.size() == 1 && ev[0].id == "scene" && ev[0].value == 1);
  CHECK(m.handle(Action::Left));
  CHECK(m.handle(Action::Left));   // wraps A -> C
  ev = m.takeEvents();
  CHECK(ev.size() == 2 && ev[1].value == 2);
  CHECK(m.value("scene") == 2);

  CHECK(m.handle(Action::Up));     // wraps to the last entry (button)
  CHECK(m.handle(Action::Right));  // nothing to change on a button
  CHECK(m.takeEvents().empty());
  CHECK(m.handle(Action::Select));
  ev = m.takeEvents();
  CHECK(ev.size() == 1 && ev[0].id == "quit");

  CHECK(m.handle(Action::Down));   // wraps to the first entry
  CHECK(m.handle(Action::Select)); // select on a choice advances it
  ev = m.takeEvents();
  CHECK(ev.size() == 1 && ev[0].id == "scene" && ev[0].value == 0);
}

TEST(menu_closes_with_back_and_leaves_other_actions_alone) {
  Menu m = sampleMenu();
  m.open();
  CHECK(!m.handle(Action::Quit));  // the app decides what quit means
  CHECK(!m.handle(Action::Interact));
  CHECK(m.isOpen());
  CHECK(m.handle(Action::Back));
  CHECK(!m.isOpen());
  m.open();
  CHECK(m.handle(Action::Menu));
  CHECK(!m.isOpen());
}

TEST(menu_set_value_is_silent_and_clamped) {
  Menu m = sampleMenu();
  m.setValue("scene", 99);
  CHECK(m.value("scene") == 2);
  CHECK(m.takeEvents().empty());
}

TEST(menu_renders_a_centered_box_and_degrades_on_small_screens) {
  Menu m = sampleMenu();
  m.open();
  Frame big(80, 24);
  m.render(big, Style::Full);
  const std::string t = big.toText();
  CHECK(t.find("Scene") != std::string::npos && t.find("< A") != std::string::npos);
  CHECK(t.find("T") != std::string::npos && t.find("+--") != std::string::npos);
  CHECK(t.find("esc: close") != std::string::npos);

  Frame plain(80, 24);
  m.render(plain, Style::Mono);
  bool anyRgb = false, anyReverse = false;
  for (int y = 0; y < plain.height(); ++y) {
    for (int x = 0; x < plain.width(); ++x) {
      const Cell& c = plain.at(x, y);
      if (c.ch != U' ' && !c.defaultFg) anyRgb = true;
      anyReverse |= c.reverse;
    }
  }
  CHECK(!anyRgb);       // monochrome menu uses only the terminal's own colors
  CHECK(anyReverse);    // ...and marks the selection with reverse video

  for (int w : {1, 5, 20, 30}) {
    for (int h : {1, 3, 8, 12}) {
      Frame tiny(w, h);
      m.render(tiny, Style::Full);  // must not crash or write out of bounds
      CHECK(tiny.width() == w);
    }
  }
  Frame narrow(16, 12);
  m.render(narrow, Style::Full);
  CHECK(narrow.toText().find("[T] ") != std::string::npos);  // compact one-line form
}

// -------------------------------------------------------------------- styles ---

TEST(styles_control_backdrop_and_color_use) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("aquarium"), 100, 30, 4);
  for (int i = 0; i < 100; ++i) world.step(0.05);

  Frame full, term, mono;
  world.render(full, Style::Full);
  world.render(term, Style::Terminal);
  world.render(mono, Style::Mono);
  CHECK(full.toText() == term.toText() && full.toText() == mono.toText());  // same glyphs

  for (int y = 0; y < 30; ++y) {
    for (int x = 0; x < 100; ++x) {
      CHECK(!full.at(x, y).defaultBg);
      // The terminal's own background everywhere, except under solid color blocks (the sun).
      CHECK(term.at(x, y).defaultBg || term.at(x, y).ch == U' ');
      CHECK(mono.at(x, y).defaultBg && mono.at(x, y).defaultFg);
    }
  }
  bool anyColoredGlyph = false;
  for (int y = 0; y < 30; ++y) {
    for (int x = 0; x < 100; ++x) {
      const Cell& c = term.at(x, y);
      anyColoredGlyph |= c.ch != U' ' && !c.defaultFg;
    }
  }
  CHECK(anyColoredGlyph);  // the terminal style still colors the sprites
}

TEST(solid_blocks_keep_their_color_in_terminal_style_but_not_in_mono) {
  AssetPack pack = miniPack("--- decor\nblock left 0 y 1\n",
                            {"name: block\nhabitat: sky\nsolid: yes\n--- art\n  \n--- mask\nbb\n"});
  World world(pack, pack.scene("t"), 20, 12, 1);
  Frame term, mono;
  world.render(term, Style::Terminal);
  world.render(mono, Style::Mono);
  CHECK(!term.at(0, 1).defaultBg);
  CHECK(mono.at(0, 1).defaultBg);
}


// -------------------------------------------------------------------- forest ---

static std::string forestScene(const std::string& extra = "") {
  return "medium: no\nsky-fraction: 1\nground-rows: 8\nriver-rows: 3\nweather-duration: 5\n"
         "action: weather raindrop\n" + extra;
}

TEST(scene_parses_river_and_weather) {
  std::string err;
  auto sc = SceneDef::parse(
      "name: s\nriver-rows: 3\nriver-colors: #102030 #405060\nriver-flow: 4\n"
      "action: weather rain\nweather-duration: 12\n", err);
  CHECK(sc != nullptr);
  CHECK(sc->riverRows == 3 && sc->riverFlow == 4 && sc->weatherDuration == 12);
  CHECK(sc->riverTop == (Rgb{16, 32, 48}) && sc->riverDeep == (Rgb{64, 80, 96}));
  CHECK(sc->action.kind == SceneDef::Action::Kind::Weather && sc->action.sprite == "rain");
}

TEST(river_takes_the_bottom_of_the_ground_and_never_more_than_half) {
  AssetPack pack = miniPack(forestScene(), {"name: raindrop\nbehaviors: rain\n--- art\n\\\n"});
  World tall(pack, pack.scene("t"), 60, 30, 1);
  CHECK(tall.riverRows() == 3 && tall.riverTop() == 27);
  CHECK(tall.seabedY() == 30 - 7);  // eight ground rows are capped to a quarter of the height
  World squat(pack, pack.scene("t"), 60, 12, 1);
  CHECK(squat.riverRows() <= (12 - squat.seabedY()) / 2);
  World tiny(pack, pack.scene("t"), 5, 3, 1);
  CHECK(tiny.riverRows() == 0 && tiny.riverTop() == 3);
  Frame f;
  tall.render(f);
  CHECK(f.at(10, 28).bg != f.at(10, 20).bg);  // river rows look different from the bank
}

TEST(river_creatures_stay_inside_the_river) {
  AssetPack pack = miniPack(forestScene("--- population\nduck count 40\n"),
                            {"name: duck\nhabitat: river\nspeed: 1..2\nbehaviors: swim\n--- art\n_o>\n`-'\n",
                             "name: raindrop\nbehaviors: rain\n--- art\n\\\n"});
  World world(pack, pack.scene("t"), 80, 30, 2);
  CHECK(world.countTag("") >= 40);
  for (auto& e : world.entities()) {
    CHECK(e->y >= world.riverTop());
    CHECK(e->y + e->height() <= world.height());
  }
  AssetPack none = miniPack("--- population\nduck count 5\n", {"name: duck\nhabitat: river\n--- art\nd\n"});
  World dry(none, none.scene("t"), 40, 20, 2);
  CHECK(dry.entityCount() == 0);  // no river, so no river creatures
}

TEST(lanes_spread_ground_sprites_over_the_bank_and_order_them_by_depth) {
  AssetPack pack = miniPack(forestScene("--- population\ntree count 60\n"),
                            {"name: tree\nhabitat: ground\nspeed: 0..0\nlanes: 4\ndepth: 10..18\ntag: tree\n--- art\nT\nT\n",
                             "name: raindrop\nbehaviors: rain\n--- art\n\\\n"});
  World world(pack, pack.scene("t"), 80, 30, 6);
  bool seen[4] = {false, false, false, false};
  for (auto& e : world.entities()) {
    const int lane = static_cast<int>(e->y) - (world.seabedY() - e->height());
    CHECK(lane >= 0 && lane <= 3);
    CHECK(e->y + e->height() <= world.riverTop() + 3);  // never into the river
    CHECK(e->z == 18 - lane * 8 / 3);                    // front lanes have smaller z, drawn later
    seen[lane] = true;
  }
  CHECK(seen[0] && seen[1] && seen[2] && seen[3]);
}

TEST(lanes_shrink_on_a_narrow_bank) {
  AssetPack pack = miniPack("medium: no\nsky-fraction: 1\nground-rows: 2\n--- population\ntree count 20\n",
                            {"name: tree\nhabitat: ground\nspeed: 0..0\nlanes: 4\ntag: tree\n--- art\nT\n"});
  World world(pack, pack.scene("t"), 40, 12, 3);
  for (auto& e : world.entities()) CHECK(e->y >= world.seabedY() - 1 && e->y <= world.seabedY() + 1);
}

TEST(rain_falls_for_a_while_splashes_and_stops) {
  AssetPack pack = miniPack(
      forestScene(),
      {"name: raindrop\nfacing: right\ntag: rain\nbehaviors: rain\n--- art\n\\\n",
       "name: splash\ntag: splash\nfps: 10\nloop: no\ndie-after-animation: yes\n--- art\n.\n--- art\no\n"});
  World world(pack, pack.scene("t"), 80, 30, 7);
  CHECK(!world.raining());
  CHECK(world.countTag("rain") == 0);
  CHECK(world.interact() == 1);
  CHECK(world.raining());
  size_t drops = 0, splashes = 0;
  for (int i = 0; i < 60; ++i) {
    world.step(0.05);
    drops = std::max(drops, world.countTag("rain"));
    splashes = std::max(splashes, world.countTag("splash"));
  }
  CHECK(drops > 10);
  CHECK(splashes > 0);
  for (int i = 0; i < 200; ++i) world.step(0.05);  // 5 s of rain have passed, drops need time to land
  CHECK(!world.raining());
  for (int i = 0; i < 80; ++i) world.step(0.05);
  CHECK(world.countTag("rain") == 0);
  CHECK(world.interact() == 1);  // and it can rain again
  CHECK(world.raining());
}

TEST(rain_drops_land_on_the_ground_or_in_the_river) {
  AssetPack pack = miniPack(forestScene(), {"name: raindrop\ntag: rain\nbehaviors: rain\n--- art\n\\\n"});
  World world(pack, pack.scene("t"), 80, 30, 8);
  world.interact();
  for (int i = 0; i < 40; ++i) world.step(0.05);
  for (auto& e : world.entities()) {
    if (e->tag != "rain") continue;
    CHECK(e->anchorY >= world.seabedY() && e->anchorY <= world.height() - 1);
  }
}

TEST(pressing_the_action_again_keeps_the_rain_going) {
  AssetPack pack = miniPack(forestScene(), {"name: raindrop\ntag: rain\nbehaviors: rain\n--- art\n\\\n"});
  World world(pack, pack.scene("t"), 80, 30, 8);
  world.interact();
  for (int i = 0; i < 80; ++i) world.step(0.05);  // 4 of 5 seconds
  world.interact();
  for (int i = 0; i < 80; ++i) world.step(0.05);  // would be over without the second press
  CHECK(world.raining());
}

TEST(hopping_animals_jump_in_arcs_and_travel_at_their_average_speed) {
  AssetPack pack = miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 4\n--- population\nbunny count 1\n",
      {"name: bunny\nfacing: right\nhabitat: ground\nspeed: 2..2\nhop-period: 1\nhop-height: 2\ntag: bunny\n"
       "behaviors: hop\n--- art\nb\n"});
  World world(pack, pack.scene("t"), 400, 24, 5);
  Entity* b = world.entities()[0].get();
  b->x = 100;
  b->vx = 2;
  b->age = 0;
  b->phase = 0;
  const double start = b->x;
  double lowest = 0, highest = 0;
  for (int i = 0; i < 400; ++i) {  // 20 s
    world.step(0.05);
    lowest = std::min(lowest, b->y - b->anchorY);
    highest = std::max(highest, b->y - b->anchorY);
  }
  CHECK(highest <= 1e-9);                 // it only ever goes up from the ground
  CHECK(lowest < -1.9 && lowest >= -2.0 - 1e-9);
  CHECK(std::fabs((b->x - start) - 40) < 3);  // about 2 cells per second on average
}

TEST(wandering_creatures_wobble_around_their_row) {
  AssetPack pack = miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 4\n--- population\nfly count 1\n",
      {"name: fly\nfacing: right\nhabitat: sky\nspeed: 1..1\nbob-amplitude: 1\nbob-frequency: 1\n"
       "behaviors: wander\n--- art\n.\n"});
  World world(pack, pack.scene("t"), 200, 24, 5);
  Entity* f = world.entities()[0].get();
  double far = 0;
  for (int i = 0; i < 200; ++i) {
    world.step(0.05);
    far = std::max(far, std::fabs(f->y - f->anchorY));
  }
  CHECK(far > 0.3);          // it does wobble
  CHECK(far <= 1.0 + 0.6 + 1e-9);  // ...but stays near its row
}

// ------------------------------------------------------------- day and night ---

// A scene with a 100 second day that starts at 06:00, a 2-row sun 'S' and a moon 'M'.
static AssetPack cyclePack(const std::string& body = "", const std::vector<std::string>& extra = {}) {
  std::vector<std::string> sprites = {
      "name: sun\nglow: yes\nhabitat: sky\ncolor: #ffffff\n--- art\nS\nS\n",
      "name: moon\nglow: yes\nhabitat: sky\ncolor: #ffffff\n--- art\nM\nM\n"};
  sprites.insert(sprites.end(), extra.begin(), extra.end());
  return miniPack("medium: no\nsky-fraction: 1\nground-rows: 4\nsky: #3050a0 #8090d0\nstars: 7\n"
                  "day-night: yes\nday-length: 100\nstart-hour: 6\nsun-sprite: sun\nmoon-sprite: moon\n" + body,
                  sprites);
}

static int countChar(const Frame& f, char32_t ch, int fromRow, int toRow) {
  int n = 0;
  for (int y = fromRow; y < toRow && y < f.height(); ++y) {
    for (int x = 0; x < f.width(); ++x) n += f.at(x, y).ch == ch;
  }
  return n;
}

TEST(scene_parses_day_night_keys) {
  std::string err;
  auto sc = SceneDef::parse("name: s\nday-night: yes\nday-length: 90\nstart-hour: 30\nsun-sprite: a\nmoon-sprite: b\n", err);
  CHECK(sc != nullptr);
  CHECK(sc->dayNight && sc->dayLength == 90 && sc->startHour == 24);  // the start hour is clamped
  CHECK(sc->sunSprite == "a" && sc->moonSprite == "b");
  auto off = SceneDef::parse("name: s\n", err);
  CHECK(off && !off->dayNight);
  AssetPack pack;
  pack.addText("a.scene", "name: s\nsun-sprite: ghost\nmoon-sprite: phantom\n");
  pack.validate();
  CHECK(pack.errors().size() == 2);  // unknown sky bodies are reported
}

TEST(time_of_day_advances_wraps_and_reports_the_clock) {
  AssetPack pack = cyclePack();
  World world(pack, pack.scene("t"), 40, 20, 1);
  CHECK(std::fabs(world.phase() - 0.25) < 1e-9);
  CHECK(world.clockText() == "06:00");
  for (int i = 0; i < 500; ++i) world.step(0.05);  // 25 s of a 100 s day
  CHECK(std::fabs(world.phase() - 0.5) < 1e-6);
  CHECK(world.clockText() == "12:00");
  for (int i = 0; i < 1500; ++i) world.step(0.05);  // 75 s more: wraps past midnight
  CHECK(std::fabs(world.phase() - 0.25) < 1e-6);
  world.setHour(14.5);
  CHECK(world.clockText() == "14:30");
  world.setHour(0);
  CHECK(world.clockText() == "00:00");
  world.setHour(25.5);  // wraps instead of overflowing
  CHECK(world.clockText() == "01:30");
}

TEST(light_follows_the_sun) {
  AssetPack pack = cyclePack();
  World world(pack, pack.scene("t"), 40, 20, 1);
  world.setPhase(0.5);
  CHECK(world.light().day == 1 && world.light().sun == 1 && world.light().stars == 0);
  world.setPhase(0.0);
  CHECK(world.light().day == 0 && world.light().stars == 1);
  double previous = -1;
  for (int i = 0; i <= 10; ++i) {  // from midnight to noon it only gets brighter
    world.setPhase(0.5 * i / 10);
    CHECK(world.light().day >= previous);
    previous = world.light().day;
  }
  world.setPhase(0.25);  // the horizon glow peaks at sunrise and sunset, not at noon or midnight
  const double sunrise = world.light().glow;
  world.setPhase(0.75);
  CHECK(std::fabs(world.light().glow - sunrise) < 1e-9);
  world.setPhase(0.5);
  CHECK(world.light().glow < 0.01 && sunrise > 0.9);
  world.setPhase(0.0);
  CHECK(world.light().glow < 0.01);
}

TEST(time_modes_freeze_the_clock_and_cycle_resumes_from_there) {
  AssetPack pack = cyclePack();
  World world(pack, pack.scene("t"), 40, 20, 1);
  world.setTimeMode(World::TimeMode::Day);
  CHECK(world.phase() == 0.5);
  for (int i = 0; i < 1000; ++i) world.step(0.05);
  CHECK(world.phase() == 0.5);  // frozen
  world.setTimeMode(World::TimeMode::Night);
  CHECK(world.phase() == 0.0);
  world.setTimeMode(World::TimeMode::Dawn);
  CHECK(world.phase() == 0.27);
  world.setTimeMode(World::TimeMode::Dusk);
  CHECK(world.phase() == 0.73);
  world.setTimeMode(World::TimeMode::Cycle);
  for (int i = 0; i < 200; ++i) world.step(0.05);  // 10 s of a 100 s day
  CHECK(std::fabs(world.phase() - 0.83) < 1e-6);   // carried on from dusk
  CHECK(world.timeMode() == World::TimeMode::Cycle);
}

TEST(scenes_without_a_cycle_ignore_the_time_of_day) {
  AssetPack pack = AssetPack::embedded();
  World moon(pack, pack.scene("moon"), 60, 24, 1);
  CHECK(!moon.hasDayNight());
  const double before = moon.phase();
  moon.setTimeMode(World::TimeMode::Night);
  CHECK(moon.timeMode() == World::TimeMode::Cycle);  // unchanged
  for (int i = 0; i < 200; ++i) moon.step(0.05);
  CHECK(moon.phase() == before);
  const Light l = moon.light();
  CHECK(l.day == 1 && l.sun == 1 && l.glow == 0 && l.stars == 1);
}

TEST(stars_come_out_only_at_night_in_scenes_with_a_cycle) {
  AssetPack pack = cyclePack();
  World world(pack, pack.scene("t"), 100, 30, 1);
  Frame f;
  world.setPhase(0.5);
  world.render(f);
  const int noon = countChar(f, U'*', 0, world.surfaceY()) + countChar(f, U'.', 0, world.surfaceY());
  world.setPhase(0.0);
  world.render(f);
  const int night = countChar(f, U'*', 0, world.surfaceY()) + countChar(f, U'.', 0, world.surfaceY());
  CHECK(noon == 0);
  CHECK(night > 20);
  AssetPack all = AssetPack::embedded();
  World moon(all, all.scene("moon"), 100, 30, 1);  // no cycle: the stars are always there
  moon.render(f);
  CHECK(countChar(f, U'*', 0, moon.surfaceY()) + countChar(f, U'.', 0, moon.surfaceY()) > 20);
}

TEST(sun_and_moon_cross_the_sky_and_set_behind_the_horizon) {
  AssetPack pack = cyclePack();
  World world(pack, pack.scene("t"), 101, 30, 1);
  Frame f;
  auto sunColumn = [&](double phase) {
    world.setPhase(phase);
    world.render(f);
    for (int x = 0; x < f.width(); ++x) {
      for (int y = 0; y < world.surfaceY(); ++y) {
        if (f.at(x, y).ch == U'S') return x;
      }
    }
    return -1;
  };
  const int morning = sunColumn(0.35), noon = sunColumn(0.5), evening = sunColumn(0.65);
  CHECK(morning >= 0 && noon >= 0 && evening >= 0);
  CHECK(morning < noon && noon < evening);          // it travels from left to right
  CHECK(std::abs(noon - 50) <= 1);                  // and is in the middle at noon
  world.setPhase(0.5);
  world.render(f);
  CHECK(countChar(f, U'S', 0, world.height()) == 2 && countChar(f, U'M', 0, world.height()) == 0);
  CHECK(f.at(noon, 0).ch == U'S');                  // at the top of the sky
  world.setPhase(0.254);                            // just risen: only the top row clears the horizon
  world.render(f);
  CHECK(countChar(f, U'S', 0, world.height()) == 1);
  world.setPhase(0.0);
  world.render(f);
  CHECK(countChar(f, U'M', 0, world.height()) == 2 && countChar(f, U'S', 0, world.height()) == 0);
  for (int y = world.surfaceY(); y < world.height(); ++y) {  // never drawn over the ground
    for (int x = 0; x < f.width(); ++x) CHECK(f.at(x, y).ch != U'M');
  }
}

TEST(day_and_night_creatures_appear_at_the_right_time) {
  AssetPack pack = cyclePack("--- population\nbee count 1\nowl count 1\n",
                             {"name: bee\nhabitat: sky\nspeed: 0..0\nactive: day\n--- art\nB\n",
                              "name: owl\nhabitat: sky\nspeed: 0..0\nactive: night\n--- art\nO\n"});
  World world(pack, pack.scene("t"), 80, 30, 2);
  for (auto& e : world.entities()) e->x = 10;  // keep them on screen
  Frame f;
  world.setPhase(0.5);
  world.render(f);
  CHECK(countChar(f, U'B', 0, 30) == 1 && countChar(f, U'O', 0, 30) == 0);
  world.setPhase(0.0);
  world.render(f);
  CHECK(countChar(f, U'B', 0, 30) == 0 && countChar(f, U'O', 0, 30) == 1);

  AssetPack flat = miniPack("medium: no\nsky-fraction: 1\n--- population\nbee count 1\nowl count 1\n",
                            {"name: bee\nhabitat: sky\nspeed: 0..0\nactive: day\n--- art\nB\n",
                             "name: owl\nhabitat: sky\nspeed: 0..0\nactive: night\n--- art\nO\n"});
  World noCycle(flat, flat.scene("t"), 80, 30, 2);
  for (auto& e : noCycle.entities()) e->x = 10;
  noCycle.render(f);
  CHECK(countChar(f, U'B', 0, 30) == 1 && countChar(f, U'O', 0, 30) == 1);  // `active` needs a cycle
}

TEST(light_dims_things_unless_they_glow_and_the_backdrop_follows_the_sun) {
  AssetPack pack = cyclePack("--- population\nlamp count 1\nstone count 1\n",
                             {"name: lamp\nhabitat: sky\nspeed: 0..0\nglow: yes\ncolor: #ffffff\n--- art\nL\n",
                              "name: stone\nhabitat: sky\nspeed: 0..0\ncolor: #ffffff\n--- art\nT\n"});
  World world(pack, pack.scene("t"), 80, 30, 3);
  int row = 3;
  for (auto& e : world.entities()) {
    if (e->tag.empty() && e->sprite->name() != "lamp" && e->sprite->name() != "stone") continue;
    e->x = e->sprite->name() == "lamp" ? 10 : 20;
    e->y = row;
  }
  Frame f;
  world.setPhase(0.5);
  world.render(f);
  const Rgb skyNoon = f.at(40, 0).bg;
  CHECK(f.at(10, row).fg == (Rgb{255, 255, 255}) && f.at(20, row).fg == (Rgb{255, 255, 255}));
  CHECK(skyNoon == (Rgb{0x30, 0x50, 0xa0}));  // at noon the scene's own colors, exactly

  world.setPhase(0.0);
  world.render(f);
  CHECK(f.at(10, row).fg == (Rgb{255, 255, 255}));                        // a lamp keeps shining
  CHECK(f.at(20, row).fg.r < 200 && f.at(20, row).fg.b > f.at(20, row).fg.r);  // a stone goes dark and blue
  const Rgb skyNight = f.at(40, 0).bg;
  CHECK(skyNight.r + skyNight.g + skyNight.b < (skyNoon.r + skyNoon.g + skyNoon.b) / 3);

  world.setPhase(0.25);  // sunrise: the horizon glows red
  world.render(f);
  const Rgb horizon = f.at(40, world.surfaceY() - 1).bg;
  world.setPhase(0.5);
  world.render(f);
  const Rgb horizonNoon = f.at(40, world.surfaceY() - 1).bg;
  CHECK(horizon.r > horizonNoon.r);
}

TEST(rain_clouds_dim_the_day_and_hide_the_sun) {
  AssetPack pack = cyclePack("action: weather drop\nweather-duration: 4\n", {"name: drop\nbehaviors: rain\n--- art\n\\\n"});
  World world(pack, pack.scene("t"), 80, 30, 4);
  world.setTimeMode(World::TimeMode::Day);
  Frame f;
  world.render(f);
  CHECK(world.light().day == 1 && countChar(f, U'S', 0, 30) == 2);
  CHECK(world.interact() == 1);
  for (int i = 0; i < 70; ++i) world.step(0.05);  // 3.5 s into a 4 s shower: the clouds have rolled in
  CHECK(world.overcast() > 0.9);
  CHECK(world.light().day < 0.6);
  world.render(f);
  CHECK(countChar(f, U'S', 0, 30) == 0);  // the sun is behind the clouds
  for (int i = 0; i < 400; ++i) world.step(0.05);  // the rain stopped long ago
  CHECK(world.overcast() < 0.01);
  CHECK(world.light().day > 0.99);
}

TEST(animals_go_to_sleep_at_night_one_after_another_and_wake_at_dawn) {
  AssetPack pack = cyclePack("--- population\nsleeper count 40\n",
                             {"name: sleeper\nhabitat: sky\nspeed: 0..0\nsleeps: 1\n--- art\nZ\n"});
  World world(pack, pack.scene("t"), 100, 30, 3);
  int i = 0;
  for (auto& e : world.entities()) {
    e->x = 2 * i++;
    e->y = 2;
  }
  Frame f;
  auto shown = [&](double phase) {
    world.setPhase(phase);
    world.render(f);
    return countChar(f, U'Z', 0, 30);
  };
  CHECK(shown(0.5) == 40);  // all awake at noon
  CHECK(shown(0.0) == 0);   // all asleep at midnight
  int previous = 41;
  bool somewhere = false;
  for (double phase = 0.32; phase >= 0.2199; phase -= 0.005) {  // dusk: the sun sinks, animals drop off
    const int now = shown(phase);
    CHECK(now <= previous);
    somewhere |= now > 0 && now < 40;  // not all at the same moment
    previous = now;
  }
  CHECK(somewhere);
  CHECK(shown(0.5) == 40);  // and everyone is back at noon
}

TEST(only_the_sleepy_share_of_a_species_sleeps) {
  AssetPack pack = cyclePack("--- population\nsleeper count 80\n",
                             {"name: sleeper\nhabitat: sky\nspeed: 0..0\nsleeps: 0.5\n--- art\nZ\n"});
  World world(pack, pack.scene("t"), 100, 30, 8);
  world.setPhase(0.0);
  int asleep = 0;
  for (auto& e : world.entities()) asleep += world.isAsleep(*e);
  CHECK(asleep > 20 && asleep < 60);  // about half of 80
  world.setPhase(0.5);
  for (auto& e : world.entities()) CHECK(!world.isAsleep(*e));
}

TEST(sleeping_animals_stay_put_are_not_hunted_and_scenes_without_a_cycle_ignore_it) {
  AssetPack pack = cyclePack(
      "--- population\nsleeper count 1\ncat count 1\n",
      {"name: sleeper\nhabitat: ground\nspeed: 5..5\nsleeps: 1\ntag: prey\nbehaviors: walk\n--- art\nZ\n",
       "name: cat\nhabitat: ground\nspeed: 0..0\nmouth-row: 0\nbehaviors: hunt\n--- art\nC\n"});
  World world(pack, pack.scene("t"), 100, 30, 3);
  Entity* sleeper = nullptr;
  Entity* cat = nullptr;
  for (auto& e : world.entities()) (e->tag == "prey" ? sleeper : cat) = e.get();
  CHECK(sleeper && cat);
  world.setTimeMode(World::TimeMode::Night);
  sleeper->x = 40;
  sleeper->vx = 5;
  sleeper->mirrored = false;
  cat->x = 41;  // right next to it
  cat->y = sleeper->y;
  for (int i = 0; i < 20; ++i) world.step(0.05);
  CHECK(sleeper->x == 40);             // frozen in its sleep
  CHECK(world.eatenCount() == 0);      // and not caught by a hunter
  world.setTimeMode(World::TimeMode::Day);
  sleeper->x = 40;
  for (int i = 0; i < 10; ++i) world.step(0.05);
  CHECK(sleeper->x > 40 || world.eatenCount() > 0);  // awake: it walks (or is caught)

  AssetPack flat = miniPack("medium: no\nsky-fraction: 1\n--- population\nsleeper count 10\n",
                            {"name: sleeper\nhabitat: sky\nspeed: 0..0\nsleeps: 1\n--- art\nZ\n"});
  World noCycle(flat, flat.scene("t"), 100, 30, 3);
  for (auto& e : noCycle.entities()) CHECK(!noCycle.isAsleep(*e));  // no cycle, no night
}

TEST(most_aquarium_and_forest_animals_sleep_at_night_but_not_by_day) {
  AssetPack pack = AssetPack::embedded();
  for (const char* name : {"aquarium", "forest"}) {
    World world(pack, pack.scene(name), 400, 36, 2);
    auto species = [](const Entity& e) { return e.sprite->props().num("sleeps", 0) > 0; };
    size_t total = 0;
    for (auto& e : world.entities()) total += species(*e);
    CHECK(total > 20);
    world.setPhase(0.5);
    size_t sleeping = 0;
    for (auto& e : world.entities()) sleeping += (species(*e) && world.isAsleep(*e));
    CHECK(sleeping == 0);  // everyone is up at noon
    world.setPhase(0.0);
    for (auto& e : world.entities()) sleeping += (species(*e) && world.isAsleep(*e));
    CHECK(sleeping * 100 >= total * 50);   // most are asleep at midnight
    CHECK(sleeping * 100 <= total * 90);   // but a few stay up
  }
}

TEST(sky_creatures_with_an_altitude_hover_just_above_the_ground) {
  AssetPack pack = cyclePack("--- population\nmoth count 40\n",
                             {"name: moth\nhabitat: sky\naltitude: 3\nspeed: 0..0\n--- art\nm\nm\n"});
  World world(pack, pack.scene("t"), 80, 30, 5);
  for (auto& e : world.entities()) {
    if (e->sprite->name() != "moth") continue;
    CHECK(e->y + e->height() <= world.surfaceY());
    CHECK(e->y + e->height() >= world.surfaceY() - 3);
  }
}

TEST(every_scene_renders_at_every_time_of_day_and_size) {
  AssetPack pack = AssetPack::embedded();
  for (const auto& sc : pack.scenes()) {
    for (int w : {1, 3, 20, 120}) {
      for (int h : {1, 3, 8, 30}) {
        World world(pack, sc, w, h, 9);
        Frame f;
        for (int hour = 0; hour < 24; hour += 3) {
          world.setHour(hour);
          for (int i = 0; i < 20; ++i) world.step(0.05);
          world.render(f, Style::Full);
          world.render(f, Style::Mono);
          CHECK(f.width() == w && f.height() == h);
        }
      }
    }
  }
}

TEST(menu_options_can_be_replaced_per_scene) {
  Menu m("T");
  m.addChoice("time", "Time", {"a", "b", "c"}, 2);
  m.setOptions("time", {"only"}, 2);
  CHECK(m.value("time") == 0);  // clamped into the new options
  m.setOptions("time", {"x", "y", "z"}, 1);
  CHECK(m.value("time") == 1);
  m.setOptions("time", {}, 0);   // an empty list is ignored
  m.setOptions("nope", {"q"}, 0);
  CHECK(m.value("time") == 1);
  m.open();
  Frame f(60, 20);
  m.render(f, Style::Full);
  CHECK(f.toText().find("< y") != std::string::npos);
}

TEST(backing_darkens_the_background_behind_glyphs_in_full_style_only) {
  AssetPack pack = miniPack("medium: no\nsky-fraction: 1\nground-rows: 4\nsky: #c8c8c8 #c8c8c8\n--- population\nmark count 1\n",
                            {"name: mark\nhabitat: sky\nspeed: 0..0\nbacking: 0.5\ncolor: #ffffff\n--- art\nM M\n"});
  World world(pack, pack.scene("t"), 40, 20, 1);
  Entity* e = world.entities()[0].get();
  e->x = 10;
  e->y = 3;
  Frame f;
  world.render(f, Style::Full);
  const Rgb sky = f.at(5, 3).bg;  // untouched background
  CHECK(f.at(10, 3).bg.r < sky.r && f.at(10, 3).bg.r >= sky.r / 2 - 2);  // about half as bright
  CHECK(f.at(12, 3).bg == f.at(10, 3).bg);                                 // behind every glyph
  CHECK(f.at(11, 3).bg == sky);                                            // but not in the gap between them
  world.render(f, Style::Terminal);
  CHECK(f.at(10, 3).defaultBg);  // the terminal styles keep the user's own background
}

// -------------------------------------------------------------------- impact ---

static AssetPack impactPack(const std::string& extraSprites = "") {
  std::vector<std::string> sprites = {
      "name: meteorite\nfacing: right\ntag: meteorite\nbehaviors: meteorite\n--- art\n*\n",
      "name: flash\nfps: 9\nloop: no\ndie-after-animation: yes\n--- art\n*\n--- art\n+\n",
      "name: debris\nrandom-frame: yes\nbehaviors: ballistic\n--- art\n.\n--- art\n,\n",
      "name: dust\nttl: 2..3\nbehaviors: ballistic\n--- art\n:\n",
      "name: hut\nhabitat: ground\ntag: hut\n--- art\n[##]\n",
      "name: walker\nhabitat: ground\ntag: walker\nspeed: 0..0\nbehaviors: walk\ndestructible: no\n--- art\noo\n",
      "name: victim\nhabitat: ground\ntag: victim\nspeed: 0..0\nbehaviors: walk\n--- art\nxx\n"};
  if (!extraSprites.empty()) sprites.push_back(extraSprites);
  return miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 4\ngravity: 3\ndust-settle: 10\n"
      "action: impact meteorite\n--- population\nwalker count 1\nvictim count 1\n--- decor\nhut random 0\n",
      sprites);
}

static Entity* findTagged(World& w, const std::string& tag) {
  for (auto& e : w.entities()) {
    if (!e->dead && e->tag == tag) return e.get();
  }
  return nullptr;
}

TEST(impact_digs_a_crater_wrecks_the_ground_and_dust_refills_it) {
  AssetPack pack = impactPack();
  World world(pack, pack.scene("t"), 60, 30, 3);
  Entity* hut = findTagged(world, "hut");
  CHECK(hut != nullptr);
  hut->x = 40;
  findTagged(world, "victim")->x = 3;  // well outside the blast
  findTagged(world, "walker")->x = 3;
  const double cx = hut->x + hut->width() / 2.0;
  const int col = static_cast<int>(cx);
  const size_t before = world.entityCount();

  world.impactAt(cx);
  world.step(0.05);
  CHECK(findTagged(world, "hut") == nullptr);   // wrecked, and decor does not come back
  CHECK(world.destroyedCount() == 1);           // only the hut was in the blast
  CHECK(world.deepestCrater() >= 2.5);
  CHECK(world.craterRows(col) >= 3);
  CHECK(world.craterRows(col - 20) == 0);       // only the blast area is dug out
  CHECK(world.entityCount() > before + 20);     // debris, dust and the flash

  Frame f;
  world.render(f);
  CHECK(f.at(col, world.seabedY()).ch == U' ');             // open sky where the ground was
  CHECK(f.at(col - 20, world.seabedY()).bg == (Rgb{150, 124, 82}));  // intact ground elsewhere

  for (int i = 0; i < 1000; ++i) world.step(0.05);  // 50 s, dust settles one row per 10 s
  CHECK(world.deepestCrater() == 0);
  CHECK(world.craterRows(col) == 0);
  CHECK(findTagged(world, "hut") == nullptr);
}

TEST(wrecked_creatures_respawn_and_indestructible_ones_survive) {
  AssetPack pack = impactPack();
  World world(pack, pack.scene("t"), 60, 30, 3);
  Entity* victim = findTagged(world, "victim");
  Entity* walker = findTagged(world, "walker");
  victim->x = 28;
  walker->x = 30;
  world.impactAt(30);
  world.step(0.05);
  CHECK(world.destroyedCount() >= 1);
  CHECK(world.countTag("walker") == 1 && !walker->dead);  // destructible: no
  CHECK(world.countTag("victim") == 1);                   // a replacement walked back in
  Entity* again = findTagged(world, "victim");  // the old one is freed: only inspect the new one
  CHECK(again != nullptr && (again->x < 0 || again->x >= world.width()));  // re-enters from off screen
}

TEST(crater_fills_gradually_not_all_at_once) {
  AssetPack pack = impactPack();  // dust settles one row per 10 s
  World world(pack, pack.scene("t"), 60, 30, 3);
  world.impactAt(30);
  const int deepStart = world.craterRows(30);
  auto width = [&] {
    int n = 0;
    for (int x = 0; x < 60; ++x) n += world.craterRows(x) > 0;
    return n;
  };
  const int widthStart = width();
  CHECK(deepStart == 3);
  for (int i = 0; i < 300; ++i) world.step(0.05);  // 15 s: one and a half rows of dust
  CHECK(world.craterRows(30) < deepStart);         // shallower...
  CHECK(world.craterRows(30) > 0);                 // ...but not gone yet
  CHECK(width() < widthStart);                     // the shallow rim fills first
  CHECK(width() > 0);
}

TEST(walkers_dip_into_craters_and_climb_out_again) {
  AssetPack pack = impactPack();
  World world(pack, pack.scene("t"), 60, 30, 3);
  Entity* w = findTagged(world, "walker");
  CHECK(w != nullptr);
  w->x = 30 - w->width() / 2.0;
  world.impactAt(30);
  world.step(0.001);
  CHECK(world.craterRows(30) >= 3);
  CHECK(w->y == w->anchorY + world.craterRows(30));
  CHECK(w->y > w->anchorY);
  for (int i = 0; i < 1000; ++i) world.step(0.05);
  CHECK(w->y == w->anchorY);
}

TEST(interact_sends_one_meteorite_that_strikes_the_ground) {
  AssetPack pack = impactPack();
  World world(pack, pack.scene("t"), 80, 30, 9);
  CHECK(world.interact() == 1);
  CHECK(world.countTag("meteorite") == 1);
  CHECK(world.interact() == 0);  // already one on its way
  bool struck = false;
  for (int i = 0; i < 200 && !struck; ++i) {
    world.step(0.05);
    struck = world.deepestCrater() > 0;
  }
  CHECK(struck);
  CHECK(world.countTag("meteorite") == 0);
  CHECK(world.interact() == 1);  // ready for the next one
}

TEST(impacts_are_safe_at_the_edges_and_on_tiny_worlds) {
  AssetPack pack = impactPack();
  for (int w : {1, 3, 12, 60}) {
    for (int h : {1, 4, 9, 30}) {
      World world(pack, pack.scene("t"), w, h, 2);
      world.impactAt(-5);
      world.impactAt(0);
      world.impactAt(w - 1);
      world.impactAt(w + 10);
      world.interact();
      for (int i = 0; i < 100; ++i) world.step(0.05);
      Frame f;
      world.render(f);
      CHECK(f.width() == w && f.height() == h);
    }
  }
}

TEST(menu_labels_can_change_with_the_scene_action) {
  Menu m = sampleMenu();
  m.setLabel("quit", "Meteor strike");
  m.open();
  Frame f(80, 24);
  m.render(f, Style::Full);
  CHECK(f.toText().find("Meteor strike") != std::string::npos);
  CHECK(f.toText().find("Quit") == std::string::npos);
}

// ------------------------------------------------------------------ terminal ---

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


TEST(vt_default_colors_and_reverse_use_terminal_theme) {
  Frame a(4, 1), b(4, 1);
  for (int x = 0; x < 4; ++x) { b.at(x, 0).defaultFg = b.at(x, 0).defaultBg = true; }
  b.at(1, 0).reverse = true;
  b.at(1, 0).ch = U'X';
  const std::string d = encodeFrame(&a, b, ColorMode::TrueColor);
  CHECK(d.find("39") != std::string::npos && d.find("49") != std::string::npos);
  CHECK(d.find("38;2") == std::string::npos && d.find("48;2") == std::string::npos);
  CHECK(d.find(";7") != std::string::npos || d.find("[7") != std::string::npos);
}

TEST(vt_ansi16_uses_named_colors_only_and_never_black_text) {
  Frame a(3, 1), b(3, 1);
  b.at(0, 0).ch = U'a'; b.at(0, 0).fg = {255, 0, 0};
  b.at(1, 0).ch = U'b'; b.at(1, 0).fg = {70, 70, 85};   // dark: must not become invisible black
  b.at(2, 0).ch = U'c'; b.at(2, 0).fg = {240, 240, 240};
  const std::string d = encodeFrame(&a, b, ColorMode::Ansi16);
  CHECK(d.find("38;") == std::string::npos);  // no extended color sequences
  CHECK(d.find("91") != std::string::npos);   // bright red
  CHECK(d.find("90") != std::string::npos);   // bright black (dark gray) for the dark color
  CHECK(d.find("30") == std::string::npos);   // plain black is never used for text
}

TEST(rgb_to_ansi16_maps_to_named_colors) {
  CHECK(rgbToAnsi16({255, 0, 0}) == 9);
  CHECK(rgbToAnsi16({0, 255, 255}) == 14);
  CHECK(rgbToAnsi16({255, 255, 255}) == 15);
  CHECK(rgbToAnsi16({0, 0, 0}) != 0);
  CHECK(rgbToAnsi16({70, 70, 85}) == 8);
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
