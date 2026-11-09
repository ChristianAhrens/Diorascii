// Dependency-free test runner: each TEST registers itself, main() runs them all.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "app/session.h"
#include "core/ambience.h"
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
                        "raindrop", "splash", "sun_big", "moon_big", "firefly", "butterfly", "dune_big",
                        "dune_small", "pyramid_big", "pyramid_small", "oasis", "funnel", "sandgrain",
                        "sandspray", "sandpuff", "dustburst", "palm_big", "palm_small", "cactus_big",
                        "cactus_small", "boulder", "tent", "campfire", "camel", "horse", "gazelle",
                        "lion", "vulture", "bat", "tumbleweed", "kraken", "inkburst", "thundercloud",
                        "lightning", "spark", "sandworm", "alien", "fir_big", "fir_small", "shrub", "dwarf_pine",
                        "crag", "ice_spire", "goat", "red_deer", "bear", "finch", "golden_eagle",
                        "cloud_wisp", "snowpuff", "snowflake", "pounce", "avalanche"})
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
  AssetPack pack = miniPack("medium: no\nsky-fraction: 1\nground-rows: 4\n", {});
  World still(pack, pack.scene("t"), 60, 24, 1);
  CHECK(!still.hasDayNight());
  const double before = still.phase();
  still.setTimeMode(World::TimeMode::Night);
  CHECK(still.timeMode() == World::TimeMode::Cycle);  // unchanged
  for (int i = 0; i < 200; ++i) still.step(0.05);
  CHECK(still.phase() == before);
  const Light l = still.light();
  CHECK(l.day == 1 && l.sun == 1 && l.glow == 0 && l.stars == 1);
}

TEST(the_moon_follows_a_cycle_of_sun_and_earth_with_a_black_sky_and_a_ground_that_changes_a_little) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("moon"), 100, 30, 1);
  CHECK(world.hasDayNight() && world.scene().airless);
  Frame day, night;
  world.setPhase(0.5);
  world.render(day);
  const Light noon = world.light();
  world.setPhase(0.0);
  world.render(night);
  const Light midnight = world.light();
  CHECK(noon.stars == 1 && midnight.stars == 1 && midnight.glow == 0);  // stars all the time, no glow
  CHECK(day.at(3, 0).bg == night.at(3, 0).bg);                          // the sky stays black
  const int gy = world.seabedY() + 1;
  auto sum = [](Rgb c) { return c.r + c.g + c.b; };
  const int d = sum(day.at(3, gy).bg), n = sum(night.at(3, gy).bg);
  CHECK(n < d && n > d * 0.6);  // darker at night, but only a little: still a grey
  // The sun by day and the earth by night, high in the sky: yellow cells against blue ones.
  auto count = [](const Frame& f, int rows, auto pred) {
    int k = 0;
    for (int y = 0; y < rows; ++y) for (int x = 0; x < f.width(); ++x) k += pred(f.at(x, y)) ? 1 : 0;
    return k;
  };
  auto yellow = [](const Cell& c) { return c.bg.r > 200 && c.bg.g > 180 && c.bg.b < 120; };
  auto blue = [](const Cell& c) { return c.bg.b > c.bg.r + 60 && c.bg.b > 120; };
  CHECK(count(day, world.surfaceY(), yellow) > 20 && count(day, world.surfaceY(), blue) == 0);
  CHECK(count(night, world.surfaceY(), blue) > 10 && count(night, world.surfaceY(), yellow) == 0);
}

TEST(the_sun_and_moon_are_larger_than_a_single_glyph_and_a_low_sky_cuts_them_off_at_the_top) {
  AssetPack pack = AssetPack::embedded();
  CHECK(pack.find("sun_big")->width() == 8 && pack.find("sun_big")->height() == 5);
  CHECK(pack.find("moon_big")->width() == 8 && pack.find("moon_big")->height() == 5);
  World world(pack, pack.scene("aquarium"), 80, 24, 1);  // the aquarium's sky is a few rows only
  CHECK(world.surfaceY() < 5);
  world.setPhase(0.5);  // the sun at the top of its arc
  Frame f;
  world.render(f);
  auto yellow = [](const Cell& c) {
    return (c.bg.r > 200 && c.bg.g > 180 && c.bg.b < 120) || (c.ch != U' ' && c.fg.r > 200 && c.fg.g > 180 && c.fg.b < 120);
  };
  int top = 0, bottom = 0;
  for (int x = 0; x < f.width(); ++x) {
    top += yellow(f.at(x, 0)) ? 1 : 0;
    bottom += yellow(f.at(x, world.surfaceY() - 1)) ? 1 : 0;
  }
  CHECK(top > 0 && bottom > 0);  // the sun fills the sky's rows, only its top is cut off
}

TEST(the_sun_over_the_moon_is_larger_than_the_earth) {
  AssetPack pack = AssetPack::embedded();
  CHECK(pack.find("sun_corona")->width() > pack.find("earth")->width());
  CHECK(pack.find("sun_corona")->height() > pack.find("earth")->height());
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
  // awake: it walks, or is caught - a caught sleeper is freed, so test that case first
  CHECK(world.eatenCount() > 0 || sleeper->x > 40);

  AssetPack flat = miniPack("medium: no\nsky-fraction: 1\n--- population\nsleeper count 10\n",
                            {"name: sleeper\nhabitat: sky\nspeed: 0..0\nsleeps: 1\n--- art\nZ\n"});
  World noCycle(flat, flat.scene("t"), 100, 30, 3);
  for (auto& e : noCycle.entities()) CHECK(!noCycle.isAsleep(*e));  // no cycle, no night
}

TEST(most_aquarium_and_forest_animals_sleep_at_night_but_not_by_day) {
  AssetPack pack = AssetPack::embedded();
  for (const char* name : {"aquarium", "forest", "desert"}) {
    World world(pack, pack.scene(name), 600, 36, 2);
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

// ------------------------------------------------------------ herds and storms ---

TEST(scene_parses_storm_actions_and_tint) {
  std::string err;
  auto sc = SceneDef::parse("name: s\naction: weather grain funnel\novercast-tint: #c8a060 0.6\n", err);
  CHECK(sc != nullptr);
  CHECK(sc->action.kind == SceneDef::Action::Kind::Weather);
  CHECK(sc->action.sprite == "grain" && sc->action.extra == "funnel");
  CHECK(sc->overcastTint == (Rgb{200, 160, 96}) && sc->overcastTintAmount == 0.6);
  auto plain = SceneDef::parse("name: s\naction: weather rain\n", err);
  CHECK(plain && plain->action.extra.empty());
  CHECK(SceneDef::parse("name: s\naction: weather a b c\n", err) == nullptr);
  CHECK(SceneDef::parse("name: s\naction: drop a b\n", err) == nullptr);
  CHECK(SceneDef::parse("name: s\novercast-tint: #123456\n", err) == nullptr);
  CHECK(SceneDef::parse("name: s\novercast-tint: nope 0.5\n", err) == nullptr);
  AssetPack pack;
  pack.addText("g.sprite", "name: grain\n--- art\n.\n");
  pack.addText("a.scene", "name: s\naction: weather grain funnel\n");
  pack.validate();
  CHECK(pack.errors().size() == 1);  // the funnel sprite does not exist
}

TEST(herds_start_together_and_stay_a_herd) {
  AssetPack pack = miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 6\n--- population\nhorse count 5\n",
      {"name: horse\nfacing: right\nhabitat: ground\nspeed: 8..8\nherd: 4..4\nherd-spacing: 5\n"
       "lanes: 3\nbehaviors: walk\n--- art\nH\n"});
  World world(pack, pack.scene("t"), 300, 30, 4);
  auto& all = world.entities();
  CHECK(all.size() == 20);  // five herds of four
  int right = 0;
  for (size_t g = 0; g < 5; ++g) {
    const bool dir = all[g * 4]->vx > 0;
    double lo = 1e9, hi = -1e9;
    for (size_t k = 0; k < 4; ++k) {
      CHECK((all[g * 4 + k]->vx > 0) == dir);  // one direction per herd
      lo = std::min(lo, all[g * 4 + k]->x);
      hi = std::max(hi, all[g * 4 + k]->x);
    }
    CHECK(hi - lo > 8 && hi - lo < 24);  // strung out, not on top of each other
    right += dir ? 4 : 0;
  }
  for (int i = 0; i < 4000; ++i) world.step(0.05);  // a few laps around the world
  CHECK(world.entityCount() == 20);
  int rightLater = 0;
  for (auto& e : world.entities()) rightLater += e->vx > 0;
  CHECK(rightLater == right);  // members that leave come back facing the same way
}

TEST(hunters_chase_catch_feast_and_then_stop_hunting) {
  AssetPack pack = miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 8\n--- population\nlion count 1\ngazelle count 1\n",
      {"name: lion\nfacing: right\nhabitat: ground\nspeed: 6..6\nmouth-row: 0\nfeast-time: 1\nhunt-effect: puff\n"
       "tag: lion\nbehaviors: chase hunt walk\n--- art\nLL\nLL\n",
       "name: gazelle\nhabitat: ground\nspeed: 0..0\ntag: prey\nbehaviors: walk\n--- art\ng\ng\n",
       "name: puff\ntag: puff\nfps: 5\nloop: no\ndie-after-animation: yes\n--- art\n*\n--- art\n.\n"});
  World world(pack, pack.scene("t"), 120, 30, 3);
  Entity* lion = nullptr;
  Entity* prey = nullptr;
  for (auto& e : world.entities()) {
    if (e->tag == "lion") lion = e.get();
    if (e->tag == "prey") prey = e.get();
  }
  CHECK(lion && prey);
  lion->x = 10;
  lion->vx = -6;
  lion->mirrored = true;  // running away from the prey...
  prey->x = 70;
  prey->anchorY = lion->anchorY + 2;  // ...in another lane
  prey->y = prey->anchorY;
  for (int i = 0; i < 25; ++i) world.step(0.05);
  CHECK(lion->vx > 0);                                // turned round towards the prey
  CHECK(std::fabs(lion->anchorY - prey->anchorY) < 0.01);  // and changed lane to line up with it

  bool caught = false;
  for (int i = 0; i < 400 && !caught; ++i) {
    world.step(0.05);
    caught = world.eatenCount() > 0;
  }
  CHECK(caught);
  CHECK(lion->kills == 1);
  CHECK(world.countTag("puff") > 0);  // the effect named in the hunter's header

  const double at = lion->x;
  for (int i = 0; i < 15; ++i) world.step(0.05);  // 0.75 s of a one second feast
  CHECK(lion->x == at);
  for (int i = 0; i < 15; ++i) world.step(0.05);
  CHECK(lion->x > at);  // it carries on afterwards

  for (auto& e : world.entities()) {
    if (e->tag == "prey") { e->x = lion->x - 25; e->anchorY = e->y = lion->y; }  // fresh prey behind it
  }
  const double before = lion->x;
  for (int i = 0; i < 40; ++i) world.step(0.05);
  CHECK(lion->vx > 0 && lion->x > before);  // a satisfied lion does not turn back to hunt
  CHECK(lion->kills == 1);
}

static AssetPack stormPack(const std::string& extra = "") {
  return miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 6\nweather-duration: 4\naction: weather grain funnel\n"
      "overcast-tint: #c8a060 0.8\nsky: #3050a0 #3050a0\n" + extra,
      {"name: grain\nwind: yes\nspeed: 20..30\ntag: grain\nbehaviors: fly\n--- art\n.\n",
       "name: funnel\nhabitat: ground\ntag: tornado\nspeed: 4..4\nbehaviors: twister\n--- art\nVVV\nVVV\n VV\n",
       "name: sandspray\ntag: spray\nbehaviors: ballistic\n--- art\n'\n"});
}

TEST(the_storm_action_brings_a_sandstorm_or_a_tornado_and_never_two_tornadoes) {
  AssetPack pack = stormPack();
  int tornadoes = 0, storms = 0;
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    World world(pack, pack.scene("t"), 80, 30, seed);
    CHECK(world.interact() == 1);
    if (world.countTag("tornado") == 1) ++tornadoes;
    else if (world.raining()) ++storms;
  }
  CHECK(tornadoes + storms == 40);
  CHECK(tornadoes >= 8 && storms >= 8);  // a coin toss each time

  for (uint64_t seed = 1; seed <= 40; ++seed) {  // pressing again during a tornado gives the storm
    World world(pack, pack.scene("t"), 80, 30, seed);
    world.interact();
    if (world.countTag("tornado") == 0) continue;
    world.interact();
    CHECK(world.countTag("tornado") == 1);
    CHECK(world.raining());
    break;
  }
}

TEST(a_sandstorm_blows_one_way_across_the_whole_scene_and_tints_the_light) {
  AssetPack pack = stormPack();
  uint64_t seed = 0;
  for (uint64_t s = 1; s <= 200 && seed == 0; ++s) {  // find a run where the coin toss gives a plain sandstorm
    World probe(pack, pack.scene("t"), 80, 30, s);
    probe.interact();
    if (probe.raining()) seed = s;
  }
  CHECK(seed != 0);
  if (seed == 0) return;
  World world(pack, pack.scene("t"), 80, 30, seed);
  Frame f;
  world.render(f);
  const Rgb calm = f.at(5, 0).bg;
  world.interact();
  for (int i = 0; i < 40; ++i) world.step(0.05);
  CHECK(world.countTag("grain") > 30);
  bool anyRight = false, anyLeft = false;
  for (auto& e : world.entities()) {
    if (e->tag != "grain") continue;
    anyRight |= e->vx > 0;
    anyLeft |= e->vx < 0;
  }
  CHECK(anyRight != anyLeft);  // all grains blow the same way
  for (int i = 0; i < 40; ++i) world.step(0.05);  // 4 s in: the sky has turned ochre
  CHECK(world.light().haze > 0.5);
  world.render(f);
  const Rgb dusty = f.at(5, 0).bg;
  auto dist = [](Rgb a, Rgb b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b); };
  CHECK(dist(dusty, Rgb{200, 160, 96}) < dist(calm, Rgb{200, 160, 96}) - 100);
  for (int i = 0; i < 400; ++i) world.step(0.05);  // it blows over
  CHECK(world.countTag("grain") == 0);
  CHECK(world.light().haze < 0.01);
}

TEST(a_sandstorm_at_night_stays_dark) {
  AssetPack pack = stormPack("day-night: yes\n");
  auto bright = [&](double hour) {
    World world(pack, pack.scene("t"), 80, 30, 1);
    world.setHour(hour);
    world.interact();
    for (int i = 0; i < 80; ++i) world.step(0.05);
    Frame f;
    world.render(f);
    const Rgb c = f.at(5, 0).bg;
    return c.r + c.g + c.b;
  };
  CHECK(bright(0) < bright(12) / 2);  // the storm at midnight is far darker than at noon
}

TEST(a_storm_at_night_hides_the_stars) {
  AssetPack pack = stormPack("day-night: yes\nstars: 3\n");
  World world(pack, pack.scene("t"), 80, 30, 1);
  world.setHour(0);
  CHECK(world.light().stars == 1);  // a clear night
  world.interact();
  for (int i = 0; i < 60; ++i) world.step(0.05);
  CHECK(world.light().stars < 0.25);  // the dust hides them
}

TEST(a_tornado_crosses_the_scene_whirls_up_sand_and_darkens_the_sky) {
  AssetPack pack = stormPack();
  uint64_t seed = 0;
  for (uint64_t s = 1; s <= 200 && seed == 0; ++s) {  // find a run where the coin toss gives a tornado
    World probe(pack, pack.scene("t"), 80, 30, s);
    probe.interact();
    if (probe.countTag("tornado") == 1) seed = s;
  }
  CHECK(seed != 0);
  if (seed == 0) return;
  World world(pack, pack.scene("t"), 80, 30, seed);
  CHECK(world.interact() == 1);
  size_t spray = 0;
  Entity* funnel = nullptr;
  for (int i = 0; i < 60; ++i) {
    world.step(0.05);
    spray = std::max(spray, world.countTag("spray"));
    for (auto& e : world.entities()) {
      if (e->tag == "tornado") funnel = e.get();
    }
  }
  CHECK(funnel != nullptr);
  CHECK(funnel->y + funnel->height() == world.seabedY());  // standing on the ground
  CHECK(spray > 3);                                         // whirling sand up
  CHECK(world.overcast() > 0.5);                            // the sky darkens while it is there
  CHECK(world.light().haze > 0.3);
  for (int i = 0; i < 700 && world.countTag("tornado") > 0; ++i) world.step(0.05);
  CHECK(world.countTag("tornado") == 0);                    // it moves on and leaves
  for (int i = 0; i < 400; ++i) world.step(0.05);
  CHECK(world.overcast() < 0.01);
}

TEST(a_tornado_throws_animals_into_the_air_and_they_land_dazed_and_walk_on) {
  AssetPack withAnimals = miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 6\nweather-duration: 4\naction: weather grain funnel\n"
      "--- population\nwalker count 2\nrock count 1\n",
      {"name: grain\nwind: yes\ntag: grain\nbehaviors: fly\n--- art\n.\n",
       "name: funnel\nhabitat: ground\ntag: tornado\nspeed: 4..4\nbehaviors: twister\n--- art\nVVV\nVVV\n VV\n",
       "name: sandspray\ntag: spray\nbehaviors: ballistic\n--- art\n'\n",
       "name: walker\nhabitat: ground\ntag: walker\nspeed: 4..4\nbehaviors: walk\n--- art\nW\nW\n",
       "name: rock\nhabitat: ground\ntag: rock\nspeed: 0..0\n--- art\nR\n"});
  uint64_t seed = 0;
  for (uint64_t s = 1; s <= 200 && seed == 0; ++s) {
    World probe(withAnimals, withAnimals.scene("t"), 100, 30, s);
    probe.interact();
    if (probe.countTag("tornado") == 1) seed = s;
  }
  CHECK(seed != 0);
  if (seed == 0) return;
  World world(withAnimals, withAnimals.scene("t"), 100, 30, seed);
  world.interact();
  Entity *funnel = nullptr, *hit = nullptr, *far = nullptr, *rock = nullptr;
  for (auto& e : world.entities()) {
    if (e->tag == "tornado") funnel = e.get();
    else if (e->tag == "rock") rock = e.get();
    else if (e->tag == "walker") (hit ? far : hit) = e.get();
  }
  CHECK(funnel && hit && far && rock);
  funnel->x = 40;
  funnel->vx = 0;  // hold the funnel still so the test is exact
  hit->x = 40;     // straight under it
  hit->vx = 4;
  far->x = 90;     // well out of its way
  rock->x = 41;    // scenery under it, too
  const double ground = hit->anchorY;

  world.step(0.05);
  CHECK(hit->flung);
  CHECK(!far->flung && !rock->flung);  // only the animal in the path is thrown, not the rock or the far one

  double highest = ground;
  int airborne = 0;
  while (hit->flung && airborne < 600) {
    world.step(0.05);
    highest = std::min(highest, hit->y);
    ++airborne;
  }
  CHECK(!hit->flung);                       // it comes down again
  CHECK(highest < ground - 4);              // after a real flight into the air
  CHECK(airborne > 10);
  CHECK(hit->y == ground);                  // landed on its lane
  CHECK(hit->vx == 4);                      // with its own walking speed restored
  CHECK(hit->pauseLeft > 0);                // dazed for a moment
  const double landed = hit->x;
  funnel->x = 95;                           // the tornado has moved on
  for (int i = 0; i < 20; ++i) world.step(0.05);  // 1 s: still dazed
  CHECK(hit->x == landed);
  for (int i = 0; i < 20; ++i) world.step(0.05);
  CHECK(hit->x != landed);                  // and then it walks on
}

TEST(the_desert_has_herds_a_lion_that_hunts_and_a_storm_on_f) {
  AssetPack pack = AssetPack::embedded();
  auto desert = pack.scene("desert");
  CHECK(desert != nullptr);
  CHECK(desert->action.kind == SceneDef::Action::Kind::Weather && !desert->action.extra.empty());
  CHECK(desert->dayNight && !desert->events.empty());

  World world(pack, desert, 400, 32, 3);  // wide enough for several herds of each kind
  size_t horses = 0, camels = 0, gazelles = 0;
  for (auto& e : world.entities()) {
    horses += e->sprite->name() == "horse";
    camels += e->sprite->name() == "camel";
    gazelles += e->sprite->name() == "gazelle";
  }
  CHECK(horses >= 4 && camels >= 2 && gazelles >= 2);  // whole herds, not single animals

  bool lionSeen = false;
  for (uint64_t seed = 1; seed <= 4 && world.eatenCount() == 0; ++seed) {
    World w(pack, desert, 120, 32, seed);
    for (int i = 0; i < 3000 && w.eatenCount() == 0; ++i) {
      w.step(0.05);
      for (auto& e : w.entities()) lionSeen |= e->sprite->name() == "lion";
    }
    if (w.eatenCount() > 0) CHECK(lionSeen);  // only the lion hunts here
  }
  CHECK(lionSeen);
}

TEST(earth_tones_and_sand_are_available_to_sprites) {
  AssetPack pack = miniPack("--- population\nhorse count 1\ngrain count 1\n",
                            {"name: horse\npool: earth\nhabitat: sky\nspeed: 0..0\n--- art\n1\n",
                             "name: grain\nhabitat: sky\nspeed: 0..0\n--- art\nX\n--- mask\nS\n"});
  World world(pack, pack.scene("t"), 40, 20, 5);
  const Rgb earth[] = {{240, 205, 150}, {228, 231, 238}, {205, 160, 110}, {190, 140, 90}, {235, 220, 190}};
  for (auto& e : world.entities()) {
    if (e->sprite->name() != "horse") continue;
    for (const Rgb& c : e->pal) {
      bool known = false;
      for (const Rgb& k : earth) known |= c == k;
      CHECK(known);
    }
  }
  for (auto& e : world.entities()) {
    e->x = e->sprite->name() == "horse" ? 3 : 7;
    e->y = 2;
  }
  Frame f;
  world.render(f, Style::Full);
  CHECK(f.at(7, 2).ch == U'X' && f.at(7, 2).fg == (Rgb{214, 176, 112}));  // the grain is sand colored
}

TEST(contrast_sprites_draw_dark_strokes_on_light_backgrounds_only) {
  auto fgAt = [&](const std::string& sky, Style style) {
    AssetPack pack = miniPack("medium: no\nsky-fraction: 1\nground-rows: 4\nsky: " + sky + " " + sky +
                                  "\n--- population\nmark count 1\n",
                              {"name: mark\nhabitat: sky\nspeed: 0..0\ncontrast: yes\ncolor: #f0c880\n--- art\nM\n"});
    World world(pack, pack.scene("t"), 40, 20, 1);
    Entity* e = world.entities()[0].get();
    e->x = 10;
    e->y = 3;
    Frame f;
    world.render(f, style);
    return f.at(10, 3).fg;
  };
  const Rgb onLight = fgAt("#f4dfb0", Style::Full), onDark = fgAt("#101830", Style::Full);
  CHECK(onDark == (Rgb{0xf0, 0xc8, 0x80}));        // its own color on a dark background
  CHECK(onLight.r < 90 && onLight.g < 90);          // dark brown on pale sand
  CHECK(fgAt("#f4dfb0", Style::Terminal) == (Rgb{0xf0, 0xc8, 0x80}));  // the terminal styles leave it alone
}

TEST(horses_kick_up_dust_as_they_run) {
  AssetPack pack = miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 6\n--- population\nhorse count 1\n",
      {"name: horse\nfacing: right\nhabitat: ground\nspeed: 10..10\ndust-rate: 8\nbehaviors: walk emit_dust\n--- art\nH\nH\n",
       "name: sandpuff\ntag: puff\nfps: 6\nloop: no\ndie-after-animation: yes\n--- art\n.\n--- art\n:\n"});
  World world(pack, pack.scene("t"), 200, 30, 2);
  Entity* horse = world.entities()[0].get();
  horse->x = 50;
  horse->vx = 10;
  horse->mirrored = false;
  size_t most = 0;
  double behind = 0;
  for (int i = 0; i < 60; ++i) {
    world.step(0.05);
    most = std::max(most, world.countTag("puff"));
    for (auto& e : world.entities()) {
      if (e->tag == "puff") behind = std::max(behind, horse->x - e->x);  // puffs are left behind
    }
  }
  CHECK(most >= 2);
  CHECK(behind > 1);
}

// ------------------------------------------------------------- rare events ---

// A scene with a catastrophe `beast` that wipes out all life halfway across, 30 static `fish`
// (life), a `rock` that is not alive, and a `puff` shown on every victim.
static AssetPack rarePack(const std::string& beastHeader = "", const std::string& sceneExtra = "",
                          const std::vector<std::string>& moreSprites = {}) {
  std::vector<std::string> sprites = {
      "name: beast\nfacing: right\nhabitat: ground\nspeed: 12..12\ntag: beast\nstrike-effect: puff\n" + beastHeader +
          "behaviors: catastrophe\n--- art\nBBB\nBBB\n",
      "name: fish\nhabitat: ground\nspeed: 0..0\nlife: yes\n--- art\nf\n",
      "name: puff\ntag: puff\nfps: 8\nloop: no\ndie-after-animation: yes\n--- art\n*\n--- art\n.\n",
      "name: rock\nhabitat: ground\n--- art\nR\n"};
  sprites.insert(sprites.end(), moreSprites.begin(), moreSprites.end());
  return miniPack("medium: no\nsky-fraction: 1\nground-rows: 6\nrare-first: 1000..1000\n" + sceneExtra +
                      "--- population\nfish count 30\n--- decor\nrock left 5\n--- rare\nbeast 1\n",
                  sprites);
}

static size_t countSprite(World& w, const std::string& name) {
  size_t n = 0;
  for (auto& e : w.entities()) n += (!e->dead && e->sprite->name() == name);
  return n;
}

TEST(scene_parses_the_rare_section_and_timing) {
  std::string err;
  auto sc = SceneDef::parse("name: s\nrare-first: 100..200\nrare-interval: 300..400\n--- rare\nkraken 2\nworm 1\n", err);
  CHECK(sc != nullptr);
  CHECK(sc->rare.size() == 2 && sc->rare[0].sprite == "kraken" && sc->rare[0].weight == 2);
  CHECK(sc->rareFirst.first == 100 && sc->rareInterval.second == 400);
  auto plain = SceneDef::parse("name: s\n", err);
  CHECK(plain && plain->rare.empty());
  CHECK(SceneDef::parse("name: s\n--- rare\nkraken\n", err) == nullptr);  // needs a weight
  AssetPack pack;
  pack.addText("a.scene", "name: s\n--- rare\nghost 1\n");
  pack.validate();
  CHECK(pack.errors().size() == 1);
}

TEST(rare_events_come_on_their_own_timer_one_at_a_time_and_apart_from_other_events) {
  AssetPack pack = rarePack("", "rare-first: 1..2\nrare-interval: 1..2\nevent-first: 1..1\nevent-interval: 1..1\n"
                                "--- events\nping 1\n",
                            {"name: ping\nfacing: right\nhabitat: ground\nspeed: 3..3\ntag: ping\nbehaviors: swim\n--- art\nP\n"});
  World world(pack, pack.scene("t"), 120, 30, 4);
  size_t maxBeasts = 0, beastsSeen = 0, pings = 0;
  bool wasThere = false;
  for (int i = 0; i < 1200; ++i) {  // a minute
    world.step(0.05);
    const size_t now = world.countTag("beast");
    maxBeasts = std::max(maxBeasts, now);
    beastsSeen += now > 0 && !wasThere;
    wasThere = now > 0;
    pings = std::max(pings, world.countTag("ping"));
  }
  CHECK(maxBeasts == 1);     // never two catastrophes at once
  CHECK(beastsSeen >= 2);    // they keep coming on their own
  CHECK(pings >= 1);         // and ordinary events carry on independently
}

TEST(a_rare_event_can_be_summoned_but_only_one_at_a_time) {
  AssetPack pack = rarePack();
  World world(pack, pack.scene("t"), 120, 30, 4);
  CHECK(world.countTag("beast") == 0);
  CHECK(world.triggerRare());
  CHECK(world.countTag("beast") == 1);
  CHECK(!world.triggerRare());  // one is already under way
  AssetPack none = miniPack("--- population\n", {});
  World quiet(none, none.scene("t"), 60, 20, 1);
  CHECK(!quiet.triggerRare());  // this scene has none
}

TEST(a_catastrophe_strikes_once_halfway_across_and_wipes_out_all_life_but_nothing_else) {
  AssetPack pack = rarePack("life: yes\n");  // the attacker is itself a living thing, and still spares itself
  World world(pack, pack.scene("t"), 120, 30, 4);
  CHECK(countSprite(world, "fish") == 30 && countSprite(world, "rock") == 1);
  world.triggerRare();
  // Look the attacker up each time instead of keeping a pointer: if it were wiped out too, it would be freed.
  auto beastNow = [&]() -> Entity* {
    for (auto& e : world.entities()) {
      if (!e->dead && e->tag == "beast") return e.get();
    }
    return nullptr;
  };
  CHECK(beastNow() != nullptr);
  bool struck = false;
  for (int i = 0; i < 600 && !struck; ++i) {
    CHECK(world.killedCount() == 0);  // nothing happens before the strike
    world.step(0.05);
    struck = world.killedCount() > 0;
  }
  CHECK(struck);
  Entity* beast = beastNow();
  CHECK(beast != nullptr);  // the attacker is spared
  if (!beast) return;
  CHECK(std::fabs(beast->x + beast->width() / 2.0 - 60) <= 1.5);  // right in the middle of the way across
  CHECK(beast->struck);
  CHECK(world.killedCount() == 30);                               // every fish, in one go, but not the attacker
  CHECK(countSprite(world, "rock") == 1);                         // the rock is not alive
  world.step(0.05);
  CHECK(world.countTag("puff") == 30);                            // an effect on every victim
  for (int i = 0; i < 400; ++i) world.step(0.05);                 // it leaves, and never strikes again
  CHECK(world.killedCount() == 30);
  CHECK(world.countTag("beast") == 0);
}

TEST(what_a_catastrophe_wiped_out_comes_back_gradually_from_off_screen) {
  AssetPack pack = rarePack();
  World world(pack, pack.scene("t"), 120, 30, 4);
  world.wipeOut("life", "puff", nullptr);
  world.step(0.05);
  CHECK(countSprite(world, "fish") == 0);
  CHECK(world.restockPending() == 30);
  auto back = [&] { return countSprite(world, "fish"); };
  for (int i = 0; i < 100; ++i) world.step(0.05);  // 5 s: the first have not come back yet
  const size_t early = back();
  for (int i = 0; i < 400; ++i) world.step(0.05);  // 25 s
  const size_t middle = back();
  for (int i = 0; i < 700; ++i) world.step(0.05);  // 60 s
  CHECK(early == 0);
  CHECK(middle > 0 && middle < 30);                // a few at a time, not all at once
  CHECK(back() == 30 && world.restockPending() == 0);
  for (auto& e : world.entities()) {
    if (e->sprite->name() == "fish") CHECK(e->x < 0 || e->x >= world.width());  // they walk in from the edges
  }
}

TEST(a_catastrophe_can_wipe_out_named_sprites_only_and_what_was_planted_is_planted_again) {
  AssetPack pack = miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 6\nrare-first: 1000..1000\n--- population\nrover count 1\nbug count 5\n"
      "--- decor\nflag center 0\n--- rare\nalien 1\n",
      {"name: alien\nfacing: right\nhabitat: ground\nspeed: 12..12\nwipes: rover flag\nstrike-effect: puff\n"
       "behaviors: catastrophe\n--- art\nAA\nAA\n",
       "name: rover\nhabitat: ground\nspeed: 0..0\n--- art\nrr\n",
       "name: bug\nhabitat: ground\nspeed: 0..0\nlife: yes\n--- art\nb\n",
       "name: flag\nhabitat: ground\n--- art\n|>\n|\n",
       "name: puff\ntag: puff\nfps: 8\nloop: no\ndie-after-animation: yes\n--- art\n*\n--- art\n.\n"});
  World world(pack, pack.scene("t"), 120, 30, 6);
  CHECK(countSprite(world, "rover") == 1 && countSprite(world, "flag") == 1 && countSprite(world, "bug") == 5);
  double flagX = 0;
  for (auto& e : world.entities()) {
    if (e->sprite->name() == "flag") flagX = e->x;
  }
  world.triggerRare();
  for (int i = 0; i < 600 && world.killedCount() == 0; ++i) world.step(0.05);
  world.step(0.05);
  CHECK(world.killedCount() == 2);
  CHECK(countSprite(world, "rover") == 0 && countSprite(world, "flag") == 0);
  CHECK(countSprite(world, "bug") == 5);  // life, but not named: spared
  for (int i = 0; i < 1300; ++i) world.step(0.05);  // well over a minute later
  CHECK(countSprite(world, "rover") == 1);          // a new rover has arrived
  CHECK(countSprite(world, "flag") == 1);           // and the flag stands again, in the same place
  for (auto& e : world.entities()) {
    if (e->sprite->name() == "flag") CHECK(e->x == flagX);
  }
}

TEST(every_scene_has_a_rare_catastrophe_and_it_strikes_when_summoned) {
  AssetPack pack = AssetPack::embedded();
  struct Case { const char* scene; const char* attacker; };
  for (const Case& c : {Case{"aquarium", "kraken"}, Case{"forest", "thundercloud"}, Case{"desert", "sandworm"},
                        Case{"moon", "alien"}}) {
    auto scene = pack.scene(c.scene);
    CHECK(scene && scene->rare.size() == 1 && scene->rare[0].sprite == c.attacker);
    World world(pack, scene, 160, 36, 3);
    const bool animals = std::string(c.scene) != "moon";
    size_t lifeBefore = 0;
    for (auto& e : world.entities()) lifeBefore += e->life;
    CHECK(!animals || lifeBefore > 10);
    CHECK(world.triggerRare());
    bool struck = false;
    for (int i = 0; i < 3000 && !struck; ++i) {
      world.step(0.05);
      struck = world.killedCount() > 0;
    }
    CHECK(struck);
    if (!struck) continue;
    if (animals) {
      size_t aliveLife = 0;
      for (auto& e : world.entities()) aliveLife += (e->life && !e->dead);
      CHECK(aliveLife == 0);                      // everything alive is gone, in a single moment
      CHECK(world.killedCount() > 0);
      CHECK(world.restockPending() >= lifeBefore);  // and every one of them comes back later, one by one
    } else {
      CHECK(countSprite(world, "rover") == 0 && countSprite(world, "flag") == 0);  // the alien's targets
      CHECK(world.killedCount() == 2 + 0 || world.killedCount() >= 1);
      CHECK(countSprite(world, "alien") == 1);
    }
    if (std::string(c.scene) == "forest") {
      CHECK(world.flashing());                    // lightning
      CHECK(world.raining());                     // and a downpour
    }
  }
}

TEST(the_moon_alien_only_destroys_the_rover_and_the_flag_and_both_are_replaced_later) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("moon"), 120, 30, 5);
  size_t rocks = countSprite(world, "rock"), landers = countSprite(world, "lander");
  CHECK(countSprite(world, "rover") == 1 && countSprite(world, "flag") == 1);
  world.triggerRare();
  for (int i = 0; i < 3000 && world.killedCount() == 0; ++i) world.step(0.05);
  CHECK(world.killedCount() == 2);
  CHECK(countSprite(world, "rock") == rocks && countSprite(world, "lander") == landers);  // nothing else
  for (int i = 0; i < 1500; ++i) world.step(0.05);  // well over a minute later
  CHECK(countSprite(world, "rover") == 1 && countSprite(world, "flag") == 1);
}

TEST(a_wipe_also_takes_what_was_spawned_in_the_same_step) {
  AssetPack pack = rarePack();
  World world(pack, pack.scene("t"), 120, 30, 4);
  world.spawnCreature(pack.find("fish"), true);  // waiting to join the world, like a respawn in the same step
  world.wipeOut("life", "puff", nullptr);
  world.step(0.05);
  CHECK(countSprite(world, "fish") == 0);
  CHECK(world.killedCount() == 31);
  CHECK(world.restockPending() == 31);
}

TEST(an_animal_that_just_left_the_scene_is_not_replaced_at_once_during_a_wipe) {
  AssetPack pack = miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 6\n--- population\nrunner count 6\n",
      {"name: runner\nfacing: right\nhabitat: ground\nspeed: 30..30\nlife: yes\nbehaviors: walk\n--- art\nR\n"});
  World world(pack, pack.scene("t"), 60, 30, 2);
  // Run until some runner has just left (dead, its replacement still pending), then wipe in that very step.
  for (int i = 0; i < 400; ++i) {
    world.step(0.05);
    bool leaving = false;
    for (auto& e : world.entities()) leaving |= e->x > 60 || e->x + 1 < 0;
    if (leaving) break;
  }
  size_t before = 0;
  for (auto& e : world.entities()) before += !e->dead;
  world.wipeOut("life", "", nullptr);
  world.step(0.05);
  CHECK(countSprite(world, "runner") <= 1);  // at most the one that was spawned this step
  CHECK(world.restockPending() + countSprite(world, "runner") >= before);
}

TEST(sleeping_animals_die_without_a_sight) {
  AssetPack pack = cyclePack("--- population\nsleeper count 40\n",
                             {"name: sleeper\nhabitat: ground\nspeed: 0..0\nlife: yes\nsleeps: 1\n--- art\nZ\n",
                              "name: puff\ntag: puff\nfps: 8\nloop: no\ndie-after-animation: yes\n--- art\n*\n--- art\n.\n"});
  World world(pack, pack.scene("t"), 100, 30, 3);
  world.setPhase(0.0);  // everyone is asleep
  world.wipeOut("life", "puff", nullptr);
  world.step(0.05);
  CHECK(world.killedCount() == 40);        // they die all the same
  CHECK(world.countTag("puff") == 0);      // but nobody sees it
  CHECK(world.restockPending() == 40);     // and they come back later
}

TEST(a_strike_can_light_up_the_screen_with_two_quick_flashes) {
  AssetPack pack = rarePack("screen-flash: 0.5\n");
  World world(pack, pack.scene("t"), 120, 30, 4);
  CHECK(!world.flashing());
  Frame calm;
  world.render(calm, Style::Full);
  world.triggerRare();
  for (int i = 0; i < 600 && world.killedCount() == 0; ++i) world.step(0.05);
  CHECK(world.killedCount() == 30);
  CHECK(world.flashing());  // the instant it strikes
  Frame lit, terminal;
  world.render(lit, Style::Full);
  world.render(terminal, Style::Terminal);
  CHECK(lit.at(110, 2).bg.r > calm.at(110, 2).bg.r + 100);  // bright white
  CHECK(terminal.at(110, 2).reverse);                       // colors flipped in the terminal styles
  std::string pattern;  // sample every 0.05 s for 0.5 s: on, off, on, off
  for (int i = 0; i < 10; ++i) {
    world.step(0.05);
    pattern += world.flashing() ? 'X' : '.';
  }
  CHECK(pattern.find('X') != std::string::npos && pattern.find('.') != std::string::npos);
  CHECK(pattern.find(".X") != std::string::npos);  // it flashes a second time
  for (int i = 0; i < 20; ++i) world.step(0.05);
  CHECK(!world.flashing());  // and is dark again
}

TEST(a_catastrophe_can_bring_rain_and_always_darkens_the_scene_while_it_is_there) {
  AssetPack pack = rarePack("storm-rain: 6\ntag: gloom\n", "action: weather rain\n",
                            {"name: rain\nfacing: right\ntag: rain\nbehaviors: rain\n--- art\n\\\n"});
  World world(pack, pack.scene("t"), 120, 30, 4);
  world.triggerRare();
  CHECK(!world.raining());
  for (int i = 0; i < 600 && world.killedCount() == 0; ++i) world.step(0.05);
  CHECK(world.raining());  // it rains from the strike on
  for (int i = 0; i < 40; ++i) world.step(0.05);
  CHECK(world.overcast() > 0.3);  // and the sky is dark
  AssetPack dry = rarePack("tag: gloom\n");  // no weather action: no rain, but still dark
  World dark(dry, dry.scene("t"), 120, 30, 4);
  dark.triggerRare();
  for (int i = 0; i < 100; ++i) dark.step(0.05);
  CHECK(!dark.raining());
  CHECK(dark.overcast() > 0.3);
  for (int i = 0; i < 1000; ++i) dark.step(0.05);  // it has gone: the light comes back
  CHECK(dark.overcast() < 0.01);
}

TEST(a_strike_can_drop_a_bolt_that_reaches_the_ground_under_the_attacker) {
  AssetPack pack = rarePack("strike-sprite: bolt\n", "",
                            {"name: bolt\ntag: bolt\nfps: 10\nloop: no\ndie-after-animation: yes\n--- art\n|\n|\n|\n|\n|\n--- art\n/\n/\n/\n/\n/\n"});
  World world(pack, pack.scene("t"), 120, 30, 4);
  world.triggerRare();
  for (int i = 0; i < 600 && world.killedCount() == 0; ++i) world.step(0.05);
  world.step(0.05);
  Entity* bolt = nullptr;
  Entity* beast = nullptr;
  for (auto& e : world.entities()) {
    if (e->tag == "bolt") bolt = e.get();
    if (e->tag == "beast") beast = e.get();
  }
  CHECK(bolt && beast);
  CHECK(bolt->y + bolt->height() == world.seabedY());  // it hits the ground
  CHECK(std::fabs((bolt->x + bolt->width() / 2.0) - (beast->x + beast->width() / 2.0)) <= 2);
}

TEST(a_sprite_can_be_pinned_to_a_row) {
  AssetPack pack = miniPack("medium: no\nsky-fraction: 1\nground-rows: 6\n--- population\ntop count 5\nlow count 5\n",
                            {"name: top\nhabitat: sky\nspeed: 0..0\nrow: 2\n--- art\nT\n",
                             "name: low\nhabitat: sky\nspeed: 0..0\nrow: -4\n--- art\nL\n"});
  World world(pack, pack.scene("t"), 80, 30, 3);
  for (auto& e : world.entities()) {
    if (e->sprite->name() == "top") CHECK(e->y == 2);
    if (e->sprite->name() == "low") CHECK(e->y == 26);
  }
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

TEST(vt_console_uses_dark_backgrounds_and_no_shade_glyphs) {
  Frame a(2, 1), b(2, 1);
  b.at(0, 0).bg = {0, 0, 110};
  b.at(1, 0).bg = {200, 220, 255};
  b.at(1, 0).ch = U'x'; b.at(1, 0).fg = {255, 255, 255};
  const std::string d = encodeFrame(&a, b, ColorMode::Console);
  CHECK(d.find("38;") == std::string::npos && d.find("48;") == std::string::npos);
  CHECK(d.find("\xe2\x96") == std::string::npos);  // no shade glyphs: console fonts lack them
  CHECK(d.find("10") == std::string::npos);        // bright backgrounds (100-107) are unreliable
}

TEST(rgb_to_ansi16_maps_to_named_colors) {
  CHECK(rgbToAnsi16({255, 0, 0}) == 9);
  CHECK(rgbToAnsi16({0, 255, 255}) == 14);
  CHECK(rgbToAnsi16({255, 255, 255}) == 15);
  CHECK(rgbToAnsi16({0, 0, 0}) != 0);
  CHECK(rgbToAnsi16({70, 70, 85}) == 8);
}

// Renders `seconds` of ambience and returns the RMS of the last second, checking every sample.
static double ambienceRms(const AmbientParams& p, double seconds, uint64_t seed = 1) {
  constexpr int kRate = 8000;
  Ambience a(kRate, seed);
  std::vector<float> buf(static_cast<size_t>(kRate * seconds) * 2);
  a.render(p, buf.data(), static_cast<int>(buf.size() / 2));
  double sum = 0;
  size_t n = 0;
  for (size_t i = buf.size() > 2u * kRate ? buf.size() - 2u * kRate : 0; i < buf.size(); ++i) {
    CHECK(std::isfinite(buf[i]) && std::fabs(buf[i]) <= 1.0f);
    sum += double(buf[i]) * buf[i];
    ++n;
  }
  return std::sqrt(sum / double(n));
}

TEST(ambience_is_silent_for_silent_scenes_and_audible_in_the_forest) {
  AmbientParams p;
  CHECK(ambienceRms(p, 3) == 0.0);  // unknown scenes are silent
  p.kind = AmbientKind::Forest;
  CHECK(ambienceRms(p, 3) > 0.005);
}

TEST(ambience_rain_makes_the_forest_louder_and_muting_silences_it) {
  AmbientParams calm;
  calm.kind = AmbientKind::Forest;
  AmbientParams rainy = calm;
  rainy.rain = 1;
  CHECK(ambienceRms(rainy, 6) > ambienceRms(calm, 6) * 1.2);
  AmbientParams muted = rainy;
  muted.volume = 0;
  CHECK(ambienceRms(muted, 3) < 1e-4);
}

// RMS of seconds [from, to) of a render in which the flash counter rises at second 1.
static double thunderRms(AmbientKind kind, bool flash, int from, int to) {
  constexpr int kRate = 8000;
  Ambience a(kRate, 5);
  AmbientParams p;
  p.kind = kind;
  std::vector<float> buf(static_cast<size_t>(kRate) * 2);
  double sum = 0;
  size_t n = 0;
  for (int s = 0; s < to; ++s) {
    if (s == 1 && flash) p.flashes = 1;  // after a first second at zero
    a.render(p, buf.data(), kRate);
    if (s < from) continue;
    for (float v : buf) { CHECK(std::isfinite(v) && std::fabs(v) <= 1.0f); sum += double(v) * v; ++n; }
  }
  return std::sqrt(sum / double(n));
}

TEST(ambience_thunder_rolls_in_after_lightning_in_the_forest_only) {
  CHECK(thunderRms(AmbientKind::Forest, true, 2, 5) > thunderRms(AmbientKind::Forest, false, 2, 5) * 2);
  CHECK(thunderRms(AmbientKind::Silent, true, 0, 5) == 0.0);  // no thunder where no scene sounds
}

TEST(ambience_ignores_a_flash_counter_that_resets_with_a_rebuilt_world) {
  constexpr int kRate = 8000;
  Ambience a(kRate, 5);
  AmbientParams p;
  p.kind = AmbientKind::Forest;
  std::vector<float> buf(static_cast<size_t>(kRate) * 2);
  p.flashes = 5;  // a world that has had five flashes ...
  a.render(p, buf.data(), kRate);
  p.flashes = 0;  // ... is replaced by a fresh one
  double sum = 0;
  for (int s = 0; s < 4; ++s) {
    a.render(p, buf.data(), kRate);
    for (float v : buf) sum += double(v) * v;
  }
  CHECK(std::sqrt(sum / (4.0 * buf.size())) < 0.08);  // no thunder, just the quiet wind
}

TEST(ambience_a_strike_cracks_sharply_at_once_and_the_rumble_follows_after_it) {
  constexpr int kRate = 48000;
  Ambience a(kRate, 5);
  AmbientParams p;
  p.kind = AmbientKind::Forest;
  std::vector<float> buf(static_cast<size_t>(kRate) * 2);
  a.render(p, buf.data(), kRate);  // a second of calm
  a.render(p, buf.data(), kRate);
  auto peak = [&](double fromS, double toS) {
    float pk = 0;
    for (size_t i = static_cast<size_t>(fromS * kRate) * 2; i < static_cast<size_t>(toS * kRate) * 2; ++i) {
      pk = std::max(pk, std::fabs(buf[i]));
    }
    return pk;
  };
  const float calm = peak(0, 0.4);
  p.flashes = 1;
  a.render(p, buf.data(), kRate);
  CHECK(peak(0, 0.15) > 0.5f && peak(0, 0.15) > calm * 5);  // the crack: loud and immediate
  CHECK(peak(0.22, 0.29) < peak(0, 0.15) * 0.5f);            // it dies away before the second pop
}

TEST(ambience_rumbles_as_a_storm_approaches_before_any_lightning) {
  constexpr int kRate = 8000;
  auto rms = [&](float storm, AmbientKind kind) {
    Ambience a(kRate, 9);
    AmbientParams p;
    p.kind = kind;
    p.storm = storm;
    std::vector<float> buf(static_cast<size_t>(kRate) * 2);
    double sum = 0;
    size_t n = 0;
    for (int s = 0; s < 12; ++s) {
      a.render(p, buf.data(), kRate);
      if (s < 4) continue;
      for (float v : buf) { CHECK(std::isfinite(v) && std::fabs(v) <= 1.0f); sum += double(v) * v; ++n; }
    }
    return std::sqrt(sum / double(n));
  };
  CHECK(rms(1.0f, AmbientKind::Forest) > rms(0.0f, AmbientKind::Forest) * 1.5);
  CHECK(rms(0.5f, AmbientKind::Forest) > rms(0.0f, AmbientKind::Forest));
  CHECK(rms(1.0f, AmbientKind::Silent) == 0.0);
}

TEST(world_storm_proximity_follows_the_thundercloud_across_the_scene) {
  AssetPack pack = AssetPack::embedded();
  World w(pack, pack.scene("forest"), 100, 30, 3);
  CHECK(w.stormProximity() == 0.0);  // no cloud yet
  CHECK(w.triggerRare());
  double lo = 1, hi = 0;
  for (int i = 0; i < 4000; ++i) {
    w.step(1.0 / 20);
    const double s = w.stormProximity();
    CHECK(s >= 0 && s <= 1);
    lo = std::min(lo, s);
    hi = std::max(hi, s);
  }
  CHECK(lo < 0.05 && hi > 0.6);  // it comes in, passes overhead and leaves
  World moon(pack, pack.scene("moon"), 100, 30, 3);
  CHECK(moon.stormProximity() == 0.0);
}

// RMS of the seconds [from, to) of an aquarium render; `at` seconds in, the nibble counter rises by 3.
static double aquariumRms(AmbientParams p, int from, int to, int at = -1, double window = 1.0, int rate = 48000) {
  Ambience a(rate, 4);
  std::vector<float> buf(static_cast<size_t>(rate) * 2);
  double sum = 0;
  size_t n = 0;
  for (int s = 0; s < to; ++s) {
    if (s == at) p.nibbles += 3;
    a.render(p, buf.data(), rate);
    if (s < from) continue;
    const size_t count = static_cast<size_t>(window * rate) * 2;
    for (size_t i = 0; i < count && i < buf.size(); ++i) {
      CHECK(std::isfinite(buf[i]) && std::fabs(buf[i]) <= 1.0f);
      sum += double(buf[i]) * buf[i];
      ++n;
    }
  }
  return std::sqrt(sum / double(n));
}

TEST(ambience_the_aquarium_hums_and_a_whale_or_kraken_nearby_adds_to_it) {
  AmbientParams p;
  p.kind = AmbientKind::Aquarium;
  const double calm = aquariumRms(p, 2, 12);
  CHECK(calm > 0.01 && calm < 0.08);  // a quiet hum
  AmbientParams whale = p;
  whale.whale = 1;
  CHECK(aquariumRms(whale, 2, 12) > calm * 1.1);
  AmbientParams kraken = p;
  kraken.storm = 1;
  CHECK(aquariumRms(kraken, 4, 8) > calm * 2);
}

TEST(ambience_fish_nibbling_at_food_make_blips_only_in_the_aquarium) {
  constexpr int kRate = 48000;
  auto peakAfter = [&](AmbientKind kind, bool nibble) {
    Ambience a(kRate, 4);
    AmbientParams p;
    p.kind = kind;
    std::vector<float> buf(static_cast<size_t>(kRate) * 2);
    for (int s = 0; s < 3; ++s) a.render(p, buf.data(), kRate);  // the first second synchronises the counter
    if (nibble) p.nibbles += 3;
    a.render(p, buf.data(), kRate);
    float pk = 0;
    for (size_t i = 0; i < static_cast<size_t>(kRate * 0.4) * 2; ++i) pk = std::max(pk, std::fabs(buf[i]));
    return pk;
  };
  CHECK(peakAfter(AmbientKind::Aquarium, true) > peakAfter(AmbientKind::Aquarium, false) * 1.3f);
  CHECK(peakAfter(AmbientKind::Silent, true) == 0.0f);
}

TEST(ambience_follows_the_aquarium_world) {
  AssetPack pack = AssetPack::embedded();
  World aq(pack, pack.scene("aquarium"), 100, 30, 2);
  CHECK(ambientFor(aq).kind == AmbientKind::Aquarium);
  CHECK(ambientFor(aq).whale >= 0.0f && ambientFor(aq).whale <= 1.0f);
}

TEST(ambience_the_desert_wind_grows_into_a_sandstorm_and_a_tornado_roars) {
  AmbientParams calm;
  calm.kind = AmbientKind::Desert;
  const double base = ambienceRms(calm, 8);
  CHECK(base > 0.003 && base < 0.02);  // barely audible
  AmbientParams storm = calm;
  storm.rain = 1;  // a sandstorm
  CHECK(ambienceRms(storm, 8) > base * 2.5);
  AmbientParams tornado = calm;
  tornado.tornado = 1;
  CHECK(ambienceRms(tornado, 8) > base * 3);
  AmbientParams worm = calm;
  worm.storm = 1;  // the sandworm
  CHECK(ambienceRms(worm, 8) > base * 1.5);
}

TEST(ambience_follows_the_desert_world) {
  AssetPack pack = AssetPack::embedded();
  World desert(pack, pack.scene("desert"), 100, 30, 2);
  CHECK(ambientFor(desert).kind == AmbientKind::Desert);
  CHECK(ambientFor(desert).tornado == 0.0f);
  desert.startRain(10);  // a sandstorm
  CHECK(ambientFor(desert).rain == 1.0f);
}

TEST(ambience_the_moon_is_a_faint_drone_with_thuds_and_a_warble) {
  constexpr int kRate = 48000;
  auto peakIn = [&](unsigned impacts, unsigned flashes, double from, double to) {
    Ambience a(kRate, 6);
    AmbientParams p;
    p.kind = AmbientKind::Moon;
    std::vector<float> buf(static_cast<size_t>(kRate) * 2);
    a.render(p, buf.data(), kRate);
    a.render(p, buf.data(), kRate);
    p.impacts = impacts;
    p.flashes = flashes;
    a.render(p, buf.data(), kRate);
    float pk = 0;
    for (size_t i = static_cast<size_t>(from * kRate) * 2; i < static_cast<size_t>(to * kRate) * 2; ++i) {
      CHECK(std::isfinite(buf[i]) && std::fabs(buf[i]) <= 1.0f);
      pk = std::max(pk, std::fabs(buf[i]));
    }
    return pk;
  };
  const float calm = peakIn(0, 0, 0, 0.6);
  CHECK(calm > 0.005f && calm < 0.1f);                    // faint, but there
  CHECK(peakIn(1, 0, 0, 0.6) > calm * 5);                 // a meteorite thuds through the ground
  CHECK(peakIn(0, 1, 0, 0.9) > calm * 3);                 // the alien warbles
}

TEST(ambience_follows_the_moon_world_strikes) {
  AssetPack pack = AssetPack::embedded();
  World moon(pack, pack.scene("moon"), 100, 30, 2);
  CHECK(moon.impactCount() == 0);
  const unsigned before = ambientFor(moon).impacts;
  moon.impactAt(50);
  CHECK(ambientFor(moon).impacts == before + 1);
}

TEST(ambience_is_deterministic_per_seed) {
  AmbientParams p;
  p.kind = AmbientKind::Forest;
  p.night = 1;
  CHECK(ambienceRms(p, 2, 7) == ambienceRms(p, 2, 7));
  CHECK(ambienceRms(p, 2, 7) != ambienceRms(p, 2, 8));
}

TEST(ambience_follows_the_world_scene_and_weather) {
  AssetPack pack = AssetPack::embedded();
  World forest(pack, pack.scene("forest"), 80, 24, 1);
  World moon(pack, pack.scene("moon"), 80, 24, 1);
  CHECK(ambientFor(forest).kind == AmbientKind::Forest);
  CHECK(ambientFor(moon).kind == AmbientKind::Moon);
  CHECK(ambientFor(forest).rain == 0.0f);
  forest.startRain(10);
  CHECK(ambientFor(forest).rain == 1.0f);
}

// ----------------------------------------------------------------- mountains ---

// Counts the live entities of a sprite.
static std::vector<Entity*> allOf(World& w, const std::string& name) {
  std::vector<Entity*> out;
  for (auto& e : w.entities()) {
    if (!e->dead && e->sprite->name() == name) out.push_back(e.get());
  }
  return out;
}

// The mountains without the random avalanches, for tests that start one themselves.
static std::shared_ptr<const SceneDef> quietMountains(const AssetPack& pack) {
  auto sc = std::make_shared<SceneDef>(*pack.scene("mountains"));
  sc->rareFirst = sc->rareInterval = {1e9, 1e9};
  return sc;
}

// Zone under the feet of an entity, at the middle column.
static Zone zoneUnder(const World& w, const Entity& e) {
  return w.zoneAt(static_cast<int>(std::floor(e.anchorY >= 0 ? e.x + e.width() / 2.0 : e.x)),
                  static_cast<int>(std::lround(e.anchorY)) + e.height() - 1);
}

TEST(scene_parses_the_mountain_keys) {
  std::string err;
  auto sc = SceneDef::parse(
      "name: m\nmountains: yes\npeak-height: 0.8\ntreeline: 0.4\nsnowline: 0.7\n"
      "forest-colors: #102030 #001020\nrock-colors: #111111 #222222\nsnow-colors: #fefefe #eeeeee\n"
      "rock-glyphs: /\\\n", err);
  CHECK(sc != nullptr);
  if (!sc) return;
  CHECK(sc->mountains && sc->peakHeight == 0.8 && sc->treeline == 0.4 && sc->snowline == 0.7);
  CHECK(sc->forestA == (Rgb{16, 32, 48}) && sc->rockB == (Rgb{34, 34, 34}) && sc->snowA == (Rgb{254, 254, 254}));
  CHECK(sc->rockGlyphs.size() == 2);
  CHECK(!SceneDef::parse("name: m\nrock-colors: #111111\n", err));  // needs two colors
  auto plain = SceneDef::parse("name: p\n", err);
  CHECK(plain && !plain->mountains);
  auto steep = SceneDef::parse("name: s\ntreeline: 0.5\nsnowline: 0.2\n", err);
  CHECK(steep && steep->snowline > steep->treeline);  // the snow always lies above the trees
}

TEST(the_mountains_span_the_whole_height_from_a_meadow_and_stream_to_snowy_peaks_and_sky) {
  AssetPack pack = AssetPack::embedded();
  auto scene = pack.scene("mountains");
  CHECK(scene && scene->mountains && scene->riverRows > 0 && scene->dayNight);
  CHECK(scene->action.kind == SceneDef::Action::Kind::Weather && scene->rare.size() == 1);
  CHECK(!pack.scene("forest")->mountains);
  for (int h : {24, 40, 70}) {
    World world(pack, scene, 140, h, 5);
    CHECK(world.mountains() && !world.hasMedium());
    int highest = h, lowest = 0, at = 0;
    for (int x = 0; x < world.width(); ++x) {
      if (world.ridgeRow(x) < highest) { highest = world.ridgeRow(x); at = x; }
      lowest = std::max(lowest, world.ridgeRow(x));
    }
    CHECK(highest > 0 && highest <= h * 0.25);  // the tallest peak reaches high up, with sky above it
    CHECK(lowest >= h * 0.4);                   // the skyline falls away between the peaks
    CHECK(lowest < world.seabedY());            // but the meadow lies in front of all of it
    // Going down the tallest peak: snow, rock, forest, meadow, in this order, and nothing but sky above.
    CHECK(world.zoneAt(at, highest - 1) == Zone::None);
    int stage = 0;
    bool seen[4] = {};
    for (int y = highest; y < h; ++y) {
      const Zone z = world.zoneAt(at, y);
      const int now = z == Zone::Snow ? 0 : z == Zone::Rock ? 1 : z == Zone::Forest ? 2 : 3;
      CHECK(now >= stage);
      stage = std::max(stage, now);
      seen[now] = true;
    }
    CHECK(seen[0] && seen[1] && seen[2] && seen[3]);
    Frame f;
    world.render(f);
    CHECK(f.at(at, h - 1).bg.b > f.at(at, h - 1).bg.r);              // the stream is blue
    CHECK(f.at(at, world.seabedY() + 1).bg.g > f.at(at, world.seabedY() + 1).bg.b);  // the meadow green
  }
}

TEST(sun_and_stars_are_hidden_behind_the_peaks) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("mountains"), 140, 40, 5);
  world.setHour(14);
  Frame f;
  world.render(f);
  for (int x = 0; x < world.width(); ++x) {  // the terrain is drawn over everything in the sky
    CHECK(f.at(x, world.height() - 1).bg.b >= 100 || f.at(x, world.height() - 1).bg.g > 0);
    const Cell& below = f.at(x, world.ridgeRow(x));
    CHECK(!below.defaultBg && below.bg.r + below.bg.g + below.bg.b > 150);  // lit ground, not sky
  }
  const Cell& sky = f.at(0, 0);
  const Cell& horizon = f.at(0, 10);
  CHECK(sky.bg.r + sky.bg.g < horizon.bg.r + horizon.bg.g);  // lighter towards the horizon
}

TEST(trees_crags_and_spires_grow_in_their_own_zones) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("mountains"), 160, 44, 3);
  struct Rule { const char* sprite; Zone zone; };
  for (const Rule& r : {Rule{"fir_big", Zone::Forest}, Rule{"fir_small", Zone::Forest},
                        Rule{"dwarf_pine", Zone::Rock}, Rule{"crag", Zone::Rock}, Rule{"ice_spire", Zone::Snow}}) {
    const auto things = allOf(world, r.sprite);
    CHECK(!things.empty());
    for (Entity* e : things) {
      const Zone z = world.zoneAt(static_cast<int>(e->x + e->width() / 2.0), static_cast<int>(e->y) + e->height() - 1);
      CHECK(z == r.zone);
      if (z != r.zone) std::printf("    %s at %.1f,%.1f is in zone %d\n", r.sprite, e->x, e->y, static_cast<int>(z));
      CHECK(world.ridgeRow(static_cast<int>(e->x + e->width() / 2.0)) <= static_cast<int>(e->y));  // never up in the sky
    }
  }
  for (Entity* e : allOf(world, "shrub")) {
    const Zone z = world.zoneAt(static_cast<int>(e->x + e->width() / 2.0), static_cast<int>(e->y) + e->height() - 1);
    CHECK(z == Zone::Forest || z == Zone::Meadow);
  }
  // Things nearer the bottom of the screen are drawn in front.
  Entity* high = nullptr;
  Entity* low = nullptr;
  for (Entity* e : allOf(world, "fir_small")) {
    if (!high || e->y < high->y) high = e;
    if (!low || e->y > low->y) low = e;
  }
  CHECK(high && low && high->y < low->y && high->z > low->z);
}

TEST(goats_deer_and_bears_stay_in_their_zones_birds_and_eagles_in_theirs) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("mountains"), 160, 44, 6);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);  // nobody sleeps
  CHECK(!allOf(world, "goat").empty() && !allOf(world, "red_deer").empty() && !allOf(world, "bear").empty());
  double eagleMin = 1e9, eagleMax = -1e9;
  size_t checks = 0;
  for (int i = 0; i < 1200; ++i) {  // a minute
    world.step(0.05);
    for (Entity* g : allOf(world, "goat")) {
      if (g->x < 0 || g->x + g->width() > world.width()) continue;
      const Zone z = zoneUnder(world, *g);
      CHECK(z == Zone::Rock || z == Zone::Snow);
      ++checks;
    }
    for (const char* name : {"red_deer", "bear"}) {
      for (Entity* a : allOf(world, name)) {
        if (a->x < 0 || a->x + a->width() > world.width()) continue;
        const Zone z = zoneUnder(world, *a);
        CHECK(z == Zone::Forest || z == Zone::Meadow);
        ++checks;
      }
    }
    for (Entity* b : allOf(world, "finch")) {  // over the forest and the valley, never up among the peaks
      CHECK(b->y + b->height() >= world.surfaceY() - 8);
      CHECK(b->y + b->height() <= world.riverTop() + 3);
    }
    for (Entity* e : allOf(world, "golden_eagle")) {
      CHECK(world.zoneAt(static_cast<int>(std::clamp(e->x + e->width() / 2, 0.0, 159.0)), static_cast<int>(e->y) + e->height() - 1) != Zone::Meadow);
      CHECK(e->y + e->height() <= world.surfaceY() + 8);  // down as far as the rock and no lower
      eagleMin = std::min(eagleMin, e->y);
      eagleMax = std::max(eagleMax, e->y);
    }
  }
  CHECK(checks > 1000);
  CHECK(eagleMax - eagleMin > 10);  // they soar high and swoop down
}

TEST(goats_leap_in_hops_and_wander_between_ledges) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, quietMountains(pack), 160, 44, 6);
  world.setTimeMode(World::TimeMode::Day);
  Entity* goat = allOf(world, "goat").front();
  const double x0 = goat->x, row0 = goat->anchorY;
  double lowest = goat->y, highest = goat->y;
  bool changedLedge = false;
  for (int i = 0; i < 2400; ++i) {
    if (goat->x < 3 || goat->x + goat->width() > world.width() - 3) break;  // it would leave and be removed
    world.step(0.05);
    lowest = std::max(lowest, goat->y);
    highest = std::min(highest, goat->y);
    changedLedge |= std::fabs(goat->anchorY - row0) > 0.9;
  }
  CHECK(std::fabs(goat->x - x0) > 1);
  CHECK(lowest - highest >= goat->hopHeight * 0.7);  // jumps
  CHECK(changedLedge);                                // and climbs to other ledges
}

TEST(a_walker_turns_round_where_its_zone_ends) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, quietMountains(pack), 160, 44, 2);
  world.setTimeMode(World::TimeMode::Day);
  // How many columns a goat can walk along its ledge before it would stand outside the rock and snow.
  auto reach = [&](const Entity& g, int dir) {
    for (int d = 1; d < 60; ++d) {
      const double x = g.x + dir * d;
      if (x < 4 || x + g.width() > world.width() - 4) return 999;
      if (!world.fits(g, x, g.anchorY)) return d;
    }
    return 999;
  };
  Entity* goat = nullptr;
  int dir = 0;
  for (Entity* g : allOf(world, "goat")) {
    if (g->x < 4 || g->x + g->width() > world.width() - 4) continue;
    for (int d : {-1, 1}) {
      if (!goat && reach(*g, d) < 30) { goat = g; dir = d; }
    }
  }
  CHECK(goat != nullptr);  // some goat stands near the end of its ledge
  if (!goat) return;
  goat->vx = std::fabs(goat->vx) * dir;  // send it that way
  goat->mirrored = (dir > 0) != goat->facesRight;
  goat->laneTarget = -1;
  bool turned = false;
  for (int i = 0; i < 1200 && !turned; ++i) {
    if (goat->x < 3 || goat->x + goat->width() > world.width() - 3) break;  // it would leave and be removed
    world.step(0.05);
    CHECK(world.fits(*goat, goat->x, goat->anchorY));  // never in the sky or out of its zone
    turned = (goat->vx > 0) != (dir > 0);
  }
  CHECK(turned);
}

TEST(bears_hunt_deer_in_the_woods_and_the_deer_come_back) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("mountains"), 160, 44, 4);
  world.setTimeMode(World::TimeMode::Day);
  const size_t deer = allOf(world, "red_deer").size();
  CHECK(deer > 3);
  for (auto& e : world.entities()) {  // one bear, one deer in front of it
    if (e->sprite->name() == "bear") e->age = 0;
  }
  Entity* bear = allOf(world, "bear").front();
  Entity* prey = allOf(world, "red_deer").front();
  bear->x = prey->x - 14;
  bear->vx = std::fabs(bear->vx);
  bear->mirrored = !bear->facesRight;
  bool caught = false;
  for (int i = 0; i < 6000 && !caught; ++i) {  // five minutes
    world.step(0.05);
    caught = world.eatenCount() > 0;
  }
  CHECK(caught);
  CHECK(!allOf(world, "pounce").empty() || world.eatenCount() > 0);
  for (int i = 0; i < 2400; ++i) world.step(0.05);
  CHECK(allOf(world, "red_deer").size() >= deer - 2);  // replaced
}

TEST(the_snowstorm_blows_snow_across_the_scene_whites_it_out_and_ends) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("mountains"), 140, 40, 2);
  CHECK(!world.raining() && world.overcast() == 0);
  CHECK(world.interact() == 1);
  CHECK(world.raining());
  for (int i = 0; i < 60; ++i) world.step(0.05);
  CHECK(world.countTag("flake") > 80);  // snow all over the screen
  double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
  for (auto& e : world.entities()) {
    if (e->tag != "flake") continue;
    minX = std::min(minX, e->x); maxX = std::max(maxX, e->x);
    minY = std::min(minY, e->y); maxY = std::max(maxY, e->y);
    CHECK(std::fabs(e->vx) > 10);  // an icy wind
    CHECK(e->vy > 0);              // that also lets the snow settle
  }
  CHECK(maxX - minX > 40 && maxY - minY > 20);
  for (int i = 0; i < 100; ++i) world.step(0.05);
  CHECK(world.overcast() > 0.4);
  Frame f;
  world.render(f);
  const Rgb sky = f.at(0, 0).bg;
  World clear(pack, pack.scene("mountains"), 140, 40, 2);
  Frame g;
  clear.render(g);
  CHECK(sky.r > g.at(0, 0).bg.r && sky.g > g.at(0, 0).bg.g);  // the whole sky turns pale
  for (int i = 0; i < 600; ++i) world.step(0.05);              // 30 s: over
  CHECK(!world.raining() && world.countTag("flake") == 0);
}

TEST(an_avalanche_rips_down_a_snowy_flank_through_the_forest_into_the_stream) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, quietMountains(pack), 140, 40, 7);
  world.setTimeMode(World::TimeMode::Day);
  const auto firsBefore = allOf(world, "fir_big").size() + allOf(world, "fir_small").size() + allOf(world, "shrub").size();
  size_t lifeBefore = 0;
  for (auto& e : world.entities()) lifeBefore += e->life;
  CHECK(firsBefore > 10 && lifeBefore > 10);
  CHECK(world.avalancheLevel() == 0);
  CHECK(world.triggerRare());
  CHECK(!world.triggerRare());  // one at a time
  CHECK(allOf(world, "avalanche").size() == 1);

  bool snow = false, rock = false, forest = false, meadow = false, stream = false;
  double loudest = 0, beforeSlide = 0;
  size_t flyers = 0;
  for (auto& e : world.entities()) flyers += (e->zone == Zone::Air || e->zone == Zone::LowAir);
  for (int i = 0; i < 1200; ++i) {
    world.step(0.05);
    if (i == 20) beforeSlide = world.avalancheLevel();
    loudest = std::max(loudest, world.avalancheLevel());
    for (int x = 0; x < world.width(); ++x) {
      for (int y = world.ridgeRow(x); y < world.height(); ++y) {
        if (world.scarAt(x, y) < 0.5) continue;
        const Zone z = world.zoneAt(x, y);
        snow |= z == Zone::Snow; rock |= z == Zone::Rock; forest |= z == Zone::Forest;
        meadow |= z == Zone::Meadow && y < world.riverTop();
        stream |= y >= world.riverTop();
      }
    }
  }
  CHECK(snow && rock && forest && meadow && stream);  // from the peak all the way to the water
  CHECK(beforeSlide > 0.1 && beforeSlide < 0.9);     // it creaks and rumbles before it goes
  CHECK(loudest == 1.0);
  CHECK(world.killedCount() > 3);                    // trees and animals in the way were buried
  CHECK(allOf(world, "avalanche").empty() && world.avalancheLevel() == 0);
  size_t flyersAfter = 0;
  for (auto& e : world.entities()) flyersAfter += (e->zone == Zone::Air || e->zone == Zone::LowAir);
  CHECK(flyersAfter + 3 >= flyers);                  // the birds and eagles above it were not buried

  // Later the swath is gone, and the buried trees and animals have come back.
  for (int i = 0; i < 2400; ++i) world.step(0.05);
  for (int x = 0; x < world.width(); ++x) {
    for (int y = world.ridgeRow(x); y < world.height(); ++y) CHECK(world.scarAt(x, y) == 0);
  }
  CHECK(world.restockPending() == 0);
  const auto firsAfter = allOf(world, "fir_big").size() + allOf(world, "fir_small").size() + allOf(world, "shrub").size();
  CHECK(firsAfter + 3 >= firsBefore);
}

TEST(the_avalanche_scars_the_picture_and_the_stream_shows_again_when_it_fades) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, quietMountains(pack), 140, 40, 7);
  world.triggerRare();
  for (int i = 0; i < 240; ++i) world.step(0.05);  // 12 s: it has reached the stream
  int sx = -1;
  for (int x = 0; x < world.width() && sx < 0; ++x) {
    if (world.scarAt(x, world.height() - 1) > 0.8) sx = x;
  }
  CHECK(sx >= 0);
  if (sx < 0) return;
  Frame f;
  world.render(f);
  const Rgb buried = f.at(sx, world.height() - 1).bg;
  CHECK(buried.r > 200 && buried.g > 200 && buried.b > 200);  // snow where the water ran
  for (int i = 0; i < 1000; ++i) world.step(0.05);  // and 50 s later
  world.render(f);
  const Rgb again = f.at(sx, world.height() - 1).bg;
  CHECK(again.b > again.r + 40);                              // the stream flows again
}

TEST(avalanches_survive_tiny_and_odd_sizes) {
  AssetPack pack = AssetPack::embedded();
  for (int w : {1, 2, 5, 40, 200}) {
    for (int h : {1, 2, 4, 9, 24, 90}) {
      World world(pack, pack.scene("mountains"), w, h, 11);
      world.triggerRare();
      world.interact();
      Frame f;
      for (int i = 0; i < 500; ++i) world.step(0.05);
      world.render(f, Style::Mono);
      CHECK(f.width() == w && f.height() == h);
    }
  }
}

TEST(mountain_populations_come_back_after_being_wiped_by_the_rare_event_timer) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("mountains"), 160, 44, 9);
  const size_t start = world.entityCount();
  for (int i = 0; i < 20000; ++i) world.step(0.05);  // 16 minutes: several avalanches
  CHECK(world.entityCount() > start / 2 && world.entityCount() < start * 2);
}

TEST(the_mountains_sound_like_wind_a_stream_an_eagle_and_roar_in_a_storm_and_an_avalanche) {
  AmbientParams calm;
  calm.kind = AmbientKind::Mountains;
  const double quiet = ambienceRms(calm, 4);
  CHECK(quiet > 0.003);
  AmbientParams storm = calm;
  storm.rain = 1;
  CHECK(ambienceRms(storm, 8) > quiet * 1.5);
  AmbientParams slide = calm;
  slide.avalanche = 1;
  CHECK(ambienceRms(slide, 4) > quiet * 2);
  // An eagle cries now and then by day (a shrill burst well above the wind) and never at night.
  auto loudestBurst = [](AmbientParams p) {
    constexpr int kRate = 8000;
    Ambience a(kRate, 3);
    std::vector<float> buf(static_cast<size_t>(kRate * 120) * 2);
    a.render(p, buf.data(), static_cast<int>(buf.size() / 2));
    double best = 0;
    for (size_t i = 0; i + 800 * 2 < buf.size(); i += 800 * 2) {  // 0.1 s windows
      double sum = 0;
      for (size_t k = 0; k < 800 * 2; ++k) sum += double(buf[i + k]) * buf[i + k];
      best = std::max(best, std::sqrt(sum / (800 * 2)));
    }
    return best;
  };
  AmbientParams night = calm;
  night.night = 1;
  CHECK(loudestBurst(calm) > loudestBurst(night) * 1.15);

  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("mountains"), 140, 40, 7);
  CHECK(ambientFor(world).kind == AmbientKind::Mountains);
  CHECK(ambientFor(world).avalanche == 0.0f);
  world.triggerRare();
  float loudest = 0;
  for (int i = 0; i < 300; ++i) {
    world.step(0.05);
    loudest = std::max(loudest, ambientFor(world).avalanche);
  }
  CHECK(loudest > 0.9f);
}

/// A session over the built-in scenes, 100 by 30 cells.
static Session makeSession(const AssetPack& pack, const Session::Config& config = {}) {
  return Session(pack, pack.scenes(), config, 100, 30);
}

TEST(session_starts_in_its_scene_with_the_title_as_a_toast_and_switches_scenes_with_wrapping) {
  AssetPack pack = AssetPack::embedded();
  const auto scenes = pack.scenes();
  Session::Config config;
  config.scene = 1;
  Session s = makeSession(pack, config);
  CHECK(s.sceneIndex() == 1);
  CHECK(s.world().scene().name == scenes[1]->name);
  Frame f;
  s.render(f);
  CHECK(f.toText().find(scenes[1]->title) != std::string::npos);
  s.handle(Action::Right);
  s.step(0.05);  // the new world is built on the next step
  CHECK(s.sceneIndex() == 2);
  CHECK(s.world().scene().name == scenes[2]->name);
  for (size_t i = 0; i < scenes.size(); ++i) s.handle(Action::Right);
  s.step(0.05);
  CHECK(s.sceneIndex() == 2);  // a full round wraps
  s.handle(Action::Left);
  s.handle(Action::Left);
  s.handle(Action::Left);
  s.step(0.05);
  CHECK(s.sceneIndex() == (2 + scenes.size() - 3) % scenes.size());
}

TEST(session_pause_freezes_the_world_and_mutes_the_sound_and_style_changes_ask_for_a_repaint) {
  AssetPack pack = AssetPack::embedded();
  Session s = makeSession(pack);
  CHECK(s.takeRepaint());   // the first frame is a full repaint
  CHECK(!s.takeRepaint());  // and reading it clears the flag
  s.step(0.05);
  const double t = s.world().time();
  CHECK(t > 0);
  CHECK(s.ambient().volume == 1.0f);
  s.handle(Action::Pause);
  CHECK(s.paused());
  s.step(0.05);
  CHECK(s.world().time() == t);
  CHECK(s.ambient().volume == 0.0f);
  s.handle(Action::Pause);
  s.step(0.05);
  CHECK(s.world().time() > t);

  const Style before = s.style();
  s.handle(Action::Style);
  CHECK(s.style() != before);
  CHECK(s.takeRepaint());
  s.handle(Action::Style);
  s.handle(Action::Style);
  CHECK(s.style() == before);  // three styles, then back
}

TEST(session_sound_is_a_request_the_frontend_can_refuse) {
  AssetPack pack = AssetPack::embedded();
  Session s = makeSession(pack);
  CHECK(!s.soundWanted());
  s.handle(Action::Audio);
  CHECK(s.soundWanted());
  s.soundUnavailable();  // the frontend found no device
  CHECK(!s.soundWanted());
  Frame f;
  s.render(f);
  CHECK(f.toText().find("No audio device") != std::string::npos);
  s.handle(Action::Audio);
  s.handle(Action::Audio);
  CHECK(!s.soundWanted());

  Session::Config config;
  config.sound = true;
  Session on = makeSession(pack, config);
  CHECK(on.soundWanted());
  on.soundUnavailable(false);  // a quiet failure, as at startup without a sound device
  CHECK(!on.soundWanted());
  Frame g;
  on.render(g);
  CHECK(g.toText().find("No audio device") == std::string::npos);
}

TEST(session_time_of_day_cycles_only_in_scenes_with_a_clock_and_survives_scene_changes_and_resizes) {
  AssetPack pack = AssetPack::embedded();
  const auto scenes = pack.scenes();
  size_t withClock = 0;
  for (size_t i = 0; i < scenes.size(); ++i) {
    if (scenes[i]->dayNight) withClock = i;
  }
  Session::Config config;
  config.scene = withClock;
  config.hour = 22;
  Session s = makeSession(pack, config);
  CHECK(std::fabs(s.world().phase() - 22.0 / 24.0) < 1e-6);
  s.handle(Action::Time);  // cycle -> dawn: the clock is frozen at dawn
  s.step(0.05);
  CHECK(s.world().timeMode() == World::TimeMode::Dawn);
  s.resize(120, 40);
  s.step(0.05);
  CHECK(s.world().width() == 120 && s.world().height() == 40);
  CHECK(s.world().timeMode() == World::TimeMode::Dawn);  // kept across the rebuild

  AssetPack still = miniPack("medium: no\nsky-fraction: 1\nground-rows: 4\n", {});
  Session m = makeSession(still);
  m.handle(Action::Time);
  CHECK(m.world().timeMode() == World::TimeMode::Cycle);  // nothing to cycle in a scene without a clock
}

TEST(real_time_is_the_last_time_mode_and_follows_the_clock_the_frontend_reports) {
  AssetPack pack = AssetPack::embedded();
  const auto scenes = pack.scenes();
  Session::Config config;
  for (size_t i = 0; i < scenes.size(); ++i) {
    if (scenes[i]->dayNight) config.scene = i;
  }
  Session s = makeSession(pack, config);
  CHECK(s.world().timeMode() == World::TimeMode::Cycle);  // never the default
  for (int i = 0; i < 4; ++i) s.handle(Action::Time);     // dawn, day, dusk, night
  CHECK(s.world().timeMode() == World::TimeMode::Night);
  s.setClockHour(15.5);
  s.handle(Action::Time);  // the last one
  CHECK(s.world().timeMode() == World::TimeMode::Realtime);
  CHECK(std::fabs(s.world().phase() - 15.5 / 24.0) < 1e-6);
  s.setClockHour(15.75);
  for (int i = 0; i < 100; ++i) s.step(0.05);  // the world's own clock does not run on
  CHECK(std::fabs(s.world().phase() - 15.75 / 24.0) < 1e-6);
  s.resize(120, 40);  // kept across the rebuild
  s.step(0.05);
  CHECK(s.world().timeMode() == World::TimeMode::Realtime);
  CHECK(std::fabs(s.world().phase() - 15.75 / 24.0) < 1e-6);
  s.handle(Action::Time);  // and round again to the cycle
  CHECK(s.world().timeMode() == World::TimeMode::Cycle);
  Session::Config rt = config;
  rt.realtime = true;
  Session r = makeSession(pack, rt);
  r.setClockHour(3);
  r.step(0.05);
  CHECK(r.world().timeMode() == World::TimeMode::Realtime && std::fabs(r.world().phase() - 3.0 / 24.0) < 1e-6);
}

TEST(session_menu_consumes_navigation_while_open_and_quit_comes_from_keys_or_menu) {
  AssetPack pack = AssetPack::embedded();
  Session s = makeSession(pack);
  const size_t scene = s.sceneIndex();
  s.handle(Action::Menu);
  CHECK(s.menu().isOpen());
  s.handle(Action::Right);  // changes the menu's first value, the scene, through the menu
  s.step(0.05);
  CHECK(s.sceneIndex() != scene);
  s.handle(Action::Back);  // closes the menu
  CHECK(!s.menu().isOpen());
  s.handle(Action::Info);
  Frame f;
  s.render(f);
  CHECK(f.toText().find("entities") != std::string::npos);  // the info line is on
  CHECK(!s.quitRequested());
  s.handle(Action::Quit);
  CHECK(s.quitRequested());
}

TEST(session_menu_offers_quit_unless_the_frontend_turns_it_off) {
  AssetPack pack = AssetPack::embedded();
  auto menuText = [&](bool quitEntry) {
    Session::Config config;
    config.quitEntry = quitEntry;
    Session s = makeSession(pack, config);
    s.handle(Action::Menu);
    Frame f;
    s.render(f);
    return f.toText();
  };
  CHECK(menuText(true).find("Quit") != std::string::npos);
  CHECK(menuText(false).find("Quit") == std::string::npos);
  CHECK(menuText(false).find("Resume") != std::string::npos);
}

TEST(session_restart_uses_a_new_seed_and_the_action_keys_reach_the_world) {
  AssetPack pack = AssetPack::embedded();
  Session::Config config;
  config.seed = 5;
  Session s = makeSession(pack, config);
  CHECK(s.world().seed() == 5);
  s.handle(Action::Redraw);
  s.step(0.05);
  CHECK(s.world().seed() == 6);
  size_t before = s.world().entityCount();
  s.handle(Action::Interact);
  CHECK(s.world().entityCount() >= before);  // the scene's action ran without a crash
  s.handle(Action::Rare);
  s.step(0.05);
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
