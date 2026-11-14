// Dependency-free test runner: each TEST registers itself, main() runs them all.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <atomic>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "app/session.h"
#include "core/ambience.h"
#include "core/seqlock.h"
#include "core/assets.h"
#include "core/layout.h"
#include "core/menu.h"
#include "core/spatializer.h"
#include "version.h"
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

TEST(decor_lines_take_jitter_chance_per_cols_and_alternatives) {
  std::string err;
  auto sc = SceneDef::parse("name: s\n--- decor\noak|pine center -4 jitter 6 chance 0.5 per-cols 40\n", err);
  CHECK(sc != nullptr);
  CHECK(sc->decor[0].sprite == "oak|pine" && sc->decor[0].jitter == 6 && sc->decor[0].chance == 0.5 && sc->decor[0].perCols == 40);
  CHECK(SceneDef::parse("name: s\n--- decor\noak left 0 shade 3\n", err) == nullptr);
}

TEST(decor_lines_can_be_aligned_to_an_anchor) {
  std::string err;
  auto sc = SceneDef::parse("name: s\n--- decor\npond center 0 jitter 9 anchor yes\npalm from-left -6\npalm from-right -7\nreeds from-center 0\n", err);
  CHECK(sc != nullptr);
  CHECK(sc->decor[0].anchor && !sc->decor[1].anchor);
  CHECK(sc->decor[1].align == "from-left" && sc->decor[2].align == "from-right" && sc->decor[3].align == "from-center");
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
                        "dune_small", "pyramid_big", "pyramid_small", "oasis", "oasis_small", "oasis_big", "funnel", "sandgrain",
                        "sandspray", "sandpuff", "dustburst", "palm_big", "palm_small", "cactus_big",
                        "cactus_small", "boulder", "tent", "campfire", "camel", "horse", "gazelle",
                        "lion", "vulture", "bat", "tumbleweed", "kraken", "inkburst", "thundercloud",
                        "lightning", "spark", "sandworm", "alien", "fir_big", "fir_small", "shrub", "dwarf_pine",
                        "crag", "ice_spire", "goat", "red_deer", "bear", "finch", "golden_eagle",
                        "cloud_wisp", "cloud_big_l", "cloud_small_l", "cloud_wisp_l", "snowpuff", "snowflake", "pounce", "avalanche",
                        "submarine", "toxinpuff", "flood", "log", "helicopter", "yeti", "lizard_titan", "winged_titan", "asteroid"})
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
  for (int i = 0; i < 2800; ++i) world.step(0.05);  // creatures leave and respawn (140 s: before the first rare catastrophe, at 150 s)
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

TEST(menu_shows_its_footer_below_the_hint_and_still_fits) {
  Menu m = sampleMenu();
  m.setFooter({"diorascii 9.9.9", "Copyright (c) 2026 Someone"});
  m.open();
  Frame f(60, 24);
  m.render(f, Style::Full);
  const std::string text = f.toText();
  const size_t hint = text.find("esc: close"), version = text.find("diorascii 9.9.9"), copy = text.find("Copyright (c) 2026 Someone");
  CHECK(hint != std::string::npos && version != std::string::npos && copy != std::string::npos);
  CHECK(hint < version && version < copy);  // in that order, from the top
  Frame tiny(30, 6);  // too small for the box: the one-line fallback, without small print
  m.render(tiny, Style::Full);
  CHECK(tiny.toText().find("Copyright") == std::string::npos);
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

TEST(scenes_mix_clouds_of_the_usual_size_with_larger_ones) {
  AssetPack pack = AssetPack::embedded();
  const std::pair<const char*, const char*> pairs[] = {{"cloud_big", "cloud_big_l"}, {"cloud_small", "cloud_small_l"},
                                                       {"cloud_wisp", "cloud_wisp_l"}};
  for (const auto& p : pairs) {
    CHECK(pack.find(p.second)->width() > pack.find(p.first)->width());
    CHECK(pack.find(p.second)->height() > pack.find(p.first)->height());
  }
  for (const char* scene : {"forest", "desert", "mountains"}) {
    World world(pack, pack.scene(scene), 300, 40, 1);
    int usual = 0, large = 0;
    for (auto& e : world.entities()) {
      const std::string& n = e->sprite->name();
      usual += (n == "cloud_big" || n == "cloud_small" || n == "cloud_wisp") ? 1 : 0;
      large += (n == "cloud_big_l" || n == "cloud_small_l" || n == "cloud_wisp_l") ? 1 : 0;
    }
    CHECK(usual > 0 && large > 0);
  }
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
  CHECK(world.triggerRare(RarePick::Common));
  CHECK(world.countTag("beast") == 1);
  CHECK(!world.triggerRare(RarePick::Common));  // one is already under way
  AssetPack none = miniPack("--- population\n", {});
  World quiet(none, none.scene("t"), 60, 20, 1);
  CHECK(!quiet.triggerRare(RarePick::Common));  // this scene has none
}

TEST(a_catastrophe_strikes_once_halfway_across_and_wipes_out_all_life_but_nothing_else) {
  AssetPack pack = rarePack("life: yes\n");  // the attacker is itself a living thing, and still spares itself
  World world(pack, pack.scene("t"), 120, 30, 4);
  CHECK(countSprite(world, "fish") == 30 && countSprite(world, "rock") == 1);
  world.triggerRare(RarePick::Common);
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
  world.triggerRare(RarePick::Common);
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
    CHECK(scene && scene->rare.size() >= 1u && scene->rare[0].sprite == c.attacker);
    World world(pack, scene, 160, 36, 3);
    const bool animals = std::string(c.scene) != "moon";
    size_t lifeBefore = 0;
    for (auto& e : world.entities()) lifeBefore += e->life;
    CHECK(!animals || lifeBefore > 10);
    CHECK(world.triggerRare(RarePick::Common));
    // A storm strikes everything at once; an alien, a worm and a kraken take things as they come
    // to them, so wait for them to have crossed the scene.
    const bool lashes = std::string(c.scene) == "desert" || std::string(c.scene) == "aquarium";
    const bool sweeps = std::string(c.scene) == "moon" || lashes;
    const size_t flyers = countSprite(world, "vulture") + countSprite(world, "bat");
    bool struck = false;
    for (int i = 0; i < 3000 && !(struck && (!sweeps || countSprite(world, c.attacker) == 0)); ++i) {
      world.step(0.05);
      struck = world.killedCount() > 0;
    }
    CHECK(struck);
    if (!struck) continue;
    if (sweeps) {
      CHECK(world.killedCount() >= 1 && countSprite(world, "flag") == 0);  // the flag stands still: always taken
    }
    if (lashes) {  // the worm and the kraken take what they reach, one by one (the worm not what flies)
      CHECK(world.crushCount() == world.killedCount() && world.crushCount() > 3);
      CHECK(countSprite(world, "vulture") + countSprite(world, "bat") == flyers);
    } else if (animals) {
      size_t aliveLife = 0;
      for (auto& e : world.entities()) aliveLife += (e->life && !e->dead);
      CHECK(aliveLife == 0);                      // everything alive is gone, in a single moment
      CHECK(world.killedCount() > 0);
      CHECK(world.restockPending() >= lifeBefore);  // and every one of them comes back later, one by one
    } else {
      CHECK(countSprite(world, "flag") == 0);  // the alien's stationary target
      CHECK(countSprite(world, "alien") == 0);  // it has crossed and gone
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
  world.triggerRare(RarePick::Common);
  for (int i = 0; i < 3000 && countSprite(world, "alien") > 0; ++i) world.step(0.05);  // until it has crossed
  // The flag stands still and is always taken; the rover drives about and may slip past the alien.
  CHECK(world.killedCount() >= 1 && world.killedCount() <= 2);
  CHECK(countSprite(world, "flag") == 0);
  CHECK(countSprite(world, "rock") == rocks && countSprite(world, "lander") == landers);  // nothing else
  for (int i = 0; i < 1500; ++i) world.step(0.05);  // well over a minute later
  CHECK(countSprite(world, "rover") == 1 && countSprite(world, "flag") == 1);
}

TEST(a_sweeping_catastrophe_takes_each_victim_as_its_middle_passes_it) {
  AssetPack pack = miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 6\nrare-first: 1000..1000\n--- population\nrover count 1\n"
      "--- decor\nflag left 10\nflag right 10\n--- rare\nalien 1\n",
      {"name: alien\nfacing: right\nhabitat: ground\nspeed: 12..12\nwipes: flag\nstrike-effect: puff\n"
       "strike-mode: sweep\nbehaviors: catastrophe\n--- art\nAAA\nAAA\n",
       "name: rover\nhabitat: ground\nspeed: 0..0\n--- art\nrr\n",
       "name: flag\nhabitat: ground\n--- art\n|>\n|\n",
       "name: puff\ntag: puff\nfps: 8\nloop: no\ndie-after-animation: yes\n--- art\n*\n--- art\n.\n"});
  World world(pack, pack.scene("t"), 120, 30, 6);
  std::vector<double> flags;
  for (auto& e : world.entities()) {
    if (e->sprite->name() == "flag") flags.push_back(e->x + e->width() / 2.0);
  }
  CHECK(flags.size() == 2);
  std::sort(flags.begin(), flags.end());
  world.triggerRare(RarePick::Common);
  Entity* alien = nullptr;
  for (auto& e : world.entities()) {
    if (e->sprite->name() == "alien") alien = e.get();
  }
  CHECK(alien != nullptr);
  if (!alien) return;
  const bool right = alien->vx > 0;
  size_t seen = 0;
  for (int i = 0; i < 600 && countSprite(world, "alien") > 0; ++i) {
    world.step(0.05);
    alien = nullptr;  // the world frees it once it has left
    for (auto& e : world.entities()) {
      if (e->sprite->name() == "alien") alien = e.get();
    }
    if (!alien) break;
    const double middle = alien->x + alien->width() / 2.0;
    // Not taken before the alien's middle has reached it...
    for (double f : flags) {
      const bool reached = right ? middle >= f - 1.5 : middle <= f + 1.5;
      if (!reached) {
        size_t standing = 0;
        for (auto& e : world.entities()) standing += (e->sprite->name() == "flag" && !e->dead && std::fabs(e->x + e->width() / 2.0 - f) < 0.01);
        CHECK(standing == 1);
      }
    }
    seen = world.killedCount();
  }
  CHECK(seen == 2);  // ...and both in the end, one after the other
  CHECK(countSprite(world, "rover") == 1);  // not a named victim
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
  world.triggerRare(RarePick::Common);
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
  world.triggerRare(RarePick::Common);
  CHECK(!world.raining());
  for (int i = 0; i < 600 && world.killedCount() == 0; ++i) world.step(0.05);
  CHECK(world.raining());  // it rains from the strike on
  for (int i = 0; i < 40; ++i) world.step(0.05);
  CHECK(world.overcast() > 0.3);  // and the sky is dark
  AssetPack dry = rarePack("tag: gloom\n");  // no weather action: no rain, but still dark
  World dark(dry, dry.scene("t"), 120, 30, 4);
  dark.triggerRare(RarePick::Common);
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
  world.triggerRare(RarePick::Common);
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
  CHECK(w.triggerRare(RarePick::Common));
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
  CHECK(scene->action.kind == SceneDef::Action::Kind::Weather && scene->rare.size() >= 1);
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
  World world(pack, quietMountains(pack), 160, 44, 7);
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
  CHECK(world.triggerRare(RarePick::Common));
  CHECK(!world.triggerRare(RarePick::Common));  // one at a time
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
  world.triggerRare(RarePick::Common);
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
      world.triggerRare(RarePick::Common);
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
  world.triggerRare(RarePick::Common);
  float loudest = 0;
  for (int i = 0; i < 300; ++i) {
    world.step(0.05);
    loudest = std::max(loudest, ambientFor(world).avalanche);
  }
  CHECK(loudest > 0.9f);
}

// RMS of the `seconds` after `after` replaces `before` in a running ambience (a counter that went up).
static double rmsAfterChange(const AmbientParams& before, const AmbientParams& after, double seconds, uint64_t seed = 2) {
  constexpr int kRate = 8000;
  Ambience a(kRate, seed);
  std::vector<float> warm(kRate * 2 * 2);
  a.render(before, warm.data(), kRate * 2);
  std::vector<float> buf(static_cast<size_t>(kRate * seconds) * 2);
  a.render(after, buf.data(), static_cast<int>(buf.size() / 2));
  double sum = 0;
  for (float s : buf) {
    CHECK(std::isfinite(s) && std::fabs(s) <= 1.0f);
    sum += double(s) * s;
  }
  return std::sqrt(sum / double(buf.size()));
}

TEST(a_sandworm_bellows_as_it_crosses_and_crunches_each_thing_it_swallows) {
  AmbientParams calm;
  calm.kind = AmbientKind::Desert;
  AmbientParams worm = calm;
  worm.worm = 1;
  CHECK(ambienceRms(worm, 10) > ambienceRms(calm, 10) * 2);  // a deep roar over the dry wind
  AmbientParams crunch = calm;
  crunch.crushes = 1;
  CHECK(rmsAfterChange(calm, crunch, 0.6) > rmsAfterChange(calm, calm, 0.6) * 2);  // a crunch at once
  AmbientParams moonCrunch = crunch;
  moonCrunch.kind = AmbientKind::Moon;
  AmbientParams moon = calm;
  moon.kind = AmbientKind::Moon;
  CHECK(rmsAfterChange(moon, moonCrunch, 0.6) < rmsAfterChange(moon, moon, 0.6) * 1.5);  // only the desert hears it

  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("desert"), 160, 36, 3);
  CHECK(ambientFor(world).worm == 0.0f && ambientFor(world).crushes == 0);
  world.triggerRare(RarePick::Common);
  float nearest = 0;
  for (int i = 0; i < 1500; ++i) {
    world.step(0.05);
    nearest = std::max(nearest, ambientFor(world).worm);
  }
  CHECK(nearest > 0.9f);
  CHECK(ambientFor(world).crushes > 3);
}

TEST(the_sandworm_lashes_out_a_tongue_that_reaches_a_victim_before_it_is_crushed) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("desert"), 160, 36, 3);
  world.setTimeMode(World::TimeMode::Day);
  world.triggerRare(RarePick::Common);
  bool lashed = false, beforeHit = false, drawn = false;
  unsigned crushedAtStart = 0;
  for (int i = 0; i < 1500 && !(lashed && world.tongueCount() == 0); ++i) {
    world.step(0.05);
    if (world.tongueCount() == 0) continue;
    if (!lashed) {
      lashed = true;
      crushedAtStart = world.crushCount();
    }
    Frame f;
    world.render(f);
    bool tongue = false;
    for (int y = 0; y < f.height(); ++y) {
      for (int x = 0; x < f.width(); ++x) {
        const Cell& c = f.at(x, y);  // the tip: a red '@' (the storm dims the colors a little)
        tongue |= c.ch == U'@' && c.fg.r > c.fg.g * 1.6 && c.fg.r > c.fg.b * 1.3;
      }
    }
    drawn |= tongue;
    if (world.crushCount() == crushedAtStart) beforeHit = true;  // out first: the victim is still alive
  }
  CHECK(lashed && drawn && beforeHit);
  CHECK(world.crushCount() > crushedAtStart);  // and the lash crushed something
  CHECK(world.tongueCount() == 0);             // one tongue at a time, and it is gone afterwards
  // The alien, which has no reach, never lashes.
  World moon(pack, pack.scene("moon"), 120, 30, 5);
  moon.triggerRare(RarePick::Common);
  for (int i = 0; i < 600; ++i) {
    moon.step(0.05);
    CHECK(moon.tongueCount() == 0);
  }
}

TEST(the_kraken_gulps_down_what_its_tentacles_catch_with_a_muffled_sound) {
  AmbientParams calm;
  calm.kind = AmbientKind::Aquarium;
  AmbientParams gulp = calm;
  gulp.crushes = 1;
  CHECK(rmsAfterChange(calm, gulp, 0.6) > rmsAfterChange(calm, calm, 0.6) * 1.5);
  AmbientParams forest = calm;
  forest.kind = AmbientKind::Forest;
  AmbientParams forestGulp = forest;
  forestGulp.crushes = 1;
  CHECK(rmsAfterChange(forest, forestGulp, 0.6) < rmsAfterChange(forest, forest, 0.6) * 1.2);  // only the sea hears it
}

TEST(the_city_hums_with_traffic_honks_now_and_then_and_has_sirens_trains_planes_and_fire) {
  AmbientParams calm;
  calm.kind = AmbientKind::City;
  const double quiet = ambienceRms(calm, 20);
  CHECK(quiet > 0.01);
  AmbientParams night = calm;
  night.night = 1;
  CHECK(ambienceRms(night, 20) < quiet * 0.8);  // fewer cars at night
  AmbientParams siren = calm, train = calm, plane = calm, fire = calm;
  siren.siren = 1;
  train.train = 1;
  plane.plane = 1;
  fire.fire = 1;
  CHECK(ambienceRms(siren, 10) > quiet * 1.4);
  CHECK(ambienceRms(train, 10) > quiet * 1.3);
  CHECK(ambienceRms(plane, 10) > quiet * 1.2);
  CHECK(ambienceRms(fire, 10) > quiet * 1.4);
  // A horn is a short, loud burst above the traffic: the loudest 50 ms of a minute against the average.
  constexpr int kRate = 8000;
  Ambience a(kRate, 4);
  std::vector<float> buf(static_cast<size_t>(kRate * 60) * 2);
  a.render(calm, buf.data(), static_cast<int>(buf.size() / 2));
  double best = 0, sum = 0;
  for (size_t i = 0; i + 800 < buf.size(); i += 800) {
    double s = 0;
    for (size_t k = 0; k < 800; ++k) s += double(buf[i + k]) * buf[i + k];
    best = std::max(best, std::sqrt(s / 800));
    sum += s;
  }
  CHECK(best > std::sqrt(sum / double(buf.size())) * 1.5);

  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 130, 40, 3);
  CHECK(ambientFor(world).kind == AmbientKind::City);
  CHECK(ambientFor(world).siren == 0.0f && ambientFor(world).fire == 0.0f);
  CHECK(world.interact() == 1);  // the ambulance
  float nearest = 0;
  for (int i = 0; i < 400; ++i) {
    world.step(0.05);
    nearest = std::max(nearest, ambientFor(world).siren);
  }
  CHECK(nearest > 0.9f);
  world.triggerRare(RarePick::Common);
  float blaze = 0;
  for (int i = 0; i < 400; ++i) {
    world.step(0.05);
    blaze = std::max(blaze, ambientFor(world).fire);
  }
  CHECK(blaze == 1.0f);
}

TEST(the_fire_engine_wails_slowly_and_drives_off_without_its_siren) {
  AmbientParams calm;
  calm.kind = AmbientKind::City;
  AmbientParams wail = calm;
  wail.wail = 1;
  CHECK(ambienceRms(wail, 10) > ambienceRms(calm, 10) * 1.4);
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 150, 40, 3);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  CHECK(world.interact() == 1);  // the ambulance has the quick two-tone siren, the fire engine the slow wail
  float siren = 0;
  for (int i = 0; i < 400; ++i) {
    world.step(0.05);
    siren = std::max(siren, ambientFor(world).siren);
    CHECK(ambientFor(world).wail == 0.0f);  // the ambulance does not wail
  }
  CHECK(siren > 0.9f);
  world.triggerRare(RarePick::Common);
  float wails = 0;
  bool left = false, quiet = true;
  for (int i = 0; i < 20 * 100; ++i) {
    world.step(0.05);
    wails = std::max(wails, ambientFor(world).wail);
    for (auto& e : world.entities()) {
      if (e->dead || e->sprite->name() != "firetruck" || e->driveState != 2) continue;
      left = true;
      quiet &= e->tag.empty();  // leaving: the siren is off
    }
  }
  CHECK(wails > 0.9f);
  CHECK(left && quiet);
  CHECK(ambientFor(world).wail == 0.0f);
}

TEST(a_train_beats_out_klack_klack_pairs_with_a_pause_between) {
  constexpr int kRate = 8000;
  Ambience a(kRate, 4);
  AmbientParams p;
  p.kind = AmbientKind::City;
  p.train = 1;
  std::vector<float> buf(static_cast<size_t>(kRate * 14) * 2);
  a.render(p, buf.data(), static_cast<int>(buf.size() / 2));
  std::vector<double> env;  // 5 ms envelope of the left ear
  for (size_t s = 0; s < 14 * 200; ++s) {
    double e = 0;
    for (size_t i = 0; i < kRate / 200; ++i) e += double(buf[(s * kRate / 200 + i) * 2]) * buf[(s * kRate / 200 + i) * 2];
    env.push_back(std::sqrt(e / (kRate / 200)));
  }
  double peak = 0;
  for (size_t i = 1600; i < env.size(); ++i) peak = std::max(peak, env[i]);  // after the horn has sounded
  std::vector<double> onsets;
  for (size_t i = 1601; i < env.size(); ++i) {
    if (env[i] > 0.55 * peak && env[i - 1] <= 0.55 * peak && (onsets.empty() || i / 200.0 - onsets.back() > 0.08)) onsets.push_back(i / 200.0);
  }
  CHECK(onsets.size() >= 10);
  // Intervals alternate: a short one inside a pair (about 0.18 s), a long one between pairs (about 0.35 s).
  int shortThenLong = 0, pairs = 0;
  for (size_t i = 1; i + 1 < onsets.size(); ++i) {
    const double d1 = onsets[i] - onsets[i - 1], d2 = onsets[i + 1] - onsets[i];
    if (d1 > 0.12 && d1 < 0.26 && d2 > 0.28 && d2 < 0.5) ++shortThenLong;
    if (d1 > 0.12 && d1 < 0.26) ++pairs;
  }
  CHECK(pairs >= 5 && shortThenLong >= 4);
}

TEST(an_animal_buried_by_an_avalanche_squeals_muffled_and_the_world_counts_it) {
  AmbientParams calm;
  calm.kind = AmbientKind::Mountains;
  AmbientParams buried = calm;
  buried.squeals = 1;
  CHECK(rmsAfterChange(calm, buried, 0.5) > rmsAfterChange(calm, calm, 0.5) * 1.3);
  AssetPack pack = AssetPack::embedded();
  World world(pack, quietMountains(pack), 140, 40, 7);
  world.setTimeMode(World::TimeMode::Day);
  CHECK(world.squealCount() == 0);
  world.triggerRare(RarePick::Common);
  for (int i = 0; i < 1200; ++i) world.step(0.05);
  CHECK(world.squealCount() > 0 && world.squealCount() <= world.killedCount());  // animals only, not the trees
  CHECK(ambientFor(world).squeals == world.squealCount());
}

/// A session over the built-in scenes, 100 by 30 cells.
/// Starts in the user mode unless `cycle` asks for the cycle mode (the program's default).
static Session makeSession(const AssetPack& pack, Session::Config config = {}, bool cycle = false) {
  config.cycle = cycle;
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

TEST(the_mode_key_toggles_the_cycle_mode_and_switching_it_on_sets_the_clock_to_cycle) {
  AssetPack pack = AssetPack::embedded();
  Session::Config config;
  config.scene = 0;
  Session s = makeSession(pack, config);
  CHECK(!s.cycling());
  s.handle(Action::Time);  // user mode: the time of day is chosen by hand
  s.step(0.05);
  const auto chosen = s.world().timeMode();
  CHECK(chosen != World::TimeMode::Cycle);
  s.handle(Action::Mode);
  s.step(0.05);
  CHECK(s.cycling() && s.world().timeMode() == World::TimeMode::Cycle);  // the clock runs by itself
  CHECK(s.menu().value("time") == static_cast<size_t>(World::TimeMode::Cycle) && s.menu().value("mode") == 1);
  s.handle(Action::Mode);  // back to the user mode
  s.step(0.05);
  CHECK(!s.cycling() && s.world().timeMode() == chosen && s.menu().value("mode") == 0);  // what the user had chosen is back
}

TEST(any_manual_choice_ends_the_cycle_mode_and_is_applied) {
  AssetPack pack = AssetPack::embedded();
  Session::Config config;
  config.scene = 0;
  {  // the time of day key: the clock steps on from "cycle" to dawn
    Session s = makeSession(pack, config, true);
    CHECK(s.cycling());
    s.handle(Action::Time);
    s.step(0.05);
    CHECK(!s.cycling() && s.world().timeMode() == World::TimeMode::Dawn && s.menu().value("mode") == 0);
  }
  {  // a rare event (x)
    Session s = makeSession(pack, config, true);
    s.handle(Action::Rare);
    CHECK(!s.cycling() && s.world().raresStarted() == 1);
  }
  {  // the other rare event (X)
    Session s = makeSession(pack, config, true);
    s.handle(Action::RareOther);
    CHECK(!s.cycling() && s.world().raresStarted() == 1);
  }
  {  // the action (f)
    Session s = makeSession(pack, config, true);
    s.handle(Action::Interact);
    CHECK(!s.cycling() && s.actionsRun() == 1);
  }
  {  // the menu: choosing a time of day (the mode shows "User", the time is the one chosen)
    Session s = makeSession(pack, config, true);
    s.handle(Action::Menu);
    s.handle(Action::Down);
    s.handle(Action::Down);
    s.handle(Action::Down);  // scene, style, mode, time
    s.handle(Action::Right);  // cycle -> dawn
    s.step(0.05);
    CHECK(!s.cycling() && s.world().timeMode() == World::TimeMode::Dawn);
    CHECK(s.menu().value("mode") == 0 && s.menu().value("time") == static_cast<size_t>(World::TimeMode::Dawn));
  }
}

TEST(cycle_mode_runs_a_random_action_within_ninety_seconds) {
  AssetPack pack = AssetPack::embedded();
  const auto scenes = pack.scenes();
  size_t aquarium = 0;
  for (size_t i = 0; i < scenes.size(); ++i) aquarium = scenes[i]->name == "aquarium" ? i : aquarium;
  Session::Config config;
  config.scene = aquarium;
  Session s = makeSession(pack, config, true);
  bool food = false;
  double t = 0;
  for (; t < 95 && !food; t += 0.1) {
    s.step(0.1);
    food = s.world().countTag("food") > 0;
  }
  CHECK(food && t >= 10);  // and not before ten seconds
}

TEST(cycle_mode_shows_every_scene_a_full_day_a_rare_event_and_an_action_before_moving_on) {
  AssetPack pack = AssetPack::embedded();
  const auto scenes = pack.scenes();
  Session::Config config;
  Session s = makeSession(pack, config, true);
  for (size_t visit = 0; visit < scenes.size(); ++visit) {
    const size_t scene = s.sceneIndex();
    CHECK(scene == visit);  // in the order of the menu
    double t = 0;
    unsigned rares = 0;
    double days = 0;
    unsigned acts = 0;
    while (s.sceneIndex() == scene && t < 3000) {
      s.step(0.1);
      t += 0.1;
      if (s.sceneIndex() != scene) break;
      rares = s.world().raresStarted();
      days = s.world().daysElapsed();
      acts = s.actionsRun();
    }
    CHECK(s.sceneIndex() == (scene + 1) % scenes.size());  // it did move on
    CHECK(days >= 0.9);  // sampled one step before the switch
    if (!scenes[scene]->rare.empty()) CHECK(rares >= 1);
    CHECK(t >= scenes[scene]->dayLength);
    if (scenes[scene]->action.kind != SceneDef::Action::Kind::None) CHECK(acts >= 1);
  }
}

TEST(crossfade_dissolves_one_frame_into_the_other_and_blends_plain_colors) {
  Frame a(20, 10), b(20, 10);
  for (int y = 0; y < 10; ++y) {
    for (int x = 0; x < 20; ++x) {
      a.at(x, y).ch = U'a';
      a.at(x, y).fg = a.at(x, y).bg = Rgb{0, 0, 0};
      b.at(x, y).ch = U'b';
      b.at(x, y).fg = b.at(x, y).bg = Rgb{200, 100, 50};
    }
  }
  auto count = [](const Frame& f, char32_t ch) {
    int n = 0;
    for (int y = 0; y < f.height(); ++y)
      for (int x = 0; x < f.width(); ++x) n += f.at(x, y).ch == ch;
    return n;
  };
  Frame start = b, mid = b, end = b;
  crossfade(a, start, 0.0);
  crossfade(a, mid, 0.5);
  crossfade(a, end, 1.0);
  CHECK(count(start, U'a') == 200);  // only the old picture
  CHECK(count(end, U'b') == 200);    // only the new one
  CHECK(count(mid, U'a') > 50 && count(mid, U'b') > 50);  // a mixture
  CHECK(mid.at(3, 3).fg == (Rgb{100, 50, 25}));           // colors halfway
  Frame small(5, 5), same = small;
  crossfade(a, small, 0.5);  // different sizes: left alone
  CHECK(small.toText() == same.toText());
}

TEST(cycle_mode_crossfades_picture_and_sound_into_the_next_scene) {
  AssetPack pack = AssetPack::embedded();
  Session::Config config;
  Session s = makeSession(pack, config, true);
  const size_t first = s.sceneIndex();
  CHECK(!s.fading());
  double t = 0;
  while (s.sceneIndex() == first && t < 3000) { s.step(0.1); t += 0.1; }
  CHECK(s.sceneIndex() != first);
  s.step(0.1);  // the new world is built, the old one stays
  CHECK(s.fading());
  Frame f;
  s.render(f);
  CHECK(f.width() == 100);
  // Sound: the old scene fades out, then the new one fades in; silent at the midpoint.
  float lowest = 1;
  bool quieter = false;
  for (int i = 0; i < 25 && s.fading(); ++i) {
    s.step(0.1);
    const float v = s.ambient().volume;
    lowest = std::min(lowest, v);
    quieter = quieter || v < 0.9f;
  }
  CHECK(quieter && lowest < 0.15f);
  for (int i = 0; i < 25; ++i) s.step(0.1);
  CHECK(!s.fading() && s.ambient().volume == 1.0f);
}

TEST(user_scene_switches_and_resizes_do_not_crossfade) {
  AssetPack pack = AssetPack::embedded();
  Session s = makeSession(pack);
  s.handle(Action::Right);
  s.step(0.05);
  CHECK(!s.fading());
  Session c = makeSession(pack, {}, true);
  double t = 0;
  const size_t first = c.sceneIndex();
  while (c.sceneIndex() == first && t < 3000) { c.step(0.1); t += 0.1; }
  c.step(0.1);
  CHECK(c.fading());
  c.resize(80, 24);
  c.step(0.05);
  CHECK(!c.fading());
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

TEST(summon_calls_one_visitor_at_a_time_and_feet_rows_and_traffic_place_vehicles_on_their_side) {
  AssetPack pack = miniPack(
      "medium: no\nsky-fraction: 1\nground-rows: 7\naction: summon van\n--- population\ncar count 40\n",
      {"name: van\nfacing: right\nhabitat: ground\nspeed: 8..8\nfeet-rows: 1..-2\ntraffic: yes\nbehaviors: swim\n--- art\nVV\n",
       "name: car\nfacing: right\nhabitat: ground\nspeed: 4..6\nfeet-rows: 1..-2\ntraffic: yes\nbehaviors: swim\n--- art\nCC\nCC\n"});
  World world(pack, pack.scene("t"), 100, 30, 3);
  CHECK(world.scene().action.kind == SceneDef::Action::Kind::Summon);
  CHECK(world.interact() == 1 && world.countSprite("van") == 1);
  CHECK(world.interact() == 0);  // one at a time
  const int rows = world.height() - world.seabedY();
  const int mid = (1 + (rows - 2) + 1) / 2;
  for (auto& e : world.entities()) {
    if (e->sprite->name() != "car") continue;
    const int feet = static_cast<int>(e->y) + e->height() - 1 - world.seabedY();  // ground row of the feet
    CHECK(feet >= 1 && feet <= rows - 2);                                         // on the road, not the sidewalks
    CHECK(e->movingRight() ? feet >= mid : feet < mid);                           // right: lower half, left: upper
  }
  for (int i = 0; i < 400 && world.countSprite("van") > 0; ++i) world.step(0.05);
  CHECK(world.countSprite("van") == 0);  // it drove through and was not replaced
  CHECK(world.interact() == 1);          // and may be called again
  std::string err;
  CHECK(SceneDef::parse("name: s\naction: summon\n", err) == nullptr);  // needs a sprite
}

TEST(the_city_has_a_skyline_lower_than_the_mountains_a_street_and_a_railway) {
  AssetPack pack = AssetPack::embedded();
  World city(pack, pack.scene("city"), 130, 40, 3);
  World mountains(pack, pack.scene("mountains"), 130, 40, 3);
  CHECK(city.city() && city.buildingCount() > 10);
  int peak = 40;
  for (int x = 0; x < 130; ++x) peak = std::min(peak, mountains.ridgeRow(x));
  CHECK(city.skylineTop() > peak);          // no skyscraper reaches as high as the highest peak
  CHECK(city.skylineTop() <= city.seabedY() - static_cast<int>(city.seabedY() * 0.6));  // but they fill a good part of the sky
  CHECK(city.railRow() >= 0 && city.railRow() < city.seabedY());
  Frame f;
  city.setHour(12);
  city.render(f);
  const Rgb road = pack.scene("city")->groundB;
  int roadCells = 0;
  for (int y = city.seabedY(); y < 40; ++y) {
    for (int x = 0; x < 130; ++x) roadCells += (f.at(x, y).bg == road) ? 1 : 0;
  }
  CHECK(roadCells > 130);  // asphalt, not the plain ground
  CHECK(city.height() - city.seabedY() == 4);  // the main road is a narrow street
  World tiny(pack, pack.scene("city"), 40, 8, 3);  // too small for a skyline or a railway
  CHECK(tiny.buildingCount() == 0 && tiny.railRow() < 0);
  CHECK(tiny.countSprite("station") == 0);
}

TEST(the_railway_is_a_visible_viaduct_and_the_vehicles_and_planes_are_detailed_line_art) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 130, 40, 3);
  Frame f;
  world.setHour(12);
  world.render(f);
  const int rail = world.railRow();
  CHECK(rail > 0);
  // A dark deck with bright rails, a concrete beam below it, pillars further down and a wire above.
  int deck = 0, beam = 0, pillars = 0, wire = 0;
  for (int x = 0; x < 130; ++x) {
    deck += (f.at(x, rail).ch == U'=' && f.at(x, rail).bg.r < 80) ? 1 : 0;
    beam += (f.at(x, rail + 1).ch == U'_' && f.at(x, rail + 1).bg.r > 100) ? 1 : 0;
    pillars += f.at(x, rail + 3).bg.r > 100 && f.at(x, rail + 3).bg.r < 150 && f.at(x, rail + 3).ch == U' ' ? 1 : 0;
    wire += f.at(x, rail - 7).ch == U'-' ? 1 : 0;
  }
  CHECK(deck > 90 && beam > 90 && pillars > 10 && wire > 40);
  // Vehicles, trains and planes are fine line art with many different glyphs, on a darkened backing
  // so that they stand out against any background.
  for (const char* name : {"car_sedan", "car_hatch", "taxi", "bus", "truck", "ambulance", "firetruck", "train", "plane"}) {
    auto sprite = pack.find(name);
    CHECK(sprite != nullptr);
    if (!sprite) continue;
    std::set<char32_t> glyphs;
    size_t cells = 0;
    for (const auto& row : sprite->frame(false, 0).art) {
      for (char32_t ch : row) {
        if (ch == U' ') continue;
        glyphs.insert(ch);
        ++cells;
      }
    }
    CHECK(glyphs.size() >= 6 && cells > 20);
    CHECK(sprite->props().num("backing", 0) >= 0.4);
  }
  CHECK(pack.find("plane")->width() >= 26 && pack.find("plane")->height() >= 6);  // a fuselage with a wing above and below it
  CHECK(pack.find("firetruck")->width() >= 16 && pack.find("train")->height() >= 4);
}

TEST(side_streets_run_off_into_the_city_and_cars_turn_into_and_out_of_them) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 150, 40, 3);
  CHECK(world.avenues().size() >= 1);
  World narrow(pack, pack.scene("city"), 50, 40, 3);
  CHECK(narrow.avenues().empty());  // no room for a side street
  World low(pack, pack.scene("city"), 150, 14, 3);
  CHECK(low.avenues().empty());
  // Nothing stands in the mouth of a side street, and the street is drawn: dashes up the middle and
  // a crossing where it meets the main street.
  for (const auto& a : world.avenues()) {
    for (auto& e : world.entities()) {
      if (e->dead || e->life || e->vx != 0 || !e->grounded || e->width() < 2) continue;
      CHECK(e->x + e->width() <= a.cx - a.halfW - 3 || e->x >= a.cx + a.halfW + 3);
    }
  }
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  Frame f;
  {
    World bare(pack, pack.scene("city"), 150, 40, 3);  // the same streets with nobody on them
    bare.entities().clear();
    bare.setHour(12);
    bare.setTimeMode(World::TimeMode::Day);
    bare.render(f);
  }
  const auto& a = world.avenues().front();
  int dashes = 0, crossing = 0;
  for (int y = world.seabedY() - a.depth; y < world.seabedY() - 1; ++y) dashes += f.at(a.cx, y).ch == U':' ? 1 : 0;
  for (int x = a.cx - a.halfW; x <= a.cx + a.halfW; ++x) crossing += f.at(x, world.seabedY() - 1).ch == U'=' ? 1 : 0;
  CHECK(dashes >= 1);              // the dashed middle line up the side street
  CHECK(crossing >= a.halfW);      // and a wide zebra crossing where it meets the main street
  CHECK(a.halfW >= 6 && a.depth <= 7);  // a broad crossing, and a street that ends quickly
  bool turnedIn = false, up = false, down = false, out = false, back = false;
  for (int i = 0; i < 20 * 150; ++i) {
    world.step(0.05);
    for (auto& e : world.entities()) {
      if (e->dead) continue;
      if (e->driveState == 1) turnedIn = true;
      if (e->driveState == 2) {
        up = true;
        const int mid = static_cast<int>(e->x + e->width() / 2.0);
        bool inAvenue = false;
        for (const auto& av : world.avenues()) inAvenue |= std::abs(mid - av.cx) <= 1;
        CHECK(inAvenue);                               // up the side street, not across the buildings
        CHECK(e->y + e->height() - 1 <= world.seabedY());  // out of the main road, heading up
      }
      if (e->driveState == 3) down = true;
      if (e->driveState == 4) out = true;
      if (e->driveState == 0 && e->driveSpeed > 0 && e->driveAvenue == -1 && e->vx * e->driveDir > 0) back = true;
    }
  }
  CHECK(turnedIn && up && down && out && back);  // in, up, down, out, and on along the main road
}

TEST(sometimes_the_fire_gets_out_of_hand_spreads_and_burns_buildings_down_which_are_rebuilt_slowly) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 200, 40, 3);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  world.forceFireOutcome(true);
  CHECK(world.triggerRare(RarePick::Common));
  size_t mostTroubled = 0, mostRuined = 0;
  double engineLeft = -1, lastBurn = -1, allRebuilt = -1;
  bool cordonedWhileRuined = false, sprayedNoEffect = false;
  for (int i = 0; i < 20 * 240; ++i) {
    world.step(0.05);
    const double t = i * 0.05;
    mostTroubled = std::max(mostTroubled, world.troubledBuildings());
    mostRuined = std::max(mostRuined, world.ruinedBuildings());
    if (world.fireBurning()) lastBurn = t;
    cordonedWhileRuined |= world.ruinedBuildings() > 0 && world.cordoned();
    for (auto& e : world.entities()) {
      if (e->dead || e->sprite->name() != "firetruck") continue;
      sprayedNoEffect |= e->driveState == 1 && world.ruinedBuildings() == 0 && world.fireBurning();
      if (e->driveState == 2 && engineLeft < 0) {
        engineLeft = t;
        CHECK(!world.fireBurning());  // it goes as soon as the last fire is out
      }
    }
    if (allRebuilt < 0 && world.troubledBuildings() == 0 && t > 30) allRebuilt = t;
  }
  CHECK(mostTroubled >= 4 && mostTroubled <= 6);  // the first building and 3 to 5 more
  CHECK(mostRuined >= 3);                         // burnt down, not just burning
  CHECK(sprayedNoEffect && cordonedWhileRuined);
  CHECK(engineLeft > 0 && engineLeft >= lastBurn - 0.5 && engineLeft < lastBurn + 3);  // as soon as it is out
  CHECK(allRebuilt > lastBurn + 40);   // and it takes a long while to rebuild everything
  CHECK(world.troubledBuildings() == 0 && !world.cordoned() && world.fireLevel() == 0);
  // The rubble is low: a burnt-down building shows only a few rows.
  World ruin(pack, pack.scene("city"), 200, 40, 3);
  ruin.setTimeMode(World::TimeMode::Day);
  ruin.forceFireOutcome(true);
  ruin.triggerRare(RarePick::Common);
  for (int i = 0; i < 20 * 70 && ruin.ruinedBuildings() == 0; ++i) ruin.step(0.05);
  CHECK(ruin.ruinedBuildings() > 0);
  for (int i = 0; i < 20 * 6; ++i) ruin.step(0.05);  // it has fallen in
  Frame f;
  ruin.render(f);
  int rubble = 0;
  for (int y = 0; y < ruin.height(); ++y) for (int x = 0; x < 200; ++x) rubble += (f.at(x, y).ch == U'#' || f.at(x, y).ch == U'%') && f.at(x, y).bg.r < 80 ? 1 : 0;
  CHECK(rubble > 6);
}

TEST(cars_in_the_side_streets_pass_under_the_viaduct) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 150, 40, 3);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  bool under = false;
  for (int i = 0; i < 20 * 150 && !under; ++i) {
    world.step(0.05);
    for (auto& e : world.entities()) {
      if (e->dead || e->driveState < 1 || e->driveState > 3) continue;
      CHECK(e->z == 24);  // drawn after the side street and before the viaduct
      const int rail = world.railRow();
      if (e->y <= rail && e->y + e->height() - 1 >= rail) {  // the car is level with the deck
        Frame f;
        world.render(f);
        const int x = static_cast<int>(e->x + e->width() / 2.0);
        if (x >= 0 && x < 150) {
          CHECK(f.at(x, rail).ch == U'=');  // the deck is in front of it
          under = true;
        }
      }
    }
  }
  CHECK(under);
}

TEST(a_fire_engine_parks_at_the_fire_sprays_it_and_the_site_is_cordoned_off_until_it_is_renovated) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 150, 40, 3);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  world.forceFireOutcome(false);
  CHECK(world.triggerRare(RarePick::Common));
  CHECK(!world.cordoned());
  bool parked = false, sprayed = false, left = false, cordonWhileLeft = false, everCordoned = false, barriers = false;
  size_t waiting = 0;
  for (int i = 0; i < 20 * 100; ++i) {
    std::vector<std::pair<Entity*, double>> before;  // left edges before the step
    for (auto& e : world.entities()) before.push_back({e.get(), e->x});
    world.step(0.05);
    if (world.cordoned()) everCordoned = true;
    const double lo = world.cordonCenter() - world.cordonHalfWidth(), hi = world.cordonCenter() + world.cordonHalfWidth();
    if (world.cordoned()) barriers |= world.countSprite("barrier") >= 8;  // across road and sidewalks at both ends
    else CHECK(world.countSprite("barrier") == 0 || !everCordoned);       // and gone when it is open again
    for (auto& e : world.entities()) {
      if (e->dead) continue;
      if (e->sprite->name() == "firetruck") {
        if (e->driveState == 1) {
          parked = true;
          CHECK(e->vx == 0 && world.cordoned());
          CHECK(std::fabs(e->x + e->width() / 2.0 - world.cordonCenter()) < 3);  // right in front of the building
          sprayed |= world.countSprite("water") > 0 && world.fireLevel() > 0;
        }
        if (e->driveState == 2) {
          left = true;
          cordonWhileLeft |= world.cordoned();  // the engine goes as soon as the fire is out; the site stays closed
          CHECK(!world.fireBurning());
        }
        continue;
      }
      if (!world.cordoned() || !e->grounded || e->vx == 0 || e->driveState != 0 || e->sprite->name() == "barrier") continue;
      for (const auto& b : before) {
        if (b.first != e.get()) continue;
        const bool was = b.second < hi && b.second + e->width() > lo, now = e->x < hi && e->x + e->width() > lo;
        if (!was && now) CHECK(false);  // reached into the site
      }
    }
    if (world.cordoned()) {
      for (auto& e : world.entities()) waiting += (!e->dead && e->grounded && e->pauseLeft > 0 && e->sprite->name() != "firetruck") ? 1 : 0;
    }
  }
  CHECK(parked && sprayed && left && cordonWhileLeft && everCordoned && barriers);
  CHECK(waiting > 0);  // somebody had to wait at the edge
  CHECK(!world.cordoned() && world.troubledBuildings() == 0 && world.countSprite("barrier") == 0);
}

TEST(cars_show_the_side_the_back_or_the_front_depending_on_how_they_face_the_camera) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 150, 40, 3);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  std::set<size_t> seen;
  for (int i = 0; i < 20 * 150; ++i) {
    world.step(0.05);
    for (auto& e : world.entities()) {
      if (e->dead || e->sprite->frameCount() < 5) continue;
      seen.insert(e->frame);
      if (e->driveState == 0 && e->driveAvenue != -2 && e->driveAvenue < 0 && e->vx != 0) CHECK(e->frame == 0);  // side view on the road
      if (e->driveState == 2) CHECK(e->frame == 2);                       // driving away: from behind
      if (e->driveState == 1) CHECK(e->frame <= 2);                       // turning away: side, three-quarters, behind
      if (e->driveState == 3) CHECK(e->frame >= 3);                       // coming: from the front
      if (e->driveState == 4) CHECK(e->frame == 0 || e->frame == 3);      // turning out: three-quarters, then side
    }
  }
  CHECK(seen.size() == 5);  // every view is used
}

TEST(traffic_peaks_in_the_morning_and_evening_rush_hours) {
  AssetPack pack = AssetPack::embedded();
  auto vehicles = [&](double hour) {
    World world(pack, pack.scene("city"), 200, 40, 3);
    world.setTimeMode(World::TimeMode::Realtime);
    world.setHour(hour);
    for (int i = 0; i < 20 * 150; ++i) world.step(0.05);  // long enough for the street to fill or empty
    size_t n = 0;
    for (auto& e : world.entities()) {
      const std::string& name = e->sprite->name();
      n += (!e->dead && (name == "car_sedan" || name == "car_hatch" || name == "taxi" || name == "bus" || name == "truck")) ? 1 : 0;
    }
    return n;
  };
  const size_t morning = vehicles(8), midday = vehicles(13), evening = vehicles(17.5), night = vehicles(3);
  CHECK(morning > midday + 2 && evening > midday + 2);  // the rush hours are busier than midday
  CHECK(midday > night);                                // and midday busier than the night
  AssetPack p2 = AssetPack::embedded();
  World world(p2, p2.scene("city"), 200, 40, 3);
  world.setTimeMode(World::TimeMode::Realtime);
  world.setHour(8);
  CHECK(world.trafficLevel() > 0.95);
  world.setHour(13);
  CHECK(world.trafficLevel() < 0.5);
  world.setHour(3);
  CHECK(world.trafficLevel() < 0.1);
}

TEST(lettering_on_a_sprite_stays_readable_when_the_sprite_is_mirrored) {
  std::string err;
  auto s = Sprite::parse("name: t\n--- art\n<[TAXI]>\n--- mask\nabcdefgh\n", err);
  CHECK(s != nullptr);
  CHECK(s->frame(true, 0).art[0] == U"<[TAXI]>");   // not "<[IXAT]>"
  CHECK(s->frame(true, 0).mask[0] == U"hgfedcba" || s->frame(true, 0).mask[0].size() == 8);
}

TEST(there_are_not_too_many_vehicles_and_the_front_row_leaves_gaps_with_streets_behind) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 150, 40, 3);
  size_t vehicles = 0;
  for (auto& e : world.entities()) {
    const std::string& n = e->sprite->name();
    vehicles += (n == "car_sedan" || n == "car_hatch" || n == "taxi" || n == "bus" || n == "truck") ? 1 : 0;
  }
  CHECK(vehicles <= 16);
  World wide(pack, pack.scene("city"), 220, 40, 3);
  CHECK(!wide.glimpses().empty());  // a street seen behind a gap in the front row
  Frame f;
  {
    World bare(pack, pack.scene("city"), 220, 40, 3);
    bare.entities().clear();
    bare.setHour(12);
    bare.setTimeMode(World::TimeMode::Day);
    bare.render(f);
    for (const auto& g : bare.glimpses()) {
      int dashes = 0;
      for (int y = bare.seabedY() - g.depth; y < bare.seabedY(); ++y) dashes += f.at(g.cx, y).ch == U':' ? 1 : 0;
      CHECK(dashes >= 1);  // it shows between the buildings, and is not hidden by them
    }
  }
}

TEST(emergency_vehicles_have_large_light_bars_that_blink_between_red_and_blue) {
  AssetPack pack = AssetPack::embedded();
  for (const char* name : {"ambulance", "firetruck"}) {
    auto sprite = pack.find(name);
    CHECK(sprite != nullptr && sprite->frameCount() == 2);
    if (!sprite || sprite->frameCount() != 2) continue;
    auto count = [&](size_t frame, char32_t letter) {
      size_t n = 0;
      for (size_t row = 0; row < 2; ++row) {  // the bar is the top two rows
        for (char32_t m : sprite->frame(false, frame).mask[row]) n += m == letter ? 1 : 0;
      }
      return n;
    };
    CHECK(count(0, U'r') >= 6 && count(0, U'b') >= 6);  // twelve cells of light, half red and half blue
    CHECK(count(0, U'r') == count(1, U'b') && count(0, U'b') == count(1, U'r'));  // and they swap
  }
  CHECK(pack.find("firetruck")->props().str("tag") == "wail" && pack.find("ambulance")->props().str("tag") == "siren");
}

TEST(the_skyline_is_varied_in_height_look_and_roof) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 200, 44, 3);
  Frame f;
  world.setHour(12);
  world.render(f);
  // The silhouette: the highest non-sky row of every column; it should differ a lot between columns.
  const Rgb sky = pack.scene("city")->skyLow;
  std::set<int> tops;
  for (int x = 0; x < 200; ++x) {
    int top = world.seabedY();
    for (int y = 0; y < world.seabedY(); ++y) {
      const Cell& c = f.at(x, y);
      if (c.bg != sky && c.bg.r < 215 && c.bg.b < 200 && std::abs(c.bg.r - c.bg.b) < 90 && c.ch != U'*') { top = y; break; }
    }
    tops.insert(top);
  }
  CHECK(tops.size() >= 8);  // many different heights
}

TEST(the_cities_windows_and_street_lamps_light_up_at_night) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 130, 40, 3);
  world.entities().clear();  // nobody in the way of the lamps
  auto render = [&](double hour) {
    world.setTimeMode(World::TimeMode::Cycle);
    world.setHour(hour);
    Frame f;
    world.render(f);
    return f;
  };
  auto litWindows = [&](const Frame& f) {
    int n = 0;
    for (int y = 0; y < world.seabedY(); ++y) {
      for (int x = 0; x < 130; ++x) n += f.at(x, y).bg == Rgb{255, 214, 120} ? 1 : 0;
    }
    return n;
  };
  auto brightness = [](Rgb c) { return c.r + c.g + c.b; };
  const Frame day = render(12), night = render(0);
  CHECK(litWindows(day) == 0);    // by day the windows are just glass
  CHECK(litWindows(night) > 40);  // by night many are lit
  // The cell under the first street lamp (column 6) against one halfway between two lamps.
  const int y = world.seabedY() + 2;
  CHECK(brightness(night.at(6, y).bg) > brightness(night.at(14, y).bg) + 30);  // a pool of light at night
  CHECK(std::abs(brightness(day.at(6, y).bg) - brightness(day.at(14, y).bg)) < 20);
}

TEST(trains_run_on_the_railway_and_planes_come_in_to_land_behind_the_skyline) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 130, 40, 3);
  CHECK(world.countSprite("station") == 1);
  bool train = false, plane = false;
  for (int i = 0; i < 20000 && !(train && plane); ++i) {
    world.step(0.05);
    for (auto& e : world.entities()) {
      if (e->dead) continue;
      if (e->sprite->name() == "train") {
        train = true;
        CHECK(e->y + e->height() == world.railRow());  // on the rails
      }
      if (e->sprite->name() == "plane") {
        plane = true;
        CHECK(e->vy > 0 && e->z >= 25);  // sinking, and drawn behind the skyline
      }
    }
  }
  CHECK(train && plane);
}

/// Number of live entities of a decoration sprite (the trees are three sizes).
static size_t treeCount(const World& world) {
  return world.countSprite("oak") + world.countSprite("oak_small") + world.countSprite("oak_tall");
}

TEST(the_station_trees_and_crossings_differ_from_seed_to_seed_and_keep_clear_of_the_side_streets) {
  AssetPack pack = AssetPack::embedded();
  std::set<int> stations;
  std::set<size_t> trees;
  std::set<std::string> kinds;
  for (uint64_t seed = 1; seed <= 12; ++seed) {
    World world(pack, pack.scene("city"), 160, 40, seed);
    double station = -1;
    for (auto& e : world.entities()) {
      if (e->tag == "station") station = e->x + e->width() / 2.0;
      if (e->sprite->name().rfind("oak", 0) == 0) {
        kinds.insert(e->sprite->name());
        for (const auto& a : world.avenues()) {  // no tree in the mouth of a side street
          CHECK(e->x + e->width() <= a.cx - a.halfW || e->x >= a.cx + a.halfW);
        }
      }
    }
    CHECK(world.countSprite("station") == 1);
    stations.insert(static_cast<int>(station));
    trees.insert(treeCount(world));
    CHECK(treeCount(world) >= 1);
    // One crossing lies at the foot of the station, and none in a side street.
    CHECK(std::count(world.crossings().begin(), world.crossings().end(), static_cast<int>(station) - 1) == 1);
    for (int c : world.crossings()) {
      if (c == static_cast<int>(station) - 1) continue;
      for (const auto& a : world.avenues()) CHECK(std::abs(c + 1 - a.cx) > a.halfW);
    }
  }
  CHECK(stations.size() >= 5);  // the station moves
  CHECK(trees.size() >= 3);     // the number of trees varies
  CHECK(kinds.size() == 3);     // and so does their size
  // A wider screen has more trees than a narrow one.
  size_t narrow = 0, wide = 0;
  for (uint64_t seed = 1; seed <= 8; ++seed) {
    narrow += treeCount(World(pack, pack.scene("city"), 90, 40, seed));
    wide += treeCount(World(pack, pack.scene("city"), 240, 40, seed));
  }
  CHECK(wide > narrow);
}

TEST(a_train_stops_at_the_station_and_its_passengers_get_off_and_walk_down_to_the_street) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 130, 40, 3);
  bool stopped = false, passengers = false;
  for (int i = 0; i < 20000 && !(stopped && passengers); ++i) {
    world.step(0.05);
    double station = -1;
    for (auto& e : world.entities()) {
      if (!e->dead && e->tag == "station") station = e->x + e->width() / 2.0;
    }
    for (auto& e : world.entities()) {
      if (e->dead) continue;
      if (e->sprite->name() == "train" && e->vx == 0 && e->stopPhase == 2) {
        stopped = true;
        CHECK(std::abs(e->x + e->width() / 2.0 - station) < 1.0);  // level with the platform
      }
      if (e->sprite->name() == "commuter") {
        passengers = true;
        CHECK(e->y + e->height() == world.railRow());  // on the platform deck
      }
    }
  }
  CHECK(stopped && passengers);
  // They all reach the station, and nobody is left on the deck; the train pulls out again.
  bool left = false;
  for (int i = 0; i < 2000 && !left; ++i) {
    world.step(0.05);
    left = world.countSprite("commuter") == 0;
  }
  CHECK(left);
}

TEST(the_oasis_pond_changes_in_size_and_place_with_every_seed_and_the_palms_stay_on_its_shore) {
  AssetPack pack = AssetPack::embedded();
  std::set<std::string> kinds;
  std::set<int> places;
  for (uint64_t seed = 1; seed <= 24; ++seed) {
    World world(pack, pack.scene("desert"), 120, 30, seed);
    const Entity* pond = nullptr;
    for (auto& e : world.entities()) {
      if (!e->dead && e->sprite->name().rfind("oasis", 0) == 0) pond = e.get();
    }
    CHECK(pond != nullptr);
    if (!pond) continue;
    kinds.insert(pond->sprite->name());
    places.insert(static_cast<int>(pond->x));
    CHECK(pond->x >= 0 && pond->x + pond->width() <= 120);
    // The reeds that are placed relative to the pond lie inside it, and some palm stands at its left edge.
    bool reedsInside = false, palmAtEdge = false;
    for (auto& e : world.entities()) {
      if (e->dead) continue;
      if (e->sprite->name() == "reeds" && e->decor && e->x >= pond->x - 0.5 && e->x + e->width() <= pond->x + pond->width() + 0.5) reedsInside = true;
      if (e->sprite->name() == "palm_big" && e->decor && std::abs(e->x - (pond->x - 6)) < 0.5) palmAtEdge = true;
    }
    CHECK(reedsInside);
    CHECK(palmAtEdge || pond->x < 6);  // (a pond at the very edge of the screen pushes the palm off it)
  }
  CHECK(kinds.size() == 3);   // small, medium and big
  CHECK(places.size() >= 8);  // and in many places
}

TEST(a_train_stops_rumbling_while_it_stands_and_its_brakes_squeal_on_the_way_in) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 130, 40, 3);
  bool braked = false, stood = false, hissed = false;
  for (int i = 0; i < 20000 && !(braked && stood && hissed); ++i) {
    world.step(0.05);
    const World::TrainSound ts = world.trainSound();
    for (auto& e : world.entities()) {
      if (e->dead || e->sprite->name() != "train") continue;
      if (e->stopPhase == 0 && e->driveSpeed > 0) CHECK(ts.motion > 0.99 && ts.brake == 0 && ts.hiss == 0);  // at full speed it only rolls
      if (e->stopPhase == 1 && ts.brake > 0.2) braked = true;
      if (e->stopPhase == 2) {
        stood = true;
        CHECK(ts.motion == 0 && ts.brake == 0);  // it does not roll any more
        if (ts.hiss > 0.5) hissed = true;
      }
    }
  }
  CHECK(braked && stood && hissed);
  // In the sound: the wheels are silent while the train stands, and the brakes are heard.
  AmbientParams rolling, standing, braking;
  rolling.kind = standing.kind = braking.kind = AmbientKind::City;
  rolling.train = standing.train = braking.train = 1;
  standing.trainMotion = 0;
  braking.trainMotion = 0.3f;
  braking.trainBrake = 1;
  AmbientParams calm;
  calm.kind = AmbientKind::City;
  CHECK(ambienceRms(rolling, 8) > ambienceRms(standing, 8) * 1.2);  // the traffic is the same, the train is not
  CHECK(ambienceRms(standing, 8) < ambienceRms(calm, 8) * 1.1);     // standing it adds nothing
  AmbientParams slowQuiet = braking;
  slowQuiet.trainBrake = 0;
  CHECK(ambienceRms(braking, 8) > ambienceRms(slowQuiet, 8) * 1.1);
  AmbientParams air = standing;
  air.trainHiss = 1;
  CHECK(ambienceRms(air, 8) > ambienceRms(standing, 8) * 1.1);
}

TEST(the_whale_has_a_tail_fin) {
  AssetPack pack = AssetPack::embedded();
  auto whale = pack.find("whale");
  CHECK(whale && whale->width() >= 44);  // the body alone is 35 cells wide
  const SpriteFrame& fr = whale->frame(false, 0);
  // The fluke is drawn at the tail end, in the right-hand cells of the upper rows.
  int cells = 0;
  for (int x = 38; x < whale->width(); ++x) {
    for (const auto& row : fr.art) cells += static_cast<size_t>(x) < row.size() && row[static_cast<size_t>(x)] != U' ';
  }
  CHECK(cells >= 8);
}

TEST(a_skyscraper_burns_with_smoke_and_a_fire_engine_then_is_renovated_quickly) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 130, 40, 3);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  const size_t buildings = world.buildingCount();
  CHECK(world.troubledBuildings() == 0 && world.fireLevel() == 0);
  world.forceFireOutcome(false);  // the fire engine wins
  CHECK(world.triggerRare(RarePick::Common));
  CHECK(!world.triggerRare(RarePick::Common));  // one at a time
  double fiercest = 0;
  bool smoke = false, engine = false, scaffold = false, flames = false;
  size_t burning = 0;
  for (int i = 0; i < 20 * 45; ++i) {  // 45 s
    world.step(0.05);
    fiercest = std::max(fiercest, world.fireLevel());
    smoke |= world.countSprite("smoke") > 0;
    engine |= world.countSprite("firetruck") > 0;
    burning = std::max(burning, world.troubledBuildings());
    if (i % 10 == 0 && world.fireLevel() > 0.9) {
      Frame f;
      world.render(f);
      int fire = 0;  // flame glyphs in yellow-orange
      for (int y = 0; y < world.seabedY(); ++y) {
        for (int x = 0; x < 130; ++x) fire += (f.at(x, y).ch != U' ' && f.at(x, y).fg.r == 255 && f.at(x, y).fg.g >= 150 && f.at(x, y).fg.b <= 110) ? 1 : 0;
      }
      flames |= fire > 2;
    }
    scaffold |= world.fireLevel() == 0 && world.troubledBuildings() == 1 && i > 20 * 8;
  }
  CHECK(fiercest == 1.0 && smoke && engine && flames);  // it rages, smokes, the engine comes
  CHECK(burning == 1);                                  // one building at a time
  CHECK(scaffold);                                      // then scaffolding goes up
  CHECK(world.troubledBuildings() == 0 && world.fireLevel() == 0);  // renovated within the minute
  CHECK(world.buildingCount() == buildings);
  CHECK(world.triggerRare(RarePick::Common));  // and it can happen again
}

TEST(the_session_menu_shows_the_version_and_the_copyright) {
  AssetPack pack = AssetPack::embedded();
  Session s = makeSession(pack);
  s.handle(Action::Menu);
  Frame f;
  s.render(f);
  const std::string text = f.toText();
  CHECK(text.find(std::string("diorascii ") + DIORASCII_VERSION) != std::string::npos);
  CHECK(text.find("Copyright (c) 2026 Christian Ahrens") != std::string::npos);
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

TEST(every_scene_has_a_second_catastrophe_that_comes_one_time_in_five) {
  AssetPack pack = AssetPack::embedded();
  struct Case { const char* scene; const char* first; const char* second; };
  for (const Case& c : {Case{"aquarium", "kraken", "submarine"}, Case{"forest", "thundercloud", "flood"}, Case{"desert", "sandworm", "helicopter"}, Case{"mountains", "avalanche", "yeti"}, Case{"city", "blaze", "lizard_titan"}, Case{"moon", "alien", "asteroid"}}) {
    auto scene = pack.scene(c.scene);
    CHECK(scene && scene->rare.size() == 2 && scene->rare[0].sprite == c.first && scene->rare[1].sprite == c.second);
    if (!scene || scene->rare.size() != 2) continue;
    CHECK(std::fabs(scene->rare[1].weight / (scene->rare[0].weight + scene->rare[1].weight) - 0.2) < 1e-9);
    // The pick can be told: the common one, or the second one.
    World common(pack, scene, 160, 36, 3), other(pack, scene, 160, 36, 3);
    CHECK(common.triggerRare(RarePick::Common) && countSprite(common, c.first) == 1 && countSprite(common, c.second) == 0);
    CHECK(other.triggerRare(RarePick::Other) && countSprite(other, c.second) == 1 && countSprite(other, c.first) == 0);
    // A weighted pick sends the second one about one time in five.
    int seconds = 0;
    const int runs = 150;
    for (int seed = 1; seed <= runs; ++seed) {
      World w(pack, scene, 60, 30, static_cast<uint64_t>(seed));
      w.triggerRare();
      seconds += countSprite(w, c.second) > 0 ? 1 : 0;
    }
    CHECK(seconds >= runs * 10 / 100 && seconds <= runs * 32 / 100);
  }
}

TEST(the_second_catastrophe_can_be_summoned_from_the_session_with_its_own_key_and_menu_entry) {
  AssetPack pack = AssetPack::embedded();
  Session::Config config;
  config.seed = 5;
  config.scene = 0;  // the aquarium comes first
  Session s = makeSession(pack, config);
  s.handle(Action::RareOther);
  s.step(0.05);
  CHECK(s.world().countSprite("submarine") == 1 && s.world().countSprite("kraken") == 0);
  Session menu = makeSession(pack, config);
  menu.handle(Action::Menu);
  Frame f;
  menu.render(f);
  CHECK(f.toText().find("Other rare event") != std::string::npos);
}

TEST(a_leaking_submarine_poisons_the_whole_sea_and_everything_alive_in_it_dies) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("aquarium"), 160, 36, 3);
  size_t life = 0;
  for (auto& e : world.entities()) life += e->life;
  CHECK(life > 15);
  world.triggerRare(RarePick::Other);
  double peak = 0;
  size_t alive = life, fewest = life;
  bool tinted = false, gone = false, trailBehind = false;
  for (int i = 0; i < 2400; ++i) {
    world.step(0.05);
    if (!trailBehind && countSprite(world, "submarine") == 1) {  // it drives on, the poison spreads behind it
      for (auto& e : world.entities()) {
        if (e->sprite->name() != "submarine") continue;
        const double front = e->movingRight() ? e->x + e->width() : e->x;
        const double back = e->movingRight() ? e->x - 18 : e->x + e->width() + 18;
        const double ahead = e->movingRight() ? front + 18 : front - 18;
        if (back > 2 && back < world.width() - 2 && ahead > 2 && ahead < world.width() - 2) {
          Frame f;
          world.render(f);
          const int y = world.surfaceY() + 4;
          const Cell& b = f.at(static_cast<int>(back), y);
          const Cell& a = f.at(static_cast<int>(ahead), y);
          trailBehind = b.bg.g > b.bg.b + 20 && !(a.bg.g > a.bg.b + 20);
        }
      }
    }
    peak = std::max(peak, world.toxinLevel());
    alive = 0;
    for (auto& e : world.entities()) alive += (e->life && !e->dead);
    fewest = std::min(fewest, alive);
    if (world.toxinLevel() > 0.99 && !tinted) {  // the sea has turned green where the cloud lies
      Frame f;
      world.render(f);
      const Cell& c = f.at(80, world.surfaceY() + 4);
      tinted = c.bg.g > c.bg.b;
    }
    gone |= i > 100 && countSprite(world, "submarine") == 0 && world.toxinLevel() == 0;
  }
  CHECK(peak > 0.99);
  CHECK(fewest == 0 && world.killedCount() >= life);  // all of it, and it was the cloud that did it
  CHECK(tinted && trailBehind);
  CHECK(gone && alive >= life / 2);  // the cloud and the wreck are gone, and the sea fills up again
}

TEST(a_flood_tears_everything_off_the_bank_and_leaves_mud_and_the_land_bare_for_a_while) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("forest"), 160, 36, 3);
  auto plants = [&] {
    size_t n = 0;
    for (auto& e : world.entities()) n += (e->grounded && !e->life && !e->dead && e->sprite->name() != "flood");
    return n;
  };
  const size_t before = plants();
  CHECK(before > 30);
  world.triggerRare(RarePick::Other);
  double peak = 0, muddy = 0;
  size_t fewest = before;
  bool rained = false, water = false;
  for (int i = 0; i < 700; ++i) {  // 35 seconds: the water has come and gone
    world.step(0.05);
    peak = std::max(peak, world.floodLevel());
    fewest = std::min(fewest, plants());
    rained |= world.raining();
    if (i == 200) {  // the water stands over the land
      Frame f;
      world.render(f);
      const Cell& c = f.at(80, world.height() - 2);
      water = c.bg.r > c.bg.b;  // brown, not the blue of the stream
    }
  }
  for (int x = 0; x < world.width(); ++x) muddy = std::max(muddy, world.mudAt(x));
  CHECK(peak > 0.9 && rained && water);
  CHECK(fewest <= before / 10);          // the trees, bushes and flowers are torn off
  CHECK(muddy > 0.5);                    // the mud is still there
  CHECK(plants() < before / 2);          // and not much has grown back yet
  for (int i = 0; i < 2400; ++i) world.step(0.05);  // two more minutes
  CHECK(plants() >= before * 9 / 10);    // it is replanted in the end
  double mud = 0;
  for (int x = 0; x < world.width(); ++x) mud = std::max(mud, world.mudAt(x));
  CHECK(mud == 0);
}

TEST(poachers_in_a_helicopter_shoot_down_the_life_of_the_desert_one_by_one_and_leave) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("desert"), 160, 36, 3);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  size_t life = 0;
  for (auto& e : world.entities()) life += e->life;
  CHECK(life > 8);
  world.triggerRare(RarePick::Other);
  size_t alive = life, fewest = life, between = 0;
  double rotor = 0;
  bool left = false;
  for (int i = 0; i < 2000; ++i) {
    world.step(0.05);
    alive = 0;
    for (auto& e : world.entities()) alive += (e->life && !e->dead);
    fewest = std::min(fewest, alive);
    between += (alive > 0 && alive < life) ? 1 : 0;
    rotor = std::max(rotor, world.rotorLevel());
    left |= i > 100 && countSprite(world, "helicopter") == 0;
  }
  CHECK(fewest <= life / 8);                      // it hunts down (nearly) all of it
  CHECK(between > 100);                           // one after the other, not all at once
  CHECK(world.shotCount() >= life * 8 / 10);      // each one with a shot
  CHECK(world.crushCount() == 0);                 // which are shots, not bites
  CHECK(rotor > 0.9 && left);                     // the rotor is heard, and the poachers go
  CHECK(alive >= life / 2);                       // and the animals come back later
}

TEST(a_giant_yeti_comes_over_the_highest_ridge_and_mauls_the_animals_on_the_ground) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("mountains"), 140, 36, 3);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  size_t ground = 0;
  for (auto& e : world.entities()) ground += (e->life && e->grounded);
  CHECK(ground > 5);
  world.triggerRare(RarePick::Other);
  world.step(0.05);
  int peak = 0;
  for (int x = 0; x < world.width(); ++x) {
    if (world.ridgeRow(x) < world.ridgeRow(peak)) peak = x;
  }
  bool atPeak = false;
  size_t left = ground, tall = 0;
  double roar = 0;
  for (auto& e : world.entities()) {
    if (e->sprite->name() == "yeti") {
      atPeak = std::fabs(e->x + e->width() / 2.0 - peak) < 3;  // it starts on the ridge
      tall = static_cast<size_t>(e->height());
    }
  }
  CHECK(atPeak && tall >= 12);                  // and it is much bigger than the bear
  bool hiddenAtFirst = false, shownLater = false;
  double highestFoot = 1e9, lowestFoot = 0;
  for (auto& e : world.entities()) hiddenAtFirst |= e->sprite->name() == "yeti" && e->behindRidge;
  for (int i = 0; i < 2000; ++i) {
    world.step(0.05);
    for (auto& e : world.entities()) {
      if (e->sprite->name() != "yeti") continue;
      if (!e->behindRidge) {
        shownLater = true;
        highestFoot = std::min(highestFoot, e->y + e->height());
        lowestFoot = std::max(lowestFoot, e->y + e->height());
      }
    }
    size_t now = 0;
    for (auto& e : world.entities()) now += (e->life && e->grounded && !e->dead);
    left = std::min(left, now);
    roar = std::max(roar, world.yetiLevel());
  }
  const double descended = lowestFoot - highestFoot;
  CHECK(left == 0 && world.killedCount() >= ground);
  CHECK(hiddenAtFirst && shownLater && descended >= 8);  // up over the ridge from behind, then down into the valley
  CHECK(roar > 0.8 && countSprite(world, "yeti") == 0);  // it has gone again
}

TEST(a_giant_lizard_and_a_winged_monster_fight_in_the_city_and_leave_it_in_ruins_that_are_rebuilt) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 150, 38, 3);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  CHECK(world.triggerRare(RarePick::Other));
  size_t most = 0, ruins = 0;
  bool both = false, beam = false, blue = false, violet = false, outwardRebuild = true, fledEverything = false,
       backAfterwards = false, quietAfterFall = true;
  double loudestFlames = 0;
  bool inFront = true;
  std::vector<double> spans;
  double firstSpan = 0, widestSpan = 0, middle = 0;
  double level = 0;
  int bothUntil = 0, over = -1;
  for (int i = 0; i < 4000; ++i) {
    world.step(0.05);
    most = std::max(most, world.troubledBuildings());
    ruins = std::max(ruins, world.ruinedBuildings());
    level = std::max(level, world.titanLevel());
    {
      const std::vector<int> cols = world.troubledColumns();
      if (!cols.empty()) {
        const double span = *std::max_element(cols.begin(), cols.end()) - *std::min_element(cols.begin(), cols.end());
        if (spans.empty()) firstSpan = span;
        spans.push_back(span);
        widestSpan = std::max(widestSpan, span);
        if (cols.size() >= most) {  // the middle of the ruins
          double sum = 0;
          for (int c : cols) sum += c;
          middle = sum / static_cast<double>(cols.size());
        }
        // While it is rebuilt, the middle must be done before the outskirts.
        if (countSprite(world, "winged_titan") == 0 && cols.size() + 8 < most && cols.size() > most / 4) {
          for (int c : cols) {
            if (std::fabs(c - middle) < 6) outwardRebuild = false;
          }
        }
      }
    }
    {
      size_t street = 0;  // people and vehicles on the main street
      for (const char* n : {"walker_front", "walker_back", "kid", "jogger", "cyclist", "car_sedan", "car_hatch", "taxi", "bus", "truck"}) {
        street += countSprite(world, n);
      }
      if (i > 700 && countSprite(world, "winged_titan") > 0 && street == 0) fledEverything = true;  // mid-fight: none left
      if (i > 3500 && street > 3) backAfterwards = true;
    }
    for (auto& e : world.entities()) {  // the monsters stand in front of the trees, shops and benches of the street
      const std::string n = e->sprite->name();
      if (i < 10 || (n != "lizard_titan" && n != "winged_titan")) continue;
      for (auto& o : world.entities()) {
        const std::string m = o->sprite->name();
        if (m == "oak" || m == "bush" || m == "pine_big" || m == "bench" || m == "kiosk") inFront &= e->z < o->z;
      }
    }
    loudestFlames = std::max(loudestFlames, world.flameLevel());
    if (world.flameLevel() > 0.5) {  // the streams of fire are an emitter at the fight
      const Emitter fl = world.emitter(EmitterKey::Flames);
      CHECK(fl.level == static_cast<float>(world.flameLevel()) && std::fabs(fl.x) <= kStageReach && fl.depth > 0.0f);
    }
    if (countSprite(world, "winged_titan") == 0 && i > 300 && ambientFor(world).winged != 0.0f) quietAfterFall = false;
    const bool pair = countSprite(world, "lizard_titan") > 0 && countSprite(world, "winged_titan") > 0;
    both |= pair;
    bothUntil = pair ? i : bothUntil;
    if (over < 0 && countSprite(world, "lizard_titan") == 0) over = i;
    beam |= world.tongueCount() > 0;
    if (world.tongueCount() > 0 && i % 3 == 0) {  // flames of two colors: the lizard's blue and the other's violet
      Frame f;
      world.render(f);
      for (int y = 0; y < f.height(); ++y) {
        for (int x = 0; x < f.width(); ++x) {
          const Cell& c = f.at(x, y);
          if (c.ch != U'*' && c.ch != U'#' && c.ch != U'%' && c.ch != U'&' && c.ch != U'@') continue;
          blue |= c.fg.b > 200 && c.fg.r < 140 && c.fg.g < 190 && c.fg.b > c.fg.g + 40;
          violet |= c.fg.r > 140 && c.fg.b > 200 && c.fg.g < 130;
        }
      }
    }
  }
  CHECK(both && beam && blue && violet);
  CHECK(fledEverything && backAfterwards && inFront);
  CHECK(loudestFlames > 0.9 && ambientFor(world).flames == 0.0f && ambientFor(world).winged == 0.0f);
  CHECK(quietAfterFall);  // once the winged monster has fallen, its sound is gone  // the traffic and the people ran off, and came back later
  // The devastation spreads out from where they fight, as soon as they fight, and the rebuilding follows from there.
  CHECK(spans.size() > 10 && firstSpan < widestSpan / 2 && widestSpan > 60);
  CHECK(outwardRebuild);
  CHECK(bothUntil > 400);                     // they fight for a good while (over 20 seconds)
  CHECK(ruins >= 12 && most >= 12);           // the city is in ruins
  CHECK(level > 0.8);
  CHECK(over > bothUntil);                    // the winner walks on afterwards
  CHECK(world.troubledBuildings() == 0);      // and everything has been rebuilt by the end
  CHECK(world.triggerRare(RarePick::Common)); // the slot is free again
}

TEST(a_big_meteor_approaches_from_afar_growing_and_strikes_across_the_whole_width_of_the_moon) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("moon"), 140, 30, 3);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  size_t grounded = 0;
  for (auto& e : world.entities()) grounded += (e->grounded && !e->dead);
  CHECK(grounded > 3);
  const unsigned impacts = world.impactCount();
  world.triggerRare(RarePick::Other);
  world.step(0.05);
  size_t firstFrame = 99, lastFrame = 0;
  double highest = 0, rumble = 0;
  bool struck = false;
  for (int i = 0; i < 400 && !struck; ++i) {
    for (auto& e : world.entities()) {
      if (e->sprite->name() != "asteroid") continue;
      if (i == 0) firstFrame = e->frame;
      lastFrame = e->frame;
      highest = std::max(highest, e->y + e->height() / 2.0);
    }
    rumble = std::max(rumble, world.asteroidLevel());
    world.step(0.05);
    struck = world.impactCount() > impacts;
  }
  CHECK(struck && firstFrame == 0 && lastFrame >= 6);  // a speck first, a giant boulder at the end
  CHECK(rumble > 0.9 && world.asteroidLevel() == 0);
  CHECK(world.flashing());                              // the whole screen flashes
  for (auto& e : world.entities()) {  // everything that stood on the ground is wrecked (the flying debris is not)
    const std::string n = e->sprite->name();
    CHECK(!(n == "rover" || n == "flag" || n == "rock" || n == "lander") || e->dead);
  }
  CHECK(world.deepestCrater() >= 1.0 && world.craterRows(0) >= 1 && world.craterRows(world.width() - 1) >= 1);  // a crater over the entire width
  size_t dust = 0;
  double leftmost = 1e9, rightmost = -1e9;
  for (auto& e : world.entities()) {
    if (e->sprite->name() != "dust" || e->dead) continue;
    ++dust;
    leftmost = std::min(leftmost, e->x);
    rightmost = std::max(rightmost, e->x);
  }
  CHECK(dust > 30 && leftmost < 10 && rightmost > world.width() - 10);  // the impact is felt over the entire width
  for (int i = 0; i < 80; ++i) world.step(0.05);
  CHECK(world.triggerRare(RarePick::Common));
}

TEST(the_second_catastrophes_have_their_own_sound_the_hiss_the_rush_the_rotor_and_the_roars) {
  constexpr int kRate = 48000;  // the hiss lives above what a low sample rate can carry
  auto rms = [&](const AmbientParams& p) {
    Ambience a(kRate, 1);
    std::vector<float> buf(static_cast<size_t>(kRate) * 6 * 2);
    a.render(p, buf.data(), static_cast<int>(buf.size() / 2));
    double sum = 0;
    for (size_t i = buf.size() / 2; i < buf.size(); ++i) sum += double(buf[i]) * buf[i];
    return std::sqrt(sum / double(buf.size() / 2));
  };
  for (AmbientKind kind : {AmbientKind::Aquarium, AmbientKind::Forest, AmbientKind::Desert, AmbientKind::Mountains, AmbientKind::City, AmbientKind::Moon}) {
    AmbientParams calm;
    calm.kind = kind;
    AmbientParams menace = calm;
    menace.menace = 1;
    CHECK(rms(menace) > rms(calm) * 1.5);
    CHECK(rms(menace) < 0.3);  // loud, but well below a storm
  }
  // The monsters' streams of fire roar, and the winged one's screech is only heard while it lives.
  {
    AmbientParams fight;
    fight.kind = AmbientKind::City;
    fight.menace = 1;
    fight.winged = 1;
    AmbientParams fire = fight;
    fire.flames = 1;
    CHECK(rms(fire) > rms(fight) * 1.4);
    // The screech is high: with the winged one gone the sound has much less of what lies above a few hundred Hz.
    auto brightness = [&](const AmbientParams& p) {
      Ambience a(kRate, 1);
      std::vector<float> buf(static_cast<size_t>(kRate) * 30 * 2);
      a.render(p, buf.data(), static_cast<int>(buf.size() / 2));
      double s = 0, d = 0;
      for (size_t i = 2; i < buf.size(); i += 2) {
        s += double(buf[i]) * buf[i];
        d += (double(buf[i]) - buf[i - 2]) * (double(buf[i]) - buf[i - 2]);
      }
      return std::sqrt(d / s);
    };
    AmbientParams alone = fight;
    alone.winged = 0;
    CHECK(brightness(fight) > brightness(alone) * 1.1);
  }
  // A rifle shot cracks, in the desert only.
  auto peakAfter = [&](AmbientKind kind, bool shoot) {
    Ambience a(kRate, 4);
    AmbientParams p;
    p.kind = kind;
    std::vector<float> buf(static_cast<size_t>(kRate) * 2);
    for (int s = 0; s < 3; ++s) a.render(p, buf.data(), kRate);
    if (shoot) p.shots += 2;
    a.render(p, buf.data(), kRate);
    float pk = 0;
    for (size_t i = 0; i < static_cast<size_t>(kRate * 0.3) * 2; ++i) pk = std::max(pk, std::fabs(buf[i]));
    return pk;
  };
  CHECK(peakAfter(AmbientKind::Desert, true) > peakAfter(AmbientKind::Desert, false) * 1.5f);
  CHECK(peakAfter(AmbientKind::Forest, true) < peakAfter(AmbientKind::Forest, false) * 1.2f);
  // The world feeds it: the strongest of the catastrophes' levels, and the shot count.
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("desert"), 160, 36, 3);
  CHECK(ambientFor(world).menace == 0.0f);
  world.triggerRare(RarePick::Other);
  for (int i = 0; i < 100; ++i) world.step(0.05);
  CHECK(ambientFor(world).menace > 0.3f && ambientFor(world).shots == world.shotCount());
}

TEST(the_second_catastrophes_survive_tiny_and_odd_screens_at_every_hour) {
  AssetPack pack = AssetPack::embedded();
  for (const char* name : {"aquarium", "forest", "desert", "mountains", "city", "moon"}) {
    for (auto [w, h] : {std::pair{1, 1}, std::pair{7, 5}, std::pair{40, 12}, std::pair{83, 25}, std::pair{121, 31}, std::pair{250, 60}}) {
      for (double hour : {3.0, 12.0, 19.0}) {
        World world(pack, pack.scene(name), w, h, 7);
        world.setHour(hour);
        world.triggerRare(RarePick::Other);
        Frame f;
        for (int i = 0; i < 700; ++i) {
          world.step(0.1);
          if (i % 70 == 0) world.render(f);
        }
        CHECK(f.width() == w && f.height() == h);
      }
    }
  }
}

TEST(the_second_catastrophes_are_deterministic_per_seed) {
  AssetPack pack = AssetPack::embedded();
  for (const char* name : {"aquarium", "forest", "desert", "mountains", "city", "moon"}) {
    std::string a, b;
    for (std::string* out : {&a, &b}) {
      World world(pack, pack.scene(name), 120, 32, 11);
      world.triggerRare(RarePick::Other);
      for (int i = 0; i < 500; ++i) world.step(0.1);
      Frame f;
      world.render(f);
      *out = f.toText();
    }
    CHECK(a == b);
  }
}

TEST(console_defaults_to_the_terminal_style) {
  CHECK(defaultStyleFor(ColorMode::Console) == Style::Terminal);  // the text console cannot show the backdrop
  CHECK(defaultStyleFor(ColorMode::TrueColor) == Style::Full);
  CHECK(defaultStyleFor(ColorMode::Ansi256) == Style::Full);
  CHECK(defaultStyleFor(ColorMode::Ansi16) == Style::Full);
}

// ---------------------------------------------------- layouts and spatializer ---

TEST(layouts_have_the_expected_channels) {
  const int counts[kLayoutCount] = {2, 6, 8, 8, 10, 10, 12, 14, 16};
  const bool heights[kLayoutCount] = {false, false, false, true, true, true, true, true, true};
  for (int i = 0; i < kLayoutCount; ++i) {
    const Layout l = Layout::named(static_cast<LayoutId>(i));
    CHECK(l.channels() == counts[i]);
    CHECK(l.channels() <= kMaxChannels);
    CHECK(l.hasHeight() == heights[i]);
    CHECK((l.lfeIndex() >= 0) == (i != 0));  // every layout but stereo has an LFE channel
    if (l.lfeIndex() >= 0) CHECK(l.speaker(l.lfeIndex()).isLfe());
    for (int c = 1; c < l.channels(); ++c)  // canonical order, no repeats
      CHECK(static_cast<int>(l.speaker(c).channel) > static_cast<int>(l.speaker(c - 1).channel));
    CHECK(l.isStereo() == (i == 0));
  }
  CHECK(Layout().channels() == 2 && Layout().isStereo());  // the default is stereo
}

TEST(layout_names_round_trip_and_bad_names_are_rejected) {
  for (int i = 0; i < kLayoutCount; ++i) {
    LayoutId id = LayoutId::Stereo;
    CHECK(Layout::parse(Layout::nameOf(static_cast<LayoutId>(i)), id));
    CHECK(static_cast<int>(id) == i);
    CHECK(std::string(Layout::named(static_cast<LayoutId>(i)).name()) == Layout::nameOf(static_cast<LayoutId>(i)));
  }
  LayoutId id = LayoutId::S7_1;
  for (const char* bad : {"", "5.0", "7.1.5", "stereo", "9.1.6 "}) CHECK(!Layout::parse(bad, id));
  CHECK(id == LayoutId::S7_1);  // a failed parse leaves it alone
}

TEST(layout_speaker_angles_follow_the_tables) {
  // The 5.1 family puts the sides at 110 degrees, 7.1 and up at 90.
  for (LayoutId id : {LayoutId::S5_1, LayoutId::S5_1_2, LayoutId::S5_1_4}) {
    const Layout l = Layout::named(id);
    for (int i = 0; i < l.channels(); ++i)
      if (l.speaker(i).channel == Channel::Ls) CHECK(l.speaker(i).azimuth == -110.0f);
  }
  const Layout l714 = Layout::named(LayoutId::S7_1_4);
  for (int i = 0; i < l714.channels(); ++i) {
    const Speaker& s = l714.speaker(i);
    if (s.channel == Channel::Rs) CHECK(s.azimuth == 90.0f);
    if (s.channel == Channel::Rtr) CHECK(s.azimuth == 135.0f && s.elevation == 45.0f);
    if (s.channel == Channel::C) CHECK(s.azimuth == 0.0f && s.elevation == 0.0f);
  }
  // Wide speakers exist only in 9.1.x, top-middle ones only in x.1.2 and 9.1.6.
  auto has = [](LayoutId id, Channel c) {
    const Layout l = Layout::named(id);
    for (int i = 0; i < l.channels(); ++i)
      if (l.speaker(i).channel == c) return true;
    return false;
  };
  CHECK(has(LayoutId::S9_1_4, Channel::Lw) && !has(LayoutId::S7_1_4, Channel::Lw));
  CHECK(has(LayoutId::S9_1_6, Channel::Ltm) && has(LayoutId::S5_1_2, Channel::Rtm) && !has(LayoutId::S9_1_4, Channel::Ltm));
}

TEST(nearest_channel_maps_directions_to_canonical_channels) {
  CHECK(nearestChannel(-30, 0) == Channel::L);
  CHECK(nearestChannel(0, 0) == Channel::C);
  CHECK(nearestChannel(110, 0) == Channel::Rs);
  CHECK(nearestChannel(-100, 50) == Channel::Ltm);
  CHECK(nearestChannel(-120, 40) == Channel::Ltr);
  CHECK(nearestChannel(60, 5) == Channel::Rw);
  CHECK(nearestChannel(NAN, INFINITY) == Channel::C);  // junk in, a defined channel out
  for (int i = 0; i < kMaxChannels; ++i) {             // every canonical position maps to itself
    const Channel c = static_cast<Channel>(i);
    if (c == Channel::LFE) continue;
    float az, el;
    canonicalPosition(c, az, el);
    CHECK(nearestChannel(az, el) == c);
  }
}

TEST(device_channels_map_to_a_layout) {
  DeviceChannel dev[8] = {{-30, 0, false, true}, {30, 0, false, true}, {0, 0, false, true}, {0, 0, true, true},
                          {0, 0, false, false}, {0, 0, true, true},    {-100, 0, false, true}, {100, 0, false, true}};
  const Layout l = Layout::fromDevice(dev, 8);
  CHECK(l.channels() == 8);
  CHECK(std::string(l.name()) == "custom");
  CHECK(l.speaker(0).channel == Channel::L && l.speaker(1).channel == Channel::R && l.speaker(2).channel == Channel::C);
  CHECK(l.lfeIndex() == 3);
  CHECK(l.speaker(4).channel == Channel::None);  // no known position: silent
  CHECK(l.speaker(5).channel == Channel::None);  // a second LFE is silent too
  CHECK(l.speaker(6).channel == Channel::Ls && l.speaker(6).azimuth == -100.0f);  // the device's angle is kept
  float g[kMaxChannels];
  Spatializer::gains(l, {-90, 0, 0, 0}, g);
  CHECK(g[4] == 0 && g[5] == 0 && g[3] == 0);  // silent and LFE channels get no gain
  CHECK(g[6] > g[7]);
  // More channels than the maximum are cut off; a nonsense count gives an empty layout.
  std::vector<DeviceChannel> many(40);
  CHECK(Layout::fromDevice(many.data(), 40).channels() == kMaxChannels);
  CHECK(Layout::fromDevice(many.data(), -3).channels() == 0);
}

TEST(panning_gains_are_constant_power_in_every_layout) {
  for (int i = 0; i < kLayoutCount; ++i) {
    const Layout l = Layout::named(static_cast<LayoutId>(i));
    for (float az = -180; az <= 180; az += 15)
      for (float el = -10; el <= 90; el += 20)
        for (float spread : {0.0f, 0.3f, 1.0f}) {
          float g[kMaxChannels];
          Spatializer::gains(l, {az, el, spread, 0}, g);
          float power = 0;
          for (int c = 0; c < kMaxChannels; ++c) {
            CHECK(std::isfinite(g[c]) && g[c] >= 0.0f);
            power += g[c] * g[c];
          }
          CHECK(std::fabs(power - 1.0f) < 1e-5f);
          if (l.lfeIndex() >= 0) CHECK(g[l.lfeIndex()] == 0.0f);  // the panner never feeds the LFE
          for (int c = l.channels(); c < kMaxChannels; ++c) CHECK(g[c] == 0.0f);
        }
  }
}

TEST(a_source_on_a_speaker_is_loudest_there) {
  for (int i = 0; i < kLayoutCount; ++i) {
    const Layout l = Layout::named(static_cast<LayoutId>(i));
    for (int s = 0; s < l.channels(); ++s) {
      if (!l.speaker(s).isDirectional()) continue;
      float g[kMaxChannels];
      Spatializer::gains(l, {l.speaker(s).azimuth, l.speaker(s).elevation, 0, 0}, g);
      int best = 0;
      for (int c = 1; c < l.channels(); ++c)
        if (g[c] > g[best]) best = c;
      CHECK(best == s);
    }
  }
}

TEST(stereo_panning_keeps_the_synths_constant_power_law) {
  const Layout st = Layout::named(LayoutId::Stereo);
  float g[kMaxChannels];
  Spatializer::gains(st, {0, 0, 0, 0}, g);
  CHECK(std::fabs(g[0] - 0.70710678f) < 1e-5f && std::fabs(g[1] - 0.70710678f) < 1e-5f);
  Spatializer::gains(st, {-90, 0, 0, 0}, g);
  CHECK(g[0] > 0.9999f && g[1] < 1e-5f);
  Spatializer::gains(st, {90, 0, 0, 0}, g);
  CHECK(g[1] > 0.9999f && g[0] < 1e-5f);
  // The law is cos/sin(pan * pi/2): a pan of 0.8, as the old random pans used, is azimuth asin(0.6).
  const float az = std::asin(0.6f) * 180.0f / 3.14159265f;
  Spatializer::gains(st, {az, 0, 0, 0}, g);
  CHECK(std::fabs(g[0] - std::cos(0.8f * 1.5707963f)) < 1e-4f && std::fabs(g[1] - std::sin(0.8f * 1.5707963f)) < 1e-4f);
  Spatializer::gains(st, {45, 60, 0, 0}, g);  // elevation does not matter in stereo
  float h[kMaxChannels];
  Spatializer::gains(st, {45, 0, 0, 0}, h);
  CHECK(g[0] == h[0] && g[1] == h[1]);
}

TEST(a_far_left_source_stays_on_the_left_and_a_high_one_goes_up) {
  const Layout l = Layout::named(LayoutId::S7_1_4);
  float g[kMaxChannels];
  Spatializer::gains(l, {-90, 0, 0, 0}, g);
  float left = 0, right = 0;
  for (int c = 0; c < l.channels(); ++c) {
    const Speaker& s = l.speaker(c);
    if (!s.isDirectional()) continue;
    (s.azimuth < 0 ? left : right) += g[c] * g[c];
  }
  CHECK(left > 0.97f && right < 0.03f);  // centre speakers (azimuth 0) count as right here, so this is strict

  Spatializer::gains(l, {0, 80, 0, 0}, g);
  float up = 0;
  for (int c = 0; c < l.channels(); ++c)
    if (l.speaker(c).elevation > 1) up += g[c] * g[c];
  CHECK(up > 0.6f);
  // Without height speakers the same source is folded onto the base layer, still at constant power.
  const Layout flat = Layout::named(LayoutId::S7_1);
  Spatializer::gains(flat, {0, 80, 0, 0}, g);
  float power = 0;
  for (int c = 0; c < flat.channels(); ++c) power += g[c] * g[c];
  CHECK(std::fabs(power - 1.0f) < 1e-5f);
}

TEST(spread_widens_a_source_until_it_fills_the_room) {
  const Layout l = Layout::named(LayoutId::S7_1_4);
  float point[kMaxChannels], wide[kMaxChannels], all[kMaxChannels];
  Spatializer::gains(l, {-90, 0, 0, 0}, point);
  Spatializer::gains(l, {-90, 0, 0.5f, 0}, wide);
  Spatializer::gains(l, {-90, 0, 1.0f, 0}, all);
  auto peak = [&](const float* g) { return *std::max_element(g, g + l.channels()); };
  CHECK(peak(wide) < peak(point) && peak(all) < peak(wide));
  for (int c = 0; c < l.channels(); ++c)  // all equal on the 11 directional speakers at spread 1
    if (c != l.lfeIndex()) CHECK(std::fabs(all[c] - 1.0f / std::sqrt(11.0f)) < 1e-5f);
}

TEST(the_panner_survives_junk_input) {
  const float junk[] = {NAN, INFINITY, -INFINITY, 1e30f, -1e30f};
  for (int i = 0; i < kLayoutCount; ++i) {
    const Layout l = Layout::named(static_cast<LayoutId>(i));
    for (float a : junk)
      for (float b : junk) {
        float g[kMaxChannels];
        Spatializer::gains(l, {a, b, a, b}, g);
        float power = 0;
        for (int c = 0; c < kMaxChannels; ++c) {
          CHECK(std::isfinite(g[c]));
          power += g[c] * g[c];
        }
        CHECK(std::fabs(power - 1.0f) < 1e-4f);
      }
  }
  // A layout with nothing to pan on is silent, not NaN.
  DeviceChannel only[1] = {{0, 0, true, true}};
  float g[kMaxChannels];
  Spatializer::gains(Layout::fromDevice(only, 1), {0, 0, 0, 0}, g);
  CHECK(g[0] == 0.0f);
  DeviceChannel mono[1] = {{0, 0, false, true}};
  Spatializer::gains(Layout::fromDevice(mono, 1), {50, 0, 0, 0}, g);
  CHECK(g[0] == 1.0f);  // one speaker takes everything
}

TEST(a_moving_voice_glides_and_never_jumps) {
  const Layout l = Layout::named(LayoutId::S7_1_4);
  SpatialVoice v(48000);
  v.setTarget(l, {-90, 0, 0, 0}, true);
  float before[kMaxChannels];
  for (int c = 0; c < l.channels(); ++c) before[c] = v.gain(c);
  v.setTarget(l, {90, 0, 0, 0});  // the source flips to the other side
  float frame[kMaxChannels] = {};
  float maxStep = 0;
  for (int n = 0; n < 4800; ++n) {  // 100 ms
    v.process(0.0f, frame, l.channels());
    for (int c = 0; c < l.channels(); ++c) {
      maxStep = std::max(maxStep, std::fabs(v.gain(c) - before[c]));
      before[c] = v.gain(c);
    }
  }
  // One step is k times the remaining distance (at most 1), with k = 1 - exp(-1 / (20 ms * 48 kHz)).
  CHECK(maxStep <= (1.0f - std::exp(-1.0f / (0.02f * 48000.0f))) * 1.0001f);
  CHECK(maxStep > 0.0005f);  // and it does move, so the bound above is not met by standing still
  float target[kMaxChannels];
  Spatializer::gains(l, {90, 0, 0, 0}, target);
  for (int c = 0; c < l.channels(); ++c) CHECK(std::fabs(v.gain(c) - target[c]) < 0.02f);  // arrived after 100 ms
}

TEST(a_new_voice_can_start_in_place_and_adds_to_the_frame) {
  const Layout l = Layout::named(LayoutId::S5_1);
  SpatialVoice v(48000);
  v.setTarget(l, {-30, 0, 0, 0}, true);
  float g[kMaxChannels];
  Spatializer::gains(l, {-30, 0, 0, 0}, g);
  float frame[kMaxChannels] = {};
  frame[0] = 0.25f;  // something is already in the frame: the voice adds, it does not overwrite
  v.process(1.0f, frame, l.channels());
  CHECK(std::fabs(frame[0] - (0.25f + g[0])) < 1e-6f);
  // Starting without snap rises from silence.
  SpatialVoice w(48000);
  w.setTarget(l, {-30, 0, 0, 0});
  float f2[kMaxChannels] = {};
  w.process(1.0f, f2, l.channels());
  CHECK(f2[0] > 0 && f2[0] < 0.05f * g[0]);
}

TEST(the_bass_send_feeds_the_lfe_channel_with_low_frequencies_only) {
  const Layout l = Layout::named(LayoutId::S5_1);
  auto lfeEnergy = [&](float hz, float send) {
    SpatialVoice v(48000);
    v.setTarget(l, {0, 0, 0, send}, true);
    double e = 0;
    for (int n = 0; n < 48000; ++n) {
      float frame[kMaxChannels] = {};
      v.process(static_cast<float>(std::sin(6.2831853 * hz * n / 48000.0)), frame, l.channels());
      if (n > 4800) e += frame[l.lfeIndex()] * frame[l.lfeIndex()];
    }
    return e;
  };
  const double low = lfeEnergy(50, 1), high = lfeEnergy(2000, 1);
  CHECK(low > 1000 * high);                  // a 2 kHz tone is far below the bass send
  CHECK(lfeEnergy(50, 0) == 0.0);            // no send, no bass
  CHECK(lfeEnergy(50, 0.5f) < 0.3 * low);    // half the send is a quarter of the energy
}

TEST(without_an_lfe_channel_the_bass_is_folded_into_the_front) {
  const Layout st = Layout::named(LayoutId::Stereo);
  SpatialVoice v(48000);
  v.setTarget(st, {-90, 0, 0, 1}, true);  // hard left in the panner, but the bass folds to both
  CHECK(std::fabs(v.bassGain(0) - 0.70710678f) < 1e-5f && std::fabs(v.bassGain(1) - 0.70710678f) < 1e-5f);
  const Layout l = Layout::named(LayoutId::S7_1_4);
  v.setTarget(l, {0, 0, 0, 1}, true);
  CHECK(v.bassGain(l.lfeIndex()) == 1.0f);  // with an LFE channel it goes there alone
  float others = 0;
  for (int c = 0; c < l.channels(); ++c)
    if (c != l.lfeIndex()) others += v.bassGain(c);
  CHECK(others == 0.0f);
  DeviceChannel side[2] = {{-100, 0, false, true}, {100, 0, false, true}};  // nothing in front: all share it
  v.setTarget(Layout::fromDevice(side, 2), {0, 0, 0, 1}, true);
  CHECK(std::fabs(v.bassGain(0) - 0.70710678f) < 1e-5f);
}


// ------------------------------------------------------ ambience golden values ---
// The stereo output of the synth is pinned per scene, so that refactors (multichannel, new threads)
// can prove they moved no sound. Each case renders 2 s at 48 kHz in blocks of 0.1 s with every
// parameter and counter active (the counters step up once, after 0.4 s) and records the RMS of each
// channel in ten windows of 0.2 s. The table is compared with a tolerance, not bit for bit: libm
// differs between platforms in the last bits, and the golden values must hold on all of them.
// Regenerate with `diorascii_tests --print-golden` only for a change that is meant to alter sound,
// and say so in the commit message.

namespace {
/// Number of golden cases: every scene, calm and with its second catastrophe.
constexpr int kGoldenCases = 12;
/// Number of windows per channel in a golden case.
constexpr int kGoldenWindows = 10;
/// Frames per window of a golden case.
constexpr int kGoldenWindowFrames = 9600;
/// Frames per render call of a golden case.
constexpr int kGoldenBlock = 4800;
/// Relative tolerance of a golden RMS value. Linux/GCC matches to 0.2 %, but the CI runs on macOS and Windows
/// differ by up to 4.5 % in single windows (libm and fused multiply-add change the noise filters and the
/// onset of events in the last bits), so the bound is 8 %: still far below what a lost or misplaced sound does.
constexpr float kGoldenTolerance = 8e-2f;

/// The parameters of golden case `n`: scene 1 + n / 2, with the catastrophe if n is odd, everything active.
AmbientParams goldenParams(int n) {
  AmbientParams p;
  p.kind = static_cast<AmbientKind>(1 + n / 2);
  p.night = 0.5f; p.rain = 0.6f; p.storm = 0.8f; p.whale = 0.6f; p.tornado = 0.6f; p.avalanche = 0.6f;
  p.worm = 0.6f; p.siren = 0.6f; p.wail = 0.6f; p.train = 0.6f; p.plane = 0.6f; p.fire = 0.6f;
  p.flames = 0.5f; p.winged = 0.5f;
  p.menace = (n % 2) ? 0.8f : 0.0f;
  return p;
}

/// Renders `seconds` of golden case `n` with the synth `a`, in blocks of 0.1 s (the counters step up
/// once, after 0.4 s, as in every golden case), appending interleaved samples of `channels` per frame.
void goldenAppend(Ambience& a, AmbientParams& p, int channels, int firstBlock, int blocks, std::vector<float>& out, bool stepCounters = true) {
  std::vector<float> buf(static_cast<size_t>(channels) * kGoldenBlock);
  for (int block = firstBlock; block < firstBlock + blocks; ++block) {
    if (stepCounters && block == 4) { ++p.flashes; ++p.nibbles; ++p.impacts; ++p.crushes; ++p.squeals; ++p.shots; }
    a.render(p, buf.data(), kGoldenBlock);
    out.insert(out.end(), buf.begin(), buf.end());
  }
}

/// The forest's wind and rain by day with nothing placed (no storm, no strike, no crickets): what a stereo pair and the
/// front speakers of a room must play alike, bit for bit.
AmbientParams placedFreeParams() {
  AmbientParams p = goldenParams(0);
  p.storm = 0;
  p.night = 0;
  return p;
}

/// Renders `placedFreeParams` as interleaved stereo for two seconds.
std::vector<float> placedFreeRender() {
  Ambience a(48000, 5);
  AmbientParams p = placedFreeParams();
  std::vector<float> out;
  goldenAppend(a, p, 2, 0, 2 * 48000 / kGoldenBlock, out, false);
  return out;
}

/// Renders golden case `n` as interleaved stereo.
std::vector<float> goldenRender(int n) {
  Ambience a(48000, 5);
  AmbientParams p = goldenParams(n);
  std::vector<float> out;
  goldenAppend(a, p, 2, 0, 2 * 48000 / kGoldenBlock, out);
  return out;
}

/// Number of one-second windows of the scene-change case.
constexpr int kSwitchSeconds = 24;

/// The scene-change case: the forest in a storm with a lightning flash after 0.4 s, the aquarium from
/// 3 s (while the thunder still rumbles), the forest again from 18 s. Interleaved stereo, 24 s.
std::vector<float> goldenSwitchRender() {
  Ambience a(48000, 5);
  AmbientParams p;
  p.kind = AmbientKind::Forest;
  p.night = 0.5f; p.rain = 0.6f; p.storm = 0.8f; p.whale = 0.6f;
  std::vector<float> out, buf(2 * kGoldenBlock);
  for (int block = 0; block < kSwitchSeconds * 48000 / kGoldenBlock; ++block) {
    if (block == 4) { ++p.flashes; ++p.nibbles; }
    if (block == 30) p.kind = AmbientKind::Aquarium;
    if (block == 180) p.kind = AmbientKind::Forest;
    a.render(p, buf.data(), kGoldenBlock);
    out.insert(out.end(), buf.begin(), buf.end());
  }
  return out;
}

/// The RMS of channel `ch` (of `channels`) in window `w` of an interleaved render.
float goldenRms(const std::vector<float>& s, int channels, int ch, int w) {
  double sum = 0;
  for (int i = 0; i < kGoldenWindowFrames; ++i) {
    const float v = s[static_cast<size_t>((w * kGoldenWindowFrames + i) * channels + ch)];
    sum += static_cast<double>(v) * v;
  }
  return static_cast<float>(std::sqrt(sum / kGoldenWindowFrames));
}

/// RMS per window and channel of the stereo golden cases (written by --print-golden).
const float kGolden[kGoldenCases][2][kGoldenWindows] = {
#include "ambience_golden.inc"
};
/// RMS per second and channel of the scene-change case, recorded from the synth that always ran the
/// forest's chain, before the chain was skipped outside the forest.
const float kGoldenSwitch[2][kSwitchSeconds] = {
#include "ambience_golden_switch.inc"
};
}  // namespace

TEST(ambience_stereo_output_matches_the_golden_values_of_every_scene) {
  for (int n = 0; n < kGoldenCases; ++n) {
    const std::vector<float> s = goldenRender(n);
    for (float v : s) CHECK(std::isfinite(v) && std::fabs(v) <= 1.0f);
    for (int ch = 0; ch < 2; ++ch)
      for (int w = 0; w < kGoldenWindows; ++w) {
        const float got = goldenRms(s, 2, ch, w), want = kGolden[n][ch][w];
        if (std::fabs(got - want) > 1e-6f + kGoldenTolerance * want)
          std::printf("  case %d ch %d window %d: got %.6g, golden %.6g\n", n, ch, w, got, want);
        CHECK(std::fabs(got - want) <= 1e-6f + kGoldenTolerance * want);
      }
  }
}


TEST(leaving_and_re_entering_the_forest_sounds_as_it_did_when_its_chain_always_ran) {
  // Recorded before the forest's wind, rain, thunder and crickets were skipped outside the forest.
  // It covers the hard part: leaving while the thunder still rumbles, where the silent chain must
  // still advance the random generator exactly as before, or the aquarium's noise and bubbles change.
  const std::vector<float> s = goldenSwitchRender();
  for (float v : s) CHECK(std::isfinite(v) && std::fabs(v) <= 1.0f);
  for (int ch = 0; ch < 2; ++ch)
    for (int w = 0; w < kSwitchSeconds; ++w) {
      double sum = 0;
      for (int i = 0; i < 48000; ++i) sum += static_cast<double>(s[(w * 48000 + i) * 2 + ch]) * s[(w * 48000 + i) * 2 + ch];
      const float got = static_cast<float>(std::sqrt(sum / 48000)), want = kGoldenSwitch[ch][w];
      if (std::fabs(got - want) > 1e-6f + kGoldenTolerance * want) std::printf("  ch %d second %d: got %.6g, golden %.6g\n", ch, w, got, want);
      CHECK(std::fabs(got - want) <= 1e-6f + kGoldenTolerance * want);
    }
}

// ------------------------------------------------- ambience in other layouts ---

TEST(the_stereo_pair_of_a_layout_is_found) {
  int l = -1, r = -1;
  CHECK(Layout::named(LayoutId::S7_1_4).stereoPair(l, r) && l == 0 && r == 1);
  DeviceChannel swapped[2] = {{100, 0, false, true}, {-100, 0, false, true}};  // right speaker first
  CHECK(Layout::fromDevice(swapped, 2).stereoPair(l, r) && l == 1 && r == 0);
  DeviceChannel odd[4] = {{0, 0, true, true}, {-60, 0, false, true}, {20, 0, false, true}, {0, 0, false, false}};
  CHECK(Layout::fromDevice(odd, 4).stereoPair(l, r) && l == 1 && r == 2);  // LFE and unknown channels are skipped
  DeviceChannel one[1] = {{0, 0, false, true}};
  CHECK(Layout::fromDevice(one, 1).stereoPair(l, r) && l == 0 && r == 0);
  DeviceChannel none[1] = {{0, 0, true, true}};
  CHECK(!Layout::fromDevice(none, 1).stereoPair(l, r) && l == -1 && r == -1);
}

TEST(ambience_in_every_layout_is_finite_bounded_deterministic_and_stays_in_its_buffer) {
  for (int i = 0; i < kLayoutCount; ++i) {
    const Layout layout = Layout::named(static_cast<LayoutId>(i));
    const int ch = layout.channels();
    std::vector<float> first;
    for (int run = 0; run < 2; ++run) {
      Ambience a(48000, 5, layout);
      AmbientParams p = goldenParams(4);  // the desert with its catastrophe
      std::vector<float> out;
      goldenAppend(a, p, ch, 0, 10, out);
      for (float v : out) CHECK(std::isfinite(v) && std::fabs(v) <= 1.0f);
      CHECK(out.size() == static_cast<size_t>(ch) * kGoldenBlock * 10);
      if (run == 0) first = out;
      else CHECK(out == first);  // the same seed gives the same sound
    }
    // The synth writes exactly frames * channels values and nothing beyond.
    Ambience a(48000, 5, layout);
    AmbientParams p = goldenParams(0);
    std::vector<float> buf(static_cast<size_t>(ch) * 100 + 32, 12345.0f);
    a.render(p, buf.data(), 100);
    for (size_t k = static_cast<size_t>(ch) * 100; k < buf.size(); ++k) CHECK(buf[k] == 12345.0f);
  }
}

TEST(ambience_in_a_layout_plays_the_stereo_pair_on_the_front_speakers_exactly_as_in_stereo) {
  // However many speakers there are, the front left and right play the stereo synth bit for bit while
  // nothing is placed (a placed sound is spread over the speakers, so it leaves the front pair): the
  // forest's wind and rain, by day, with no storm and no strike. Their bed draws from the same generator as ever.
  auto render = [](const Layout& layout) {
    Ambience a(48000, 5, layout);
    AmbientParams p = goldenParams(0);
    p.storm = 0;
    p.night = 0;
    const int ch = layout.channels();
    std::vector<float> buf(static_cast<size_t>(ch) * kGoldenBlock), out;
    for (int block = 0; block < 2 * 48000 / kGoldenBlock; ++block) {
      a.render(p, buf.data(), kGoldenBlock);
      out.insert(out.end(), buf.begin(), buf.end());
    }
    return out;
  };
  const std::vector<float> stereo = render(Layout::named(LayoutId::Stereo));
  for (int i = 0; i < kLayoutCount; ++i) {
    const Layout layout = Layout::named(static_cast<LayoutId>(i));
    const int ch = layout.channels();
    const std::vector<float> out = render(layout);
    for (size_t f = 0; f < stereo.size() / 2; ++f)
      for (int c = 0; c < 2; ++c) CHECK(out[f * ch + c] == stereo[f * 2 + c]);
  }
}

TEST(ambience_in_a_layout_is_silent_when_the_scene_is_silent_or_muted) {
  for (int i = 0; i < kLayoutCount; ++i) {
    const Layout layout = Layout::named(static_cast<LayoutId>(i));
    Ambience a(48000, 3, layout);
    AmbientParams p;  // AmbientKind::Silent
    std::vector<float> out(static_cast<size_t>(layout.channels()) * 4800);
    a.render(p, out.data(), 4800);
    for (float v : out) CHECK(v == 0.0f);
    p = goldenParams(0);
    p.volume = 0;
    Ambience b(48000, 3, layout);
    for (int k = 0; k < 3; ++k) b.render(p, out.data(), 4800);
    for (float v : out) CHECK(std::fabs(v) < 1e-6f);
  }
}

TEST(a_single_speaker_plays_the_mean_of_the_stereo_pair) {
  DeviceChannel one[1] = {{0, 0, false, true}};
  Ambience a(48000, 5, Layout::fromDevice(one, 1));
  AmbientParams p = placedFreeParams();
  std::vector<float> mono;
  goldenAppend(a, p, 1, 0, 10, mono, false);
  const std::vector<float> stereo = placedFreeRender();
  for (size_t f = 0; f < mono.size(); ++f) CHECK(std::fabs(mono[f] - 0.5f * (stereo[2 * f] + stereo[2 * f + 1])) < 1e-6f);
}

TEST(changing_the_layout_while_rendering_keeps_the_sound_going_and_does_not_click) {
  // One second in stereo, then 5.1 for a second, then 7.1.4: L and R must continue exactly as if
  // nothing had happened (no reset), and the new channels must come in without a jump.
  const std::vector<float> reference = placedFreeRender();
  Ambience a(48000, 5);
  AmbientParams p = placedFreeParams();
  std::vector<float> stereoPart, part51, part714;
  goldenAppend(a, p, 2, 0, 5, stereoPart, false);
  a.setLayout(Layout::named(LayoutId::S5_1));
  CHECK(a.layout().channels() == 6);
  goldenAppend(a, p, 6, 5, 5, part51, false);
  a.setLayout(Layout::named(LayoutId::S7_1_4));
  goldenAppend(a, p, 12, 10, 10, part714, false);
  for (size_t f = 0; f < reference.size() / 2; ++f) {
    float l, r;
    if (f < stereoPart.size() / 2) { l = stereoPart[2 * f]; r = stereoPart[2 * f + 1]; }
    else if (f < stereoPart.size() / 2 + part51.size() / 6) { const size_t g = f - stereoPart.size() / 2; l = part51[6 * g]; r = part51[6 * g + 1]; }
    else { const size_t g = f - stereoPart.size() / 2 - part51.size() / 6; l = part714[12 * g]; r = part714[12 * g + 1]; }
    CHECK(l == reference[2 * f] && r == reference[2 * f + 1]);
  }

  // A channel that changes its role fades in along a known curve instead of jumping: swap the order of
  // two speakers. The signal itself is noise, so the test compares with the continuing stereo signal
  // times the gain 1 - (1 - k)^(n + 1) of a one-pole fade with k = 1 - exp(-1 / (20 ms * rate)).
  Ambience b(48000, 5);
  AmbientParams q = placedFreeParams();
  std::vector<float> before, after;
  goldenAppend(b, q, 2, 0, 10, before, false);
  DeviceChannel swapped[2] = {{100, 0, false, true}, {-100, 0, false, true}};
  b.setLayout(Layout::fromDevice(swapped, 2));
  goldenAppend(b, q, 2, 10, 1, after, false);
  const double k = 1.0 - std::exp(-1.0 / (0.02 * 48000.0));
  float level = 0;
  for (size_t f = 0; f < kGoldenBlock; ++f) {
    const double gain = 1.0 - std::pow(1.0 - k, static_cast<double>(f + 1));
    const float left = reference[2 * (10 * kGoldenBlock + f)], right = reference[2 * (10 * kGoldenBlock + f) + 1];
    CHECK(std::fabs(after[2 * f] - right * gain) < 1e-5);      // channel 0 is now the right speaker
    CHECK(std::fabs(after[2 * f + 1] - left * gain) < 1e-5);   // channel 1 the left one
    level = std::max(level, std::fabs(after[2 * f]));
  }
  CHECK(level > 0.15f);  // the sound is loud, so an unfaded swap would have been plainly different
  CHECK(std::fabs(before[2 * (10 * kGoldenBlock - 1)] - reference[2 * (10 * kGoldenBlock - 1)]) == 0.0f);  // same run as the reference
}

// ------------------------------------------------------- per-speaker bed chains ---

namespace {
/// Renders `seconds` of the scene `kind` in `layout` with nothing but its calm bed (optionally rain), and
/// returns the interleaved samples of the last second.
std::vector<float> bedRender(AmbientKind kind, const Layout& layout, float rain = 0, int seconds = 3, int rate = 48000, uint64_t seed = 5) {
  Ambience a(rate, seed, layout);
  AmbientParams p;
  p.kind = kind;
  p.rain = rain;
  const int ch = layout.channels();
  std::vector<float> buf(static_cast<size_t>(ch) * rate), last;
  for (int s = 0; s < seconds; ++s) a.render(p, buf.data(), rate);
  return buf;
}

/// RMS of channel `c` of an interleaved render with `ch` channels.
double channelRms(const std::vector<float>& v, int ch, int c) {
  double sum = 0;
  const size_t n = v.size() / static_cast<size_t>(ch);
  for (size_t i = 0; i < n; ++i) sum += static_cast<double>(v[i * ch + c]) * v[i * ch + c];
  return std::sqrt(sum / static_cast<double>(n));
}

/// Correlation coefficient (no mean removal) of two channels of an interleaved render.
double channelCorrelation(const std::vector<float>& v, int ch, int a, int b) {
  double sab = 0;
  const size_t n = v.size() / static_cast<size_t>(ch);
  for (size_t i = 0; i < n; ++i) sab += static_cast<double>(v[i * ch + a]) * v[i * ch + b];
  return sab / (static_cast<double>(n) * channelRms(v, ch, a) * channelRms(v, ch, b) + 1e-30);
}
}  // namespace

TEST(every_full_range_speaker_carries_the_bed_of_every_scene_except_overhead_in_the_city) {
  for (int k = 1; k < kAmbientKinds; ++k) {
    for (int i = 0; i < kLayoutCount; ++i) {
      const Layout layout = Layout::named(static_cast<LayoutId>(i));
      const int ch = layout.channels();
      const std::vector<float> out = bedRender(static_cast<AmbientKind>(k), layout);
      for (int c = 0; c < ch; ++c) {
        const double rms = channelRms(out, ch, c);
        if (layout.speaker(c).isLfe()) CHECK(rms > 1e-3);  // the bass channel has the low end
        else if (static_cast<AmbientKind>(k) == AmbientKind::City && layout.speaker(c).elevation > 1.0f) continue;  // only what the cars put there
        else CHECK(rms > 2e-3);
      }
    }
  }
}

TEST(the_beds_of_different_speakers_are_not_correlated) {
  // Independent noise per speaker: whatever pair of speakers is picked, the correlation is small.
  // (The moon's drone is a chord, whose speakers can only differ in phase; see the next test. The
  // city's cars and horns are placed sounds that several speakers share, so they correlate by design.)
  const Layout layout = Layout::named(LayoutId::S9_1_6);
  const int ch = layout.channels();
  for (AmbientKind kind : {AmbientKind::Forest, AmbientKind::Aquarium, AmbientKind::Desert, AmbientKind::Mountains}) {
    const std::vector<float> out = bedRender(kind, layout, 0, 4);
    for (int a = 0; a < ch; ++a)
      for (int b = a + 1; b < ch; ++b) {
        if (layout.speaker(a).isLfe() || layout.speaker(b).isLfe()) continue;
        CHECK(std::fabs(channelCorrelation(out, ch, a, b)) < 0.25);
      }
  }
  const std::vector<float> rainy = bedRender(AmbientKind::Forest, layout, 1.0f, 3);  // the rain's hiss as well
  for (int a = 0; a < ch; ++a)
    for (int b = a + 1; b < ch; ++b)
      if (!layout.speaker(a).isLfe() && !layout.speaker(b).isLfe()) CHECK(std::fabs(channelCorrelation(rainy, ch, a, b)) < 0.1);
}

TEST(the_moon_drone_plays_in_every_speaker_with_its_own_phase) {
  const Layout layout = Layout::named(LayoutId::S9_1_6);
  const int ch = layout.channels();
  const std::vector<float> out = bedRender(AmbientKind::Moon, layout, 0, 3);
  for (int a = 0; a < ch; ++a)
    for (int b = a + 1; b < ch; ++b) {
      if (layout.speaker(a).isLfe() || layout.speaker(b).isLfe()) continue;
      CHECK(std::fabs(channelCorrelation(out, ch, a, b)) < 0.995);  // no two speakers play the same waveform
    }
}

TEST(the_bass_channel_plays_only_low_frequencies) {
  // The rain's hiss is broadband; the bass channel must not carry any of it. The ratio of the RMS of
  // the first difference to the RMS of the signal is about 1.4 for white noise and small for a
  // low-passed one.
  const Layout layout = Layout::named(LayoutId::S5_1);
  const int ch = layout.channels();
  const std::vector<float> out = bedRender(AmbientKind::Forest, layout, 1.0f);
  const int lfe = layout.lfeIndex();
  CHECK(lfe == 3);
  auto roughness = [&](int c) {
    double d = 0;
    const size_t n = out.size() / static_cast<size_t>(ch);
    for (size_t i = 1; i < n; ++i) {
      const double w = static_cast<double>(out[i * ch + c]) - out[(i - 1) * ch + c];
      d += w * w;
    }
    return std::sqrt(d / static_cast<double>(n)) / channelRms(out, ch, c);
  };
  CHECK(channelRms(out, ch, lfe) > 0.01);
  CHECK(roughness(lfe) < 0.05);
  CHECK(roughness(0) > 0.3);  // the front speaker has the hiss
}

TEST(a_speakers_bed_does_not_depend_on_how_many_other_speakers_there_are) {
  // The centre is the third chain in 5.1 and in 7.1.4 alike, so it plays the same samples.
  const Layout small = Layout::named(LayoutId::S5_1), large = Layout::named(LayoutId::S7_1_4);
  const std::vector<float> a = bedRender(AmbientKind::Mountains, small), b = bedRender(AmbientKind::Mountains, large);
  const size_t n = a.size() / 6;
  for (size_t i = 0; i < n; ++i) CHECK(a[i * 6 + 2] == b[i * 12 + 2]);
  CHECK(channelRms(a, 6, 2) > 2e-3);
}

TEST(the_bed_is_as_loud_at_other_sample_rates) {
  // The noise density per Hz is kept, so a rear speaker's level agrees within noise at 44.1 and 48 kHz.
  const Layout layout = Layout::named(LayoutId::S5_1);
  for (AmbientKind kind : {AmbientKind::Forest, AmbientKind::Desert, AmbientKind::City}) {
    const double r48 = channelRms(bedRender(kind, layout, 0.5f, 6, 48000), 6, 4);
    const double r44 = channelRms(bedRender(kind, layout, 0.5f, 6, 44100), 6, 4);
    CHECK(r44 > 0.7 * r48 && r44 < 1.4 * r48);
  }
}

TEST(overhead_speakers_play_less_of_a_scene_with_a_small_height_share_than_ear_level_ones) {
  const Layout layout = Layout::named(LayoutId::S7_1_4);
  const int ch = layout.channels();
  // The city's overhead share (0.3) is the smallest of the scenes that have any.
  const std::vector<float> out = bedRender(AmbientKind::City, layout, 0, 6);
  double ground = 0, high = 0;
  int ng = 0, nh = 0;
  for (int c = 2; c < ch; ++c) {  // skip the stereo pair, which carries the voices
    if (layout.speaker(c).isLfe()) continue;
    if (layout.speaker(c).elevation > 1.0f) { high += channelRms(out, ch, c); ++nh; }
    else { ground += channelRms(out, ch, c); ++ng; }
  }
  CHECK(high / nh < 0.6 * ground / ng);
}

TEST(the_second_catastrophe_reaches_every_speaker_including_the_height_speakers) {
  const Layout layout = Layout::named(LayoutId::S7_1_4);
  const int ch = layout.channels();
  for (AmbientKind kind : {AmbientKind::Aquarium, AmbientKind::Forest, AmbientKind::Desert, AmbientKind::Moon}) {
    Ambience a(48000, 5, layout);
    AmbientParams p;
    p.kind = kind;
    p.menace = 1.0f;
    std::vector<float> buf(static_cast<size_t>(ch) * 48000);
    for (int s = 0; s < 4; ++s) a.render(p, buf.data(), 48000);
    for (int c = 0; c < ch; ++c) {
      if (layout.speaker(c).isLfe()) continue;
      CHECK(channelRms(buf, ch, c) > 1e-3);
    }
  }
  // The city's fire streams and the yeti's menace too (the city's monsters have flames).
  Ambience a(48000, 5, layout);
  AmbientParams p;
  p.kind = AmbientKind::City;
  p.menace = 1.0f;
  p.flames = 1.0f;
  std::vector<float> buf(static_cast<size_t>(ch) * 48000);
  for (int s = 0; s < 3; ++s) a.render(p, buf.data(), 48000);
  for (int c = 0; c < ch; ++c) CHECK(channelRms(buf, ch, c) > 1e-3);
}

TEST(speakers_that_join_later_come_in_without_a_jump_and_a_scene_switch_keeps_every_chain_going) {
  Ambience a(48000, 5);
  AmbientParams p;
  p.kind = AmbientKind::Forest;
  p.rain = 1.0f;
  std::vector<float> buf(2 * 4800), wide(12 * 4800);
  for (int s = 0; s < 5; ++s) a.render(p, buf.data(), 4800);
  a.setLayout(Layout::named(LayoutId::S7_1_4));
  a.render(p, wide.data(), 4800);
  // The new speakers start from silence and rise along the fade (about 20 ms), no sample jumps.
  float jump = 0;
  for (int c = 2; c < 12; ++c) {
    jump = std::max(jump, std::fabs(wide[c]));  // first frame of a new channel
    CHECK(std::fabs(wide[c]) < 0.02f);
  }
  CHECK(jump < 0.02f);
  // Switching the scene fades the chains over with the presence, as for the stereo pair: no sample of any speaker jumps.
  p.kind = AmbientKind::Desert;
  float prev[12];
  for (int c = 0; c < 12; ++c) prev[c] = wide[11 * 12 + c];
  a.render(p, wide.data(), 4800);
  for (int f = 0; f < 4800; ++f)
    for (int c = 0; c < 12; ++c) {
      CHECK(std::fabs(wide[f * 12 + c]) <= 1.0f);
      CHECK(std::fabs(wide[f * 12 + c] - prev[c]) < 0.35f);
      prev[c] = wide[f * 12 + c];
    }
}

TEST(a_speakers_level_follows_its_position_the_rear_quieter_the_centre_lower_the_heights_by_the_scene) {
  // Three speakers differing only in where the third stands: it is chain 2 in each, so it plays the
  // same noise and the same drift, and the RMS ratio is the ratio of the trims (the soft clip is
  // negligible at these levels).
  auto third = [](float azimuth, float elevation, AmbientKind kind) {
    DeviceChannel d[3] = {{-30, 0, false, true}, {30, 0, false, true}, {azimuth, elevation, false, true}};
    return channelRms(bedRender(kind, Layout::fromDevice(d, 3)), 3, 2);
  };
  const double side = third(90, 0, AmbientKind::Forest);
  CHECK(std::fabs(third(150, 0, AmbientKind::Forest) / side - 0.85) < 0.01);
  CHECK(std::fabs(third(0, 0, AmbientKind::Forest) / side - 0.7) < 0.01);
  CHECK(std::fabs(third(90, 45, AmbientKind::Forest) / side - 0.6) < 0.01);
  CHECK(third(90, 45, AmbientKind::City) < 0.6 * third(90, 0, AmbientKind::City));  // the city's bed has no overhead share, only what the cars put there
}

// ---------------------------------------------- thread boundary: seqlock and ring ---

namespace {
/// A value big enough to tear: all words are always equal.
struct Wide {
  uint32_t word[24];
  /// Sets every word to `v`.
  void fill(uint32_t v) { for (uint32_t& w : word) w = v; }
  /// True if all words agree; `v` receives the first one.
  bool consistent(uint32_t& v) const {
    v = word[0];
    for (uint32_t w : word) if (w != v) return false;
    return true;
  }
};
}  // namespace

TEST(a_seqlock_starts_with_the_default_and_hands_over_the_latest_value) {
  Seqlock<AmbientParams> link;
  AmbientParams p;
  p.volume = 0.25f;  // not the default
  CHECK(link.load(p));
  CHECK(p.kind == AmbientKind::Silent && p.volume == 1.0f);  // the default before anything was stored
  AmbientParams a;
  a.kind = AmbientKind::City;
  a.rain = 0.5f;
  a.shots = 7;
  link.store(a);
  a.shots = 9;
  link.store(a);
  AmbientParams b;
  CHECK(link.load(b) && b.kind == AmbientKind::City && b.rain == 0.5f && b.shots == 9);
}

TEST(every_field_of_the_ambient_parameters_survives_the_hand_over_to_the_audio_thread) {
  // Every byte of the struct gets its own value, so a field added later is covered without this test
  // (or the audio backends) knowing about it: the backends copy the whole struct.
  static_assert(sizeof(AmbientParams) % 4 == 0, "the struct has no tail padding, so the byte comparison is exact");
  unsigned char raw[sizeof(AmbientParams)];
  for (size_t i = 0; i < sizeof raw; ++i) raw[i] = static_cast<unsigned char>(i * 7 + 1);
  AmbientParams sent, got;
  std::memcpy(&sent, raw, sizeof raw);
  Seqlock<AmbientParams> link;
  link.store(sent);
  CHECK(link.load(got));
  CHECK(std::memcmp(&sent, &got, sizeof sent) == 0);
}

TEST(a_seqlock_reader_never_sees_a_torn_value_while_the_writer_keeps_writing) {
  Seqlock<Wide> link;
  constexpr uint32_t kWrites = 400000;
  std::atomic<bool> done{false};
  std::atomic<uint32_t> readCount{0};  // successful reads so far, for the writer to see
  std::thread writer([&] {
    Wide w;
    uint32_t seen = 0;
    for (uint32_t i = 1; i <= kWrites; ++i) {
      w.fill(i);
      link.store(w);
      if (i % 2000 != 0) continue;
      // The writer finishes within a few milliseconds, so a reader that the machine does not schedule in that time
      // (a loaded CI runner, a build running next to the test) would see too few values to prove anything. Every
      // 2000 writes the writer therefore waits (at most a second) until the reader has read something new.
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      while (readCount.load() == seen && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
      seen = readCount.load();
    }
    done = true;
  });
  uint32_t last = 0, reads = 0, torn = 0, backwards = 0;
  Wide w;
  while (!done.load()) {
    if (!link.load(w)) continue;  // a write in progress: the reader keeps what it had
    uint32_t v;
    if (!w.consistent(v)) ++torn;
    if (v < last) ++backwards;
    last = v;
    ++reads;
    readCount.store(reads);
  }
  writer.join();
  CHECK(link.load(w) && w.consistent(last) && last == kWrites);  // the final value arrives
  CHECK(torn == 0);
  CHECK(backwards == 0);  // the newest value never goes back to an older one
  CHECK(reads > 100);
}

/// Compares two sound events field by field; memcmp would also compare the padding after `kind`, which the
/// compilers (MSVC) leave undefined in a default-constructed struct.
static bool sameEvent(const SoundEvent& a, const SoundEvent& b) {
  return a.kind == b.kind && a.x == b.x && a.y == b.y && a.depth == b.depth && a.strength == b.strength;
}

TEST(an_event_ring_hands_events_over_in_order_and_an_empty_ring_gives_nothing) {
  EventRing<int, 8> ring;
  int v = -1;
  CHECK(!ring.pop(v) && v == -1);  // empty: no event, value untouched
  for (int i = 0; i < 5; ++i) ring.push(i);
  for (int i = 0; i < 5; ++i) CHECK(ring.pop(v) && v == i);
  CHECK(!ring.pop(v));
  for (int round = 0; round < 20; ++round) {  // wraps around many times
    for (int i = 0; i < 6; ++i) ring.push(round * 10 + i);
    for (int i = 0; i < 6; ++i) CHECK(ring.pop(v) && v == round * 10 + i);
  }
  CHECK(ring.dropped() == 0);
}

TEST(an_event_ring_that_overflows_drops_the_oldest_events_and_counts_them) {
  EventRing<int, 8> ring;
  for (int i = 0; i < 13; ++i) ring.push(i);  // 5 more than fit
  int v = -1;
  for (int i = 5; i < 13; ++i) CHECK(ring.pop(v) && v == i);  // the newest eight
  CHECK(!ring.pop(v));
  CHECK(ring.dropped() == 5);
  for (int i = 0; i < 8; ++i) ring.push(100 + i);  // exactly full: nothing lost
  for (int i = 0; i < 8; ++i) CHECK(ring.pop(v) && v == 100 + i);
  CHECK(ring.dropped() == 5);
}

TEST(an_event_ring_under_concurrent_use_loses_only_old_events_and_never_tears_one) {
  struct Event { uint32_t id, check, pad[6]; };
  EventRing<Event, 16> ring;  // small, so that the writer laps the reader often
  constexpr uint32_t kEvents = 300000;
  std::atomic<bool> done{false};
  std::thread writer([&] {
    Event e{};
    for (uint32_t i = 0; i < kEvents; ++i) {
      e.id = i;
      e.check = i * 2654435761u;
      for (uint32_t& p : e.pad) p = i;
      ring.push(e);
    }
    done = true;
  });
  uint64_t received = 0, bad = 0;
  int64_t last = -1;
  Event e;
  auto take = [&] {
    while (ring.pop(e)) {
      bool ok = e.check == e.id * 2654435761u;
      for (uint32_t p : e.pad) ok = ok && p == e.id;
      if (!ok) ++bad;
      if (static_cast<int64_t>(e.id) <= last) ++bad;  // strictly increasing: no repeats, no going back
      last = e.id;
      ++received;
    }
  };
  while (!done.load()) take();
  writer.join();
  take();  // the rest
  CHECK(bad == 0);
  CHECK(last == static_cast<int64_t>(kEvents) - 1);  // the newest event always arrives
  CHECK(received + ring.dropped() == kEvents);       // every event was either read or counted as lost
  CHECK(received > 0);  // (how many depends on how the scheduler interleaves the two threads: on a loaded machine the reader may see few)
}

// ------------------------------------------------------- emitters and sound events ---

namespace {
/// A world with one creature called "whale" (4 x 2 cells, layer 16) that the test can move around.
struct WhaleWorld {
  AssetPack pack = miniPack("medium: yes\n--- population\nwhale count 1\n",
                            {"name: whale\nhabitat: medium\nspeed: 0..0\ndepth: 16\n--- art\nWWWW\nWWWW\n"});
  World world{pack, pack.scene("t"), 100, 30, 5};
  /// The whale (the first one found).
  Entity& whale() {
    for (auto& e : world.entities()) if (e->sprite->name() == "whale") return *e;
    return *world.entities().front();
  }
  /// Puts the centre of the whale at column `cx`, row `cy`.
  void place(double cx, double cy, int z = 16) {
    Entity& w = whale();
    w.x = cx - w.width() / 2.0;
    w.y = cy - w.height() / 2.0;
    w.z = z;
  }
};
}  // namespace

TEST(an_emitter_stands_where_its_creature_is_on_the_stage_and_goes_off_screen_with_it) {
  WhaleWorld t;
  Emitter e = t.world.emitter("whale");
  CHECK(t.world.entities().size() >= 1 && t.whale().sprite->name() == "whale");
  t.place(50, 14);  // the middle of a 100-column scene
  e = t.world.emitter("whale");
  CHECK(e.level > 0.99f && std::fabs(e.x) < 0.01f);
  CHECK(e.depth == 0.5f);  // layer 16 of 32
  t.place(0, 14);          // the left edge
  e = t.world.emitter(EmitterKey::Whale);
  CHECK(std::fabs(e.x + 1.0f) < 0.01f && e.level > 0.2f && e.level < 0.5f);
  t.place(100, 14);        // the right edge
  CHECK(std::fabs(t.world.emitter("whale").x - 1.0f) < 0.01f);
  t.place(-20, 14);        // off-screen to the left, still heard (it starts before it is in view)
  e = t.world.emitter("whale");
  CHECK(e.x < -1.3f && e.x >= -kStageReach && e.level > 0.0f);
  t.place(-26, 14);        // as far as it is still heard: the position stops at the limit
  e = t.world.emitter("whale");
  CHECK(e.level > 0.0f && e.x == -kStageReach);
  t.place(-60, 14);        // far away: not heard, no position
  e = t.world.emitter("whale");
  CHECK(e.level == 0.0f && e.x == 0.0f && e.y == 0.0f);
  // Height: the ground is 0, the top of the screen 1.
  t.place(50, t.world.seabedY());
  const float ground = t.world.emitter("whale").y;
  t.place(50, 3);
  const float high = t.world.emitter("whale").y;
  CHECK(ground < 0.02f && high > 0.85f && high <= 1.0f);
  t.place(50, 14, 4);
  CHECK(t.world.emitter("whale").depth < 0.15f);  // a near layer
}

TEST(the_nearest_creature_gives_the_emitter_its_position_and_its_level_is_nearness) {
  AssetPack pack = miniPack("medium: yes\n--- population\nwhale count 2\n",
                            {"name: whale\nhabitat: medium\nspeed: 0..0\ndepth: 16\n--- art\nWWWW\nWWWW\n"});
  World w(pack, pack.scene("t"), 100, 30, 5);
  Entity* a = nullptr;
  Entity* b = nullptr;
  for (auto& e : w.entities()) {
    if (e->sprite->name() != "whale") continue;
    (a ? b : a) = e.get();
  }
  CHECK(a && b);
  a->x = 10 - a->width() / 2.0;  // left of the middle
  b->x = 60 - b->width() / 2.0;  // just right of it: the nearer one
  const Emitter e = w.emitter("whale");
  CHECK(std::fabs(e.x - 0.2f) < 0.01f);
  CHECK(e.level == static_cast<float>(w.nearness("whale")));
  CHECK(w.emitter("nothing").level == 0.0f);  // no such creature: no level, no position
  AmbientParams p = ambientFor(w);  // the parameters carry the emitters, with the same levels as before
  CHECK(p.emitters[static_cast<int>(EmitterKey::Whale)].level == p.whale);
  CHECK(std::fabs(p.emitters[static_cast<int>(EmitterKey::Whale)].x - 0.2f) < 0.01f);
  CHECK(p.emitters[static_cast<int>(EmitterKey::Train)].level == 0.0f);
  w.step(0.05);  // looking does not change the world: asking twice gives the same
  CHECK(w.emitter("whale").x == w.emitter("whale").x);
}

TEST(a_meteorite_strike_is_an_event_at_its_column_and_the_queue_keeps_the_latest) {
  AssetPack pack = AssetPack::embedded();
  World w(pack, pack.scene("moon"), 140, 30, 3);
  std::vector<SoundEvent> ev;
  w.takeEvents(ev);
  ev.clear();
  w.impactAt(35);
  w.takeEvents(ev);
  CHECK(ev.size() == 1 && ev[0].kind == EventKind::Impact);
  CHECK(std::fabs(ev[0].x + 0.5f) < 0.01f && ev[0].y < 0.01f && ev[0].strength == 1.0f);
  ev.clear();
  w.takeEvents(ev);
  CHECK(ev.empty());  // taken once
  for (int i = 0; i < 200; ++i) w.impactAt(70);
  w.takeEvents(ev);
  CHECK(ev.size() == World::kMaxPendingEvents);  // nobody took them: only the latest are kept
}

TEST(nibbles_squeals_crushes_and_shots_are_events_that_carry_the_place_they_happened) {
  auto count = [](const std::vector<SoundEvent>& ev, EventKind k) {
    float n = 0;
    for (const SoundEvent& e : ev) n += e.kind == k ? e.strength : 0;
    return n;
  };
  auto inStage = [](const std::vector<SoundEvent>& ev) {
    for (const SoundEvent& e : ev)
      if (std::fabs(e.x) > kStageReach || e.y < 0 || e.y > 1 || e.depth < 0 || e.depth > 1) return false;
    return true;
  };
  {  // fish nibbling at food
    AssetPack pack = miniPack("action: drop food\n--- population\nfish count 4\n",
                              {"name: fish\nhabitat: medium\nspeed: 3..3\ndepth: 5\nbehaviors: seek_food swim\n--- art\n<o>\n",
                               "name: food\ntag: food\nbehaviors: sink\n--- art\n*\n"});
    World w(pack, pack.scene("t"), 60, 24, 5);
    w.interact();
    std::vector<SoundEvent> ev;
    int nearFish = 0, nibbles = 0;
    for (int i = 0; i < 1200; ++i) {
      w.step(0.05);
      ev.clear();
      w.takeEvents(ev);
      for (const SoundEvent& e : ev) {  // each nibble is at a fish's mouth: a fish is right there
        const double cx = 30.0 + e.x * 30.0, cy = w.seabedY() - e.y * w.seabedY();
        ++nibbles;
        for (auto& f : w.entities())
          if (f->sprite->name() == "fish" && std::fabs(f->x + f->width() / 2.0 - cx) < 4 && std::fabs(f->y + f->height() / 2.0 - cy) < 3) { ++nearFish; break; }
      }
    }
    CHECK(nibbles == static_cast<int>(w.foodEatenCount()) && nibbles > 0 && nearFish == nibbles);
    CHECK(inStage(ev));
  }
  {  // an avalanche buries animals
    AssetPack pack = AssetPack::embedded();
    World w(pack, quietMountains(pack), 140, 40, 7);
    w.setTimeMode(World::TimeMode::Day);
    w.triggerRare(RarePick::Common);
    std::vector<SoundEvent> ev;
    for (int i = 0; i < 1200; ++i) { w.step(0.05); w.takeEvents(ev); }
    CHECK(w.squealCount() > 0 && count(ev, EventKind::Squeal) == static_cast<float>(w.squealCount()));
    CHECK(inStage(ev));
  }
  {  // the sandworm crushes what it reaches
    AssetPack pack = AssetPack::embedded();
    World w(pack, pack.scene("desert"), 160, 36, 3);
    w.setTimeMode(World::TimeMode::Day);
    w.triggerRare(RarePick::Common);
    std::vector<SoundEvent> ev;
    for (int i = 0; i < 1500; ++i) { w.step(0.05); w.takeEvents(ev); }
    CHECK(w.crushCount() > 3 && count(ev, EventKind::Crush) == static_cast<float>(w.crushCount()));
    CHECK(inStage(ev));
  }
  {  // the poachers' rifle shots come from the helicopter, in the air
    AssetPack pack = AssetPack::embedded();
    World w(pack, pack.scene("desert"), 160, 36, 3);
    w.setHour(12);
    w.setTimeMode(World::TimeMode::Day);
    w.triggerRare(RarePick::Other);
    std::vector<SoundEvent> ev, step;
    int shots = 0, nearChopper = 0;
    for (int i = 0; i < 2000; ++i) {
      w.step(0.05);
      step.clear();
      w.takeEvents(step);
      for (const SoundEvent& e : step) {
        if (e.kind != EventKind::Shot) continue;
        ++shots;
        const double cx = 80.0 + e.x * 80.0;  // the shot comes from the helicopter that fires it
        for (auto& h : w.entities())
          if (h->sprite->name() == "helicopter" && std::fabs(h->x + h->width() / 2.0 - cx) <= h->width() / 2.0 + 2) { ++nearChopper; break; }
      }
      ev.insert(ev.end(), step.begin(), step.end());
    }
    CHECK(w.shotCount() > 5 && count(ev, EventKind::Shot) == static_cast<float>(w.shotCount()));
    CHECK(shots > 5 && nearChopper == shots);
    CHECK(inStage(ev));
    // Same seed, same events: the sound events are as deterministic as the world.
    World w2(pack, pack.scene("desert"), 160, 36, 3);
    w2.setHour(12);
    w2.setTimeMode(World::TimeMode::Day);
    w2.triggerRare(RarePick::Other);
    std::vector<SoundEvent> ev2;
    for (int i = 0; i < 2000; ++i) { w2.step(0.05); w2.takeEvents(ev2); }
    CHECK(ev.size() == ev2.size() && std::equal(ev.begin(), ev.end(), ev2.begin(), sameEvent));
  }
}

TEST(a_lightning_strike_is_an_event_from_the_top_of_the_sky_and_the_big_meteor_is_a_strong_impact) {
  AssetPack pack = rarePack("screen-flash: 0.5\nstrike-at: 0.25\n");  // it strikes a quarter of the way across, not in the middle
  World w(pack, pack.scene("t"), 120, 30, 4);
  w.triggerRare(RarePick::Common);
  std::vector<SoundEvent> ev;
  for (int i = 0; i < 600 && w.killedCount() == 0; ++i) { w.step(0.05); w.takeEvents(ev); }
  CHECK(w.flashCount() == 1 && ev.size() == 1 && ev[0].kind == EventKind::Flash);
  CHECK(ev[0].y > 0.99f && ev[0].x < -0.3f && ev[0].x > -0.7f);  // left of the middle, where the creature struck
  for (auto& e : w.entities())  // the bolt strikes under the catastrophe that causes it
    if (e->sprite->name() == "beast") CHECK(std::fabs(60.0 + ev[0].x * 60.0 - (e->x + e->width() / 2.0)) < 2.0);
  World crusher(pack, pack.scene("t"), 100, 30, 4);
  std::vector<SoundEvent> cr;
  crusher.noteCrushed(0, 50, 10, 8);  // nothing crushed: nothing to hear
  crusher.noteCrushed(5, 25, 10, 8);  // five at once: one event that says so
  crusher.takeEvents(cr);
  CHECK(cr.size() == 1 && cr[0].kind == EventKind::Crush && cr[0].strength == 5.0f && crusher.crushCount() == 5);
  CHECK(std::fabs(cr[0].x + 0.5f) < 0.01f);
  AssetPack moonPack = AssetPack::embedded();
  World moon(moonPack, moonPack.scene("moon"), 140, 30, 3);
  moon.setHour(12);
  moon.setTimeMode(World::TimeMode::Day);
  moon.triggerRare(RarePick::Other);
  ev.clear();
  for (int i = 0; i < 400 && moon.impactCount() == 0; ++i) { moon.step(0.05); moon.takeEvents(ev); }
  float strongest = 0;
  for (const SoundEvent& e : ev) if (e.kind == EventKind::Impact) strongest = std::max(strongest, e.strength);
  CHECK(moon.impactCount() > 0 && strongest == 2.0f);
}

TEST(the_synth_keeps_the_latest_events_until_the_next_render_and_the_session_hands_them_on) {
  Ambience a(48000, 5);
  CHECK(a.pendingEvents() == 0);
  for (int i = 0; i < 40; ++i) {
    SoundEvent e;
    e.strength = static_cast<float>(i);
    a.event(e);
  }
  CHECK(a.pendingEvents() == Ambience::kMaxPendingEvents);  // the newest are kept
  AmbientParams p;
  p.kind = AmbientKind::Forest;
  std::vector<float> buf(2 * 100);
  a.render(p, buf.data(), 100);
  CHECK(a.pendingEvents() == 0);  // heard; gone
  // Through the lock-free queue, the way the audio output hands them over.
  EventRing<SoundEvent, 64> ring;
  SoundEvent in, out;
  in.kind = EventKind::Shot;
  in.x = -0.5f;
  in.y = 0.25f;
  in.depth = 0.75f;
  in.strength = 3;
  ring.push(in);
  CHECK(ring.pop(out) && sameEvent(in, out));
  // The session hands the world's events on, once.
  AssetPack pack = AssetPack::embedded();
  Session s = makeSession(pack);
  std::vector<SoundEvent> ev;
  s.takeSoundEvents(ev);
  s.step(0.05);
  s.takeSoundEvents(ev);
  s.takeSoundEvents(ev);  // taking again adds nothing
  const size_t n = ev.size();
  s.takeSoundEvents(ev);
  CHECK(ev.size() == n);
}

// ---------------------------------------------------------------- the city in space ---

namespace {
/// Per-channel RMS of the last second of 4 s of the city in `layout`, set up by `setup`.
std::vector<double> cityRms(const Layout& layout, const std::function<void(AmbientParams&)>& setup, int seconds = 4) {
  Ambience a(48000, 5, layout);
  AmbientParams p;
  p.kind = AmbientKind::City;
  setup(p);
  const int ch = layout.channels();
  std::vector<float> buf(static_cast<size_t>(ch) * 48000);
  for (int s = 0; s < seconds; ++s) a.render(p, buf.data(), 48000);
  std::vector<double> rms(static_cast<size_t>(ch));
  for (int c = 0; c < ch; ++c) rms[static_cast<size_t>(c)] = channelRms(buf, ch, c);
  return rms;
}

/// A source of the city's sound: sets its level and emitter, at stage position (x, y).
struct CitySource {
  const char* name;
  std::function<void(AmbientParams&, float x, float y)> set;
};

/// The placed sources of the city, each loud enough to stand out of the traffic.
std::vector<CitySource> citySources() {
  return {
      {"siren", [](AmbientParams& p, float x, float y) { p.siren = 1; p.emitters[static_cast<int>(EmitterKey::Siren)] = {1, x, y, 0.3f, 0}; }},
      {"wail", [](AmbientParams& p, float x, float y) { p.wail = 1; p.emitters[static_cast<int>(EmitterKey::Wail)] = {1, x, y, 0.3f, 0}; }},
      {"train", [](AmbientParams& p, float x, float y) { p.train = 1; p.emitters[static_cast<int>(EmitterKey::Train)] = {1, x, y, 0.3f, 0}; }},
      {"plane", [](AmbientParams& p, float x, float y) { p.plane = 1; p.emitters[static_cast<int>(EmitterKey::Plane)] = {1, x, y, 0.3f, 0}; }},
      {"fire", [](AmbientParams& p, float x, float y) { p.fire = 1; p.emitters[static_cast<int>(EmitterKey::Fire)] = {1, x, y, 0.3f, 0}; }},
      {"flames", [](AmbientParams& p, float x, float y) {
         p.menace = 1; p.flames = 1; p.emitters[static_cast<int>(EmitterKey::Flames)] = {1, x, y, 0.3f, 0};
         p.emitters[static_cast<int>(EmitterKey::Titan)] = {1, x, y, 0.3f, 0}; }},
      {"monster", [](AmbientParams& p, float x, float y) { p.menace = 1; p.emitters[static_cast<int>(EmitterKey::Titan)] = {1, x, y, 0.3f, 0}; }},
  };
}
}  // namespace

TEST(the_citys_sources_are_heard_from_where_they_are_left_right_and_from_the_side_when_off_screen) {
  const Layout layout = Layout::named(LayoutId::S5_1);  // L R C LFE Ls Rs
  const auto calm = cityRms(layout, [](AmbientParams&) {});  // the traffic alone, to measure what a source adds to it
  // The power a source adds to the left speakers (L, Ls) and to the right ones (R, Rs), over the calm city.
  auto added = [&](const std::vector<double>& with, int a, int b) {
    return std::max(0.0, with[static_cast<size_t>(a)] * with[static_cast<size_t>(a)] - calm[static_cast<size_t>(a)] * calm[static_cast<size_t>(a)] +
                             with[static_cast<size_t>(b)] * with[static_cast<size_t>(b)] - calm[static_cast<size_t>(b)] * calm[static_cast<size_t>(b)]);
  };
  for (const CitySource& src : citySources()) {
    const auto left = cityRms(layout, [&](AmbientParams& p) { src.set(p, -1.0f, 0.0f); });
    const auto right = cityRms(layout, [&](AmbientParams& p) { src.set(p, 1.0f, 0.0f); });
    CHECK(added(left, 0, 4) > 1.5 * added(left, 1, 5));    // at the left edge the left speakers play more
    CHECK(added(right, 1, 5) > 1.5 * added(right, 0, 4));  // and at the right edge the right ones
    // Off-screen it moves round to the side: the side speaker plays more than the front one.
    const auto off = cityRms(layout, [&](AmbientParams& p) { src.set(p, -1.5f, 0.0f); });
    CHECK(off[4] > off[0]);  // Ls (-110 degrees) louder than L (-30 degrees)
    const auto offRight = cityRms(layout, [&](AmbientParams& p) { src.set(p, 1.5f, 0.0f); });
    CHECK(offRight[5] > offRight[1]);
  }
}

TEST(a_high_source_lights_the_height_speakers_and_the_bass_follows_the_heavy_sources) {
  const Layout layout = Layout::named(LayoutId::S7_1_4);
  const int ch = layout.channels();
  auto heights = [&](const std::vector<double>& rms) {
    double sum = 0;
    for (int c = 0; c < ch; ++c) if (layout.speaker(c).elevation > 1.0f) sum += rms[static_cast<size_t>(c)] * rms[static_cast<size_t>(c)];
    return sum;
  };
  for (const CitySource& src : citySources()) {
    const double low = heights(cityRms(layout, [&](AmbientParams& p) { src.set(p, 0.0f, 0.0f); }));
    const double high = heights(cityRms(layout, [&](AmbientParams& p) { src.set(p, 0.0f, 1.0f); }));
    CHECK(high > 1.5 * low);  // the same sound is on the height speakers when it is high up
  }
  // The bass channel: the lizard's stamps (a full bass send) and the train's rumble fill it; nothing does in the calm.
  const Layout l51 = Layout::named(LayoutId::S5_1);
  const double calm = cityRms(l51, [](AmbientParams&) {})[3];
  const double stamps = cityRms(l51, [](AmbientParams& p) { p.menace = 1; p.emitters[static_cast<int>(EmitterKey::Titan)] = {1, 0, 0, 0.3f, 0}; })[3];
  const double train = cityRms(l51, [](AmbientParams& p) { p.train = 1; p.emitters[static_cast<int>(EmitterKey::Train)] = {1, 0, 0, 0.3f, 0}; })[3];
  CHECK(stamps > 3.0 * calm);
  CHECK(train > 1.05 * calm);  // its rumble is low but a small part of the traffic bed's own bass
  const double plane = cityRms(l51, [](AmbientParams& p) { p.siren = 1; p.emitters[static_cast<int>(EmitterKey::Siren)] = {1, 0, 0, 0.3f, 0}; })[3];
  CHECK(plane < 1.5 * calm);  // a siren is high: no bass of its own
}

TEST(the_placed_sounds_of_the_city_start_in_place_and_follow_a_moving_emitter_without_a_jump) {
  Ambience a(48000, 5, Layout::named(LayoutId::S5_1));
  AmbientParams p;
  p.kind = AmbientKind::City;
  p.siren = 1;
  const int ch = 6;
  std::vector<float> buf(static_cast<size_t>(ch) * 480);
  // The siren sweeps from the left edge to the right edge in 4 s, one block (10 ms) at a time.
  float prev[6] = {};
  float worst = 0;
  double leftFirst = 0, rightFirst = 0, leftLast = 0, rightLast = 0;
  for (int block = 0; block < 400; ++block) {
    const float x = -1.0f + 2.0f * static_cast<float>(block) / 399.0f;
    p.emitters[static_cast<int>(EmitterKey::Siren)] = {1, x, 0, 0.3f, 0};
    a.render(p, buf.data(), 480);
    for (int f = 0; f < 480; ++f)
      for (int c = 0; c < ch; ++c) {
        if (block > 100) worst = std::max(worst, std::fabs(buf[static_cast<size_t>(f) * ch + c] - prev[c]));
        prev[c] = buf[static_cast<size_t>(f) * ch + c];
      }
    if (block >= 100 && block < 140) for (int f = 0; f < 480; ++f) { leftFirst += buf[f * ch] * buf[f * ch]; rightFirst += buf[f * ch + 1] * buf[f * ch + 1]; }
    if (block >= 360) for (int f = 0; f < 480; ++f) { leftLast += buf[f * ch] * buf[f * ch]; rightLast += buf[f * ch + 1] * buf[f * ch + 1]; }
  }
  CHECK(leftFirst > rightFirst && rightLast > leftLast);  // it crosses from the left speakers to the right ones
  CHECK(worst < 0.12f);                                    // and no sample jumps (a siren's own swing is far smaller than a click)
}

TEST(the_fire_emitter_stands_at_the_burning_building_and_the_flame_emitter_at_the_monsters) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("city"), 130, 40, 3);
  world.setHour(12);
  world.setTimeMode(World::TimeMode::Day);
  CHECK(world.emitter(EmitterKey::Fire).level == 0.0f && world.emitter(EmitterKey::Fire).x == 0.0f);
  world.forceFireOutcome(false);
  CHECK(world.triggerRare(RarePick::Common));
  bool checked = false;
  for (int i = 0; i < 20 * 20; ++i) {
    world.step(0.05);
    const Emitter e = world.emitter(EmitterKey::Fire);
    if (world.fireLevel() > 0.5 && world.troubledColumns().size() == 1) {
      const double col = world.troubledColumns()[0];  // the burning building's middle column
      CHECK(e.level == static_cast<float>(world.fireLevel()));
      CHECK(std::fabs(e.x - (col - 65.0) / 65.0) < 0.1);
      CHECK(e.y > 0.0f && e.y < 1.0f && e.depth > 0.0f);
      CHECK(ambientFor(world).emitters[static_cast<int>(EmitterKey::Fire)].level == static_cast<float>(world.fireLevel()));
      checked = true;
    }
  }
  CHECK(checked);
}

// ------------------------------------------------------------ the mountains in space ---

namespace {
/// Per-channel RMS of the last second of 4 s of the mountains in `layout`, set up by `setup`.
std::vector<double> mountainRms(const Layout& layout, const std::function<void(AmbientParams&)>& setup) {
  Ambience a(48000, 5, layout);
  AmbientParams p;
  p.kind = AmbientKind::Mountains;
  setup(p);
  const int ch = layout.channels();
  std::vector<float> buf(static_cast<size_t>(ch) * 48000);
  for (int s = 0; s < 4; ++s) a.render(p, buf.data(), 48000);
  std::vector<double> rms(static_cast<size_t>(ch));
  for (int c = 0; c < ch; ++c) rms[static_cast<size_t>(c)] = channelRms(buf, ch, c);
  return rms;
}

/// The placed sources of the mountains: the avalanche and the yeti.
std::vector<CitySource> mountainSources() {
  return {
      {"avalanche", [](AmbientParams& p, float x, float y) { p.avalanche = 1; p.emitters[static_cast<int>(EmitterKey::Avalanche)] = {1, x, y, 0.4f, 0}; }},
      {"yeti", [](AmbientParams& p, float x, float y) { p.menace = 1; p.emitters[static_cast<int>(EmitterKey::Beast)] = {1, x, y, 0.3f, 0}; }},
  };
}
}  // namespace

TEST(the_avalanche_and_the_yeti_are_heard_from_where_they_are_and_the_avalanche_rumbles_in_the_bass) {
  const Layout layout = Layout::named(LayoutId::S5_1);  // L R C LFE Ls Rs
  const auto calm = mountainRms(layout, [](AmbientParams&) {});
  auto added = [&](const std::vector<double>& with, int a, int b) {
    return std::max(0.0, with[static_cast<size_t>(a)] * with[static_cast<size_t>(a)] - calm[static_cast<size_t>(a)] * calm[static_cast<size_t>(a)] +
                             with[static_cast<size_t>(b)] * with[static_cast<size_t>(b)] - calm[static_cast<size_t>(b)] * calm[static_cast<size_t>(b)]);
  };
  for (const CitySource& src : mountainSources()) {
    const auto left = mountainRms(layout, [&](AmbientParams& p) { src.set(p, -1.0f, 0.0f); });
    const auto right = mountainRms(layout, [&](AmbientParams& p) { src.set(p, 1.0f, 0.0f); });
    CHECK(added(left, 0, 4) > 1.5 * added(left, 1, 5));
    CHECK(added(right, 1, 5) > 1.5 * added(right, 0, 4));
    const auto off = mountainRms(layout, [&](AmbientParams& p) { src.set(p, -1.5f, 0.0f); });
    CHECK(off[4] > off[0]);  // off-screen it comes from the side
  }
  const Layout l714 = Layout::named(LayoutId::S7_1_4);
  for (const CitySource& src : mountainSources()) {  // high on the mountain: the height speakers
    auto heights = [&](float y) {
      const auto rms = mountainRms(l714, [&](AmbientParams& p) { src.set(p, 0.0f, y); });
      double sum = 0;
      for (int c = 0; c < l714.channels(); ++c) if (l714.speaker(c).elevation > 1.0f) sum += rms[static_cast<size_t>(c)] * rms[static_cast<size_t>(c)];
      return sum;
    };
    CHECK(heights(1.0f) > 1.3 * heights(0.0f));
  }
  const double lfeCalm = calm[3];
  CHECK(mountainRms(layout, [](AmbientParams& p) { p.avalanche = 1; p.emitters[static_cast<int>(EmitterKey::Avalanche)] = {1, 0, 0.5f, 0.4f, 0}; })[3] > 2.0 * lfeCalm);
  // The yeti's stamps have a bass send of their own (0.8): without it the bass channel plays only 2.9 times the calm level, with it 5.6 times.
  CHECK(mountainRms(layout, [](AmbientParams& p) { p.menace = 1; p.emitters[static_cast<int>(EmitterKey::Beast)] = {1, 0, 0, 0.3f, 0}; })[3] > 4.0 * lfeCalm);
}

TEST(a_buried_animal_squeals_from_where_it_was_buried) {
  const Layout layout = Layout::named(LayoutId::S5_1);
  double leftSide[2] = {}, rightSide[2] = {};
  for (int variant = 0; variant < 2; ++variant) {  // the event says left (-1) or right (+1)
    Ambience a(48000, 3, layout);
    AmbientParams p;
    p.kind = AmbientKind::Mountains;
    std::vector<float> buf(6 * 48000);
    a.render(p, buf.data(), 24000);
    SoundEvent e;
    e.kind = EventKind::Squeal;
    e.x = variant ? 1.0f : -1.0f;
    a.event(e);
    p.squeals = 1;
    a.render(p, buf.data(), 36000);  // 0.75 s: the squeal lasts 0.6 s
    for (int f = 0; f < 36000; ++f) {
      leftSide[variant] += static_cast<double>(buf[f * 6]) * buf[f * 6] + static_cast<double>(buf[f * 6 + 4]) * buf[f * 6 + 4];
      rightSide[variant] += static_cast<double>(buf[f * 6 + 1]) * buf[f * 6 + 1] + static_cast<double>(buf[f * 6 + 5]) * buf[f * 6 + 5];
    }
  }
  CHECK(leftSide[0] > 1.3 * rightSide[0]);  // buried on the left: the left speakers play more
  CHECK(rightSide[1] > 1.3 * leftSide[1]);  // and on the right: the right ones
}

TEST(the_eagle_cries_from_the_sky_so_the_height_speakers_play_its_shrill_notes) {
  // By day the calm mountains have almost nothing high-pitched but the eagle's cry (and the woodpecker's
  // knocks, which come from a tree). The roughness of a signal (the RMS of its first difference) tells the
  // cry from the wind: the height speakers get a share of it only because the eagle is high up.
  const Layout layout = Layout::named(LayoutId::S7_1_4);
  const int ch = layout.channels();
  for (uint64_t seed : {5ull, 7ull}) {
    Ambience a(48000, seed, layout);
    AmbientParams p;
    p.kind = AmbientKind::Mountains;
    std::vector<float> buf(static_cast<size_t>(ch) * 48000);
    std::vector<double> rough(static_cast<size_t>(ch), 0.0);
    for (int sec = 0; sec < 20; ++sec) {  // the first cry comes within 15 s
      a.render(p, buf.data(), 48000);
      for (int c = 0; c < ch; ++c)
        for (int i = 1; i < 48000; ++i) {
          const double d = static_cast<double>(buf[static_cast<size_t>(i) * ch + c]) - buf[static_cast<size_t>(i - 1) * ch + c];
          rough[static_cast<size_t>(c)] += d * d;
        }
    }
    double high = 0, ground = 0;
    for (int c = 0; c < ch; ++c) {
      if (layout.speaker(c).isLfe()) continue;
      (layout.speaker(c).elevation > 1.0f ? high : ground) += rough[static_cast<size_t>(c)];
    }
    CHECK(high > 0.25 * ground);
  }
}

TEST(the_avalanche_emitter_moves_down_the_slope_with_the_slide) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, quietMountains(pack), 140, 40, 7);
  world.setTimeMode(World::TimeMode::Day);
  CHECK(world.emitter(EmitterKey::Avalanche).level == 0.0f);
  world.triggerRare(RarePick::Common);
  float firstY = -1, lastY = -1, firstX = 0, lastX = 0;
  for (int i = 0; i < 1200; ++i) {
    world.step(0.05);
    const Emitter e = world.emitter(EmitterKey::Avalanche);
    CHECK(e.level == static_cast<float>(world.avalancheLevel()));
    CHECK(ambientFor(world).emitters[static_cast<int>(EmitterKey::Avalanche)].level == e.level);
    if (e.level <= 0) continue;
    CHECK(std::fabs(e.x) <= kStageReach && e.y >= 0 && e.y <= 1);
    if (firstY < 0) { firstY = e.y; firstX = e.x; }
    lastY = e.y;
    lastX = e.x;
  }
  CHECK(firstY > 0 && lastY >= 0);
  CHECK(firstY > lastY + 0.1f);                 // it comes down the slope: from high to low
  CHECK(std::fabs(firstX - lastX) > 0.02f);     // and across it, along the swath
}

// ------------------------------------------------------------ the desert in space ---

namespace {
/// Per-channel RMS of the last second of 4 s of the desert in `layout`, set up by `setup`.
std::vector<double> desertRms(const Layout& layout, const std::function<void(AmbientParams&)>& setup) {
  Ambience a(48000, 5, layout);
  AmbientParams p;
  p.kind = AmbientKind::Desert;
  setup(p);
  const int ch = layout.channels();
  std::vector<float> buf(static_cast<size_t>(ch) * 48000);
  for (int s = 0; s < 4; ++s) a.render(p, buf.data(), 48000);
  std::vector<double> rms(static_cast<size_t>(ch));
  for (int c = 0; c < ch; ++c) rms[static_cast<size_t>(c)] = channelRms(buf, ch, c);
  return rms;
}

/// The placed sources of the desert: the tornado, the sandworm and the helicopter.
std::vector<CitySource> desertSources() {
  return {
      {"tornado", [](AmbientParams& p, float x, float y) { p.tornado = 1; p.emitters[static_cast<int>(EmitterKey::Tornado)] = {1, x, y, 0.4f, 0}; }},
      {"worm", [](AmbientParams& p, float x, float y) { p.worm = 1; p.storm = 1; p.emitters[static_cast<int>(EmitterKey::Worm)] = {1, x, y, 0.4f, 0}; }},
      {"helicopter", [](AmbientParams& p, float x, float y) { p.menace = 1; p.emitters[static_cast<int>(EmitterKey::Chopper)] = {1, x, y, 0.4f, 0}; }},
  };
}
}  // namespace

TEST(the_tornado_the_worm_and_the_helicopter_are_heard_from_where_they_are_and_the_worm_rumbles_in_the_bass) {
  const Layout layout = Layout::named(LayoutId::S5_1);  // L R C LFE Ls Rs
  const auto calm = desertRms(layout, [](AmbientParams&) {});
  auto added = [&](const std::vector<double>& with, int a, int b) {
    return std::max(0.0, with[static_cast<size_t>(a)] * with[static_cast<size_t>(a)] - calm[static_cast<size_t>(a)] * calm[static_cast<size_t>(a)] +
                             with[static_cast<size_t>(b)] * with[static_cast<size_t>(b)] - calm[static_cast<size_t>(b)] * calm[static_cast<size_t>(b)]);
  };
  for (const CitySource& src : desertSources()) {
    const auto left = desertRms(layout, [&](AmbientParams& p) { src.set(p, -1.0f, 0.0f); });
    const auto right = desertRms(layout, [&](AmbientParams& p) { src.set(p, 1.0f, 0.0f); });
    CHECK(added(left, 0, 4) > 1.5 * added(left, 1, 5));
    CHECK(added(right, 1, 5) > 1.5 * added(right, 0, 4));
    const auto off = desertRms(layout, [&](AmbientParams& p) { src.set(p, -1.5f, 0.0f); });
    CHECK(off[4] > off[0]);  // off-screen it comes from the side
  }
  const Layout l714 = Layout::named(LayoutId::S7_1_4);
  for (const CitySource& src : desertSources()) {  // high in the air: the height speakers
    auto heights = [&](float y) {
      const auto rms = desertRms(l714, [&](AmbientParams& p) { src.set(p, 0.0f, y); });
      double sum = 0;
      for (int c = 0; c < l714.channels(); ++c) if (l714.speaker(c).elevation > 1.0f) sum += rms[static_cast<size_t>(c)] * rms[static_cast<size_t>(c)];
      return sum;
    };
    CHECK(heights(1.0f) > 1.3 * heights(0.0f));
  }
  // The worm's growl is the deepest sound of the desert: the bass channel plays it.
  CHECK(desertRms(layout, [](AmbientParams& p) { p.worm = 1; p.storm = 1; p.emitters[static_cast<int>(EmitterKey::Worm)] = {1, 0, 0, 0.4f, 0}; })[3] > 3.0 * calm[3]);
}

TEST(the_tornados_spin_turns_round_its_place_where_there_are_speakers_to_turn_through) {
  // The balance between the left and right front speakers over 50 ms windows: a roar that turns
  // swings it, one that stands still does not.
  auto swing = [](LayoutId id) {
    const Layout layout = Layout::named(id);
    Ambience a(48000, 5, layout);
    AmbientParams p;
    p.kind = AmbientKind::Desert;
    p.tornado = 1;
    p.emitters[static_cast<int>(EmitterKey::Tornado)] = {1, 0, 0, 0.4f, 0};
    const int ch = layout.channels();
    std::vector<float> buf(static_cast<size_t>(ch) * 2400);
    for (int s = 0; s < 40; ++s) a.render(p, buf.data(), 2400);  // settle
    double lo = 1e9, hi = -1e9;
    for (int s = 0; s < 120; ++s) {  // 6 s: 17 turns
      a.render(p, buf.data(), 2400);
      double l = 0, r = 0;
      for (int f = 0; f < 2400; ++f) {
        l += static_cast<double>(buf[static_cast<size_t>(f) * ch]) * buf[static_cast<size_t>(f) * ch];
        r += static_cast<double>(buf[static_cast<size_t>(f) * ch + 1]) * buf[static_cast<size_t>(f) * ch + 1];
      }
      const double balance = std::log((l + 1e-12) / (r + 1e-12));
      lo = std::min(lo, balance);
      hi = std::max(hi, balance);
    }
    return hi - lo;
  };
  const double stereo = swing(LayoutId::Stereo);
  const double surround = swing(LayoutId::S5_1);
  CHECK(surround > 1.5 * stereo);
  CHECK(surround > 1.0);
}

TEST(a_crushed_thing_is_chewed_where_it_was_crushed_and_a_shot_comes_from_its_muzzle) {
  const Layout layout = Layout::named(LayoutId::S5_1);
  for (int what = 0; what < 2; ++what) {  // 0: the worm's chewing, 1: a rifle shot
    double leftSide[2] = {}, rightSide[2] = {};
    for (int variant = 0; variant < 2; ++variant) {  // the event says left (-1) or right (+1)
      Ambience a(48000, 3, layout);
      AmbientParams p;
      p.kind = AmbientKind::Desert;
      std::vector<float> buf(6 * 48000);
      a.render(p, buf.data(), 24000);
      SoundEvent e;
      e.kind = what ? EventKind::Shot : EventKind::Crush;
      e.x = variant ? 1.0f : -1.0f;
      a.event(e);
      (what ? p.shots : p.crushes) = 1;
      a.render(p, buf.data(), 48000);
      for (int f = 0; f < 48000; ++f) {
        leftSide[variant] += static_cast<double>(buf[f * 6]) * buf[f * 6] + static_cast<double>(buf[f * 6 + 4]) * buf[f * 6 + 4];
        rightSide[variant] += static_cast<double>(buf[f * 6 + 1]) * buf[f * 6 + 1] + static_cast<double>(buf[f * 6 + 5]) * buf[f * 6 + 5];
      }
    }
    CHECK(leftSide[0] > 1.3 * rightSide[0]);
    CHECK(rightSide[1] > 1.3 * leftSide[1]);
  }
}

// ------------------------------------------------------------ the aquarium in space ---

namespace {
/// Per-channel RMS of the last second of 4 s of the aquarium in `layout`, set up by `setup`.
std::vector<double> aquariumRms(const Layout& layout, const std::function<void(AmbientParams&)>& setup) {
  Ambience a(48000, 5, layout);
  AmbientParams p;
  p.kind = AmbientKind::Aquarium;
  setup(p);
  const int ch = layout.channels();
  std::vector<float> buf(static_cast<size_t>(ch) * 48000);
  for (int s = 0; s < 4; ++s) a.render(p, buf.data(), 48000);
  std::vector<double> rms(static_cast<size_t>(ch));
  for (int c = 0; c < ch; ++c) rms[static_cast<size_t>(c)] = channelRms(buf, ch, c);
  return rms;
}

/// Energy of the first difference (the bubbles' notes, not the water's rumble) of channel `c` of an interleaved render.
double roughEnergy(const std::vector<float>& v, int ch, int c) {
  double e = 0;
  for (size_t i = 1; i < v.size() / static_cast<size_t>(ch); ++i) {
    const double d = static_cast<double>(v[i * static_cast<size_t>(ch) + static_cast<size_t>(c)]) - v[(i - 1) * static_cast<size_t>(ch) + static_cast<size_t>(c)];
    e += d * d;
  }
  return e;
}

/// The placed sources of the aquarium: the kraken's growl, the whale's song and the submarine.
std::vector<CitySource> aquariumSources() {
  return {
      {"kraken", [](AmbientParams& p, float x, float y) { p.storm = 1; p.emitters[static_cast<int>(EmitterKey::Gloom)] = {1, x, y, 0.4f, 0}; }},
      {"submarine", [](AmbientParams& p, float x, float y) { p.menace = 1; p.emitters[static_cast<int>(EmitterKey::Submarine)] = {1, x, y, 0.4f, 0}; }},
  };
}
}  // namespace

TEST(the_kraken_and_the_submarine_are_heard_from_where_they_are_and_the_water_surrounds_the_listener) {
  const Layout layout = Layout::named(LayoutId::S5_1);  // L R C LFE Ls Rs
  const auto calm = aquariumRms(layout, [](AmbientParams&) {});
  auto added = [&](const std::vector<double>& with, int a, int b) {
    return std::max(0.0, with[static_cast<size_t>(a)] * with[static_cast<size_t>(a)] - calm[static_cast<size_t>(a)] * calm[static_cast<size_t>(a)] +
                             with[static_cast<size_t>(b)] * with[static_cast<size_t>(b)] - calm[static_cast<size_t>(b)] * calm[static_cast<size_t>(b)]);
  };
  for (const CitySource& src : aquariumSources()) {
    const auto left = aquariumRms(layout, [&](AmbientParams& p) { src.set(p, -1.0f, 0.0f); });
    const auto right = aquariumRms(layout, [&](AmbientParams& p) { src.set(p, 1.0f, 0.0f); });
    CHECK(added(left, 0, 4) > 1.5 * added(left, 1, 5));
    CHECK(added(right, 1, 5) > 1.5 * added(right, 0, 4));
    const auto off = aquariumRms(layout, [&](AmbientParams& p) { src.set(p, -1.5f, 0.0f); });
    CHECK(off[4] > off[0]);  // off-screen it comes from the side
  }
  const Layout l714 = Layout::named(LayoutId::S7_1_4);
  const auto calmHigh = aquariumRms(l714, [](AmbientParams&) {});
  for (const CitySource& src : aquariumSources()) {  // high in the water: the height speakers play more of it
    auto heights = [&](float y) {
      const auto rms = aquariumRms(l714, [&](AmbientParams& p) { src.set(p, 0.0f, y); });
      double sum = 0;
      for (int c = 0; c < l714.channels(); ++c)
        if (l714.speaker(c).elevation > 1.0f) sum += rms[static_cast<size_t>(c)] * rms[static_cast<size_t>(c)] - calmHigh[static_cast<size_t>(c)] * calmHigh[static_cast<size_t>(c)];
      return sum;
    };
    CHECK(heights(1.0f) > 1.3 * heights(0.0f));
  }
  // The water is all around, overhead too: the height speakers play the bed, a good part of the ear-level speakers' level.
  double ground = 0, high = 0;
  int ng = 0, nh = 0;
  for (int c = 2; c < l714.channels(); ++c) {
    if (l714.speaker(c).isLfe()) continue;
    if (l714.speaker(c).elevation > 1.0f) { high += calmHigh[static_cast<size_t>(c)]; ++nh; }
    else { ground += calmHigh[static_cast<size_t>(c)]; ++ng; }
  }
  CHECK(high / nh > 0.3 * ground / ng);
  // The kraken's growl is the deepest sound: the bass channel plays it.
  CHECK(aquariumRms(layout, [](AmbientParams& p) { p.storm = 1; p.emitters[static_cast<int>(EmitterKey::Gloom)] = {1, 0, 0, 0.4f, 0}; })[3] > 1.8 * calm[3]);  // (the water's own bed has plenty of bass: calm 0.021, with the kraken 0.045)
}

TEST(a_nibble_a_gulp_and_a_whales_phrase_come_from_where_they_happen) {
  const Layout layout = Layout::named(LayoutId::S5_1);
  for (int what = 0; what < 3; ++what) {  // 0: a nibble's blip, 1: the kraken's gulp, 2: a whale's phrase
    double leftSide[2] = {}, rightSide[2] = {};
    for (int variant = 0; variant < 2; ++variant) {  // the place is left (-1) or right (+1)
      Ambience a(48000, 3, layout);
      AmbientParams p;
      p.kind = AmbientKind::Aquarium;
      if (what == 2) {
        p.whale = 1;
        p.emitters[static_cast<int>(EmitterKey::Whale)] = {1, variant ? 1.0f : -1.0f, 0, 0.4f, 0};
      }
      std::vector<float> buf(6 * 48000);
      a.render(p, buf.data(), 24000);
      if (what < 2) {
        SoundEvent e;
        e.kind = what ? EventKind::Crush : EventKind::Nibble;
        e.x = variant ? 1.0f : -1.0f;
        a.event(e);
        (what ? p.crushes : p.nibbles) = what ? 1 : 4;  // four blips, so that they stand out of the random bubbles
      }
      const int frames = what == 0 ? 24000 : 48000;
      for (int s = 0; s < (what == 2 ? 6 : 1); ++s) {
        a.render(p, buf.data(), frames);
        for (int f = 0; f < frames; ++f) {
          const bool rough = what == 0;  // a blip is told from the water by its pitch, a gulp and a song are loud and low
          for (int c = 0; c < 6; ++c) {
            if (c == 2 || c == 3) continue;
            const double v = rough ? static_cast<double>(buf[f * 6 + c]) - (f ? buf[(f - 1) * 6 + c] : 0.0f) : buf[f * 6 + c];
            ((c == 0 || c == 4) ? leftSide : rightSide)[variant] += v * v;
          }
        }
      }
    }
    CHECK(leftSide[0] > 1.3 * rightSide[0]);
    CHECK(rightSide[1] > 1.3 * leftSide[1]);
  }
}

TEST(a_nibble_high_in_the_water_is_heard_overhead_and_one_on_the_floor_at_ear_level) {
  // A nibble high in the water is heard overhead, one at the bottom at ear level.
  const Layout l714 = Layout::named(LayoutId::S7_1_4);
  auto overhead = [&](float y) {
    Ambience a(48000, 3, l714);
    AmbientParams p;
    p.kind = AmbientKind::Aquarium;
    const int ch = l714.channels();
    std::vector<float> buf(static_cast<size_t>(ch) * 24000);
    a.render(p, buf.data(), 24000);
    SoundEvent e;
    e.kind = EventKind::Nibble;
    e.y = y;
    a.event(e);
    p.nibbles = 4;
    a.render(p, buf.data(), 24000);
    double high = 0, ground = 0;
    for (int c = 0; c < ch; ++c) (l714.speaker(c).elevation > 1.0f ? high : ground) += l714.speaker(c).isLfe() ? 0.0 : roughEnergy(buf, ch, c);
    return high / (ground + 1e-30);
  };
  CHECK(overhead(1.0f) > 2.0 * overhead(0.0f));
}

TEST(a_string_of_bubbles_starts_anywhere_round_the_listener_and_climbs) {
  Rng rng(11);
  int behind = 0, behindLeft = 0, behindRight = 0, strings = 0;
  float lowest = 1e9f, highest = 0, firstOfLone = 0;
  int lone = 0;
  for (int i = 0; i < 600; ++i) {
    const int n = 1 + i % 3;
    Source s[3];
    Ambience::bubbleString(rng, n, s);
    ++strings;
    if (std::fabs(s[0].azimuth) > 100.0f) { ++behind; ++(s[0].azimuth < 0 ? behindLeft : behindRight); }
    for (int k = 0; k < n; ++k) {
      CHECK(s[k].elevation >= 0.0f && s[k].elevation <= 75.0f);
      CHECK(std::fabs(s[k].azimuth) <= 160.0f);
      if (k > 0) CHECK(s[k].elevation > s[k - 1].elevation || s[k].elevation == 75.0f);  // each bubble is higher than the last
      lowest = std::min(lowest, s[k].elevation);
      highest = std::max(highest, s[k].elevation);
    }
    if (n == 1) { firstOfLone += s[0].elevation; ++lone; }
  }
  CHECK(behind > strings / 10 && behindLeft > strings / 30 && behindRight > strings / 30);  // behind the listener on both sides
  CHECK(lowest < 5.0f && highest > 40.0f);
  CHECK(firstOfLone / static_cast<float>(lone) > 10.0f);  // a lone bubble is up in the water, not on the floor
}

TEST(tiny_bubbles_glint_in_the_speakers_of_a_room_and_stereo_is_left_as_it_was) {
  // Before the first bubble (0.5 s) the water is the bed and nothing else: the front speakers of a room
  // differ from a stereo pair only by the tiny bubbles that glint in them.
  const Layout stereo = Layout::named(LayoutId::Stereo), room = Layout::named(LayoutId::S5_1);
  double glints = 0;
  for (int seed = 1; seed <= 8; ++seed) {
    Ambience a(48000, static_cast<uint64_t>(seed), stereo), b(48000, static_cast<uint64_t>(seed), room);
    AmbientParams p;
    p.kind = AmbientKind::Aquarium;
    std::vector<float> x(2 * 21600), y(6 * 21600);
    a.render(p, x.data(), 21600);
    b.render(p, y.data(), 21600);
    for (int i = 0; i < 21600; ++i) {
      const double l = x[static_cast<size_t>(i) * 2] - y[static_cast<size_t>(i) * 6], r = x[static_cast<size_t>(i) * 2 + 1] - y[static_cast<size_t>(i) * 6 + 1];
      glints += l * l + r * r;
    }
  }
  CHECK(glints > 1e-4);
  // In a stereo pair there are no glints: the synth is what it always was (the golden values hold it to the sample).
  Ambience a(48000, 5, stereo), b(48000, 5, stereo);
  AmbientParams p;
  p.kind = AmbientKind::Aquarium;
  std::vector<float> x(2 * 21600), y(2 * 21600);
  a.render(p, x.data(), 21600);
  b.render(p, y.data(), 21600);
  CHECK(x == y);
}

// ------------------------------------------------------------ the forest in space ---

namespace {
/// Per-channel RMS of the last second of 4 s of the forest in `layout`, set up by `setup`.
std::vector<double> forestRms(const Layout& layout, const std::function<void(AmbientParams&)>& setup, int seconds = 4) {
  Ambience a(48000, 5, layout);
  AmbientParams p;
  p.kind = AmbientKind::Forest;
  setup(p);
  const int ch = layout.channels();
  std::vector<float> buf(static_cast<size_t>(ch) * 48000);
  for (int s = 0; s < seconds; ++s) a.render(p, buf.data(), 48000);
  std::vector<double> rms(static_cast<size_t>(ch));
  for (int c = 0; c < ch; ++c) rms[static_cast<size_t>(c)] = channelRms(buf, ch, c);
  return rms;
}

/// The placed sources of the forest: the storm on its way and the flood.
std::vector<CitySource> forestSources() {
  return {
      {"storm", [](AmbientParams& p, float x, float y) { p.storm = 1; p.emitters[static_cast<int>(EmitterKey::Gloom)] = {1, x, y, 0.4f, 0}; }},
      {"flood", [](AmbientParams& p, float x, float y) { p.menace = 1; p.emitters[static_cast<int>(EmitterKey::Flood)] = {1, x, y, 0.4f, 0}; }},
  };
}
}  // namespace

TEST(the_storm_and_the_flood_are_heard_from_where_they_are_and_rumble_in_the_bass) {
  const Layout layout = Layout::named(LayoutId::S5_1);  // L R C LFE Ls Rs
  const auto calm = forestRms(layout, [](AmbientParams&) {});
  auto added = [&](const std::vector<double>& with, int a, int b) {
    return std::max(0.0, with[static_cast<size_t>(a)] * with[static_cast<size_t>(a)] - calm[static_cast<size_t>(a)] * calm[static_cast<size_t>(a)] +
                             with[static_cast<size_t>(b)] * with[static_cast<size_t>(b)] - calm[static_cast<size_t>(b)] * calm[static_cast<size_t>(b)]);
  };
  for (const CitySource& src : forestSources()) {
    const auto left = forestRms(layout, [&](AmbientParams& p) { src.set(p, -1.0f, 0.0f); });
    const auto right = forestRms(layout, [&](AmbientParams& p) { src.set(p, 1.0f, 0.0f); });
    const double need = std::string(src.name) == "flood" ? 1.15 : 1.3;  // the flood is wide (spread 0.8) and a gentle roar beside the bed
    CHECK(added(left, 0, 4) > need * added(left, 1, 5));
    CHECK(added(right, 1, 5) > need * added(right, 0, 4));
    const auto off = forestRms(layout, [&](AmbientParams& p) { src.set(p, -1.5f, 0.0f); });
    CHECK(off[4] > off[0]);  // off-screen it comes from the side
  }
  CHECK(forestRms(layout, [](AmbientParams& p) { p.storm = 1; p.emitters[static_cast<int>(EmitterKey::Gloom)] = {1, 0, 0, 0.4f, 0}; })[3] > 2.0 * calm[3]);
  CHECK(forestRms(layout, [](AmbientParams& p) { p.menace = 1; p.emitters[static_cast<int>(EmitterKey::Flood)] = {1, 0, 0, 0.4f, 0}; })[3] > 1.4 * calm[3]);
}

TEST(a_lightning_strike_cracks_where_it_struck_and_high_in_the_sky) {
  const Layout l51 = Layout::named(LayoutId::S5_1);
  double side[2][2] = {};  // [struck left/right][left speakers/right speakers]
  for (int variant = 0; variant < 2; ++variant) {
    Ambience a(48000, 3, l51);
    AmbientParams p;
    p.kind = AmbientKind::Forest;
    std::vector<float> buf(6 * 24000);
    a.render(p, buf.data(), 24000);
    SoundEvent e;
    e.kind = EventKind::Flash;
    e.x = variant ? 1.0f : -1.0f;
    e.y = 0.5f;
    a.event(e);
    ++p.flashes;
    a.render(p, buf.data(), 12000);  // 0.25 s: the crack and the snap, before the rumble
    side[variant][0] = roughEnergy(buf, 6, 0) + roughEnergy(buf, 6, 4);
    side[variant][1] = roughEnergy(buf, 6, 1) + roughEnergy(buf, 6, 5);
  }
  CHECK(side[0][0] > 1.5 * side[0][1]);
  CHECK(side[1][1] > 1.5 * side[1][0]);
  // High in the sky the height speakers play it.
  const Layout l714 = Layout::named(LayoutId::S7_1_4);
  auto overhead = [&](float y) {
    Ambience a(48000, 3, l714);
    AmbientParams p;
    p.kind = AmbientKind::Forest;
    const int ch = l714.channels();
    std::vector<float> buf(static_cast<size_t>(ch) * 24000);
    a.render(p, buf.data(), 24000);
    SoundEvent e;
    e.kind = EventKind::Flash;
    e.y = y;
    a.event(e);
    ++p.flashes;
    a.render(p, buf.data(), 12000);
    double high = 0, ground = 0;
    for (int c = 0; c < ch; ++c) (l714.speaker(c).elevation > 1.0f ? high : ground) += l714.speaker(c).isLfe() ? 0.0 : roughEnergy(buf, ch, c);
    return high / (ground + 1e-30);
  };
  CHECK(overhead(1.0f) > 1.5 * overhead(0.0f));
}

TEST(the_rain_falls_from_above_and_the_crickets_sing_in_the_surrounds) {
  // The rain: the height speakers play a larger part of it than of the calm wind.
  const Layout l714 = Layout::named(LayoutId::S7_1_4);
  auto heightShare = [&](float rain) {
    const std::vector<float> out = bedRender(AmbientKind::Forest, l714, rain);
    double high = 0, ground = 0;
    int nh = 0, ng = 0;
    for (int c = 2; c < l714.channels(); ++c) {
      if (l714.speaker(c).isLfe()) continue;
      if (l714.speaker(c).elevation > 1.0f) { high += channelRms(out, l714.channels(), c); ++nh; }
      else { ground += channelRms(out, l714.channels(), c); ++ng; }
    }
    return (high / nh) / (ground / ng);
  };
  CHECK(heightShare(1.0f) > 1.3 * heightShare(0.0f));
  // The crickets at night: in a room they are behind and beside the listener, not in front.
  const Layout l51 = Layout::named(LayoutId::S5_1);
  for (AmbientKind kind : {AmbientKind::Forest, AmbientKind::Desert}) {
    Ambience a(48000, 5, l51);
    AmbientParams p;
    p.kind = kind;
    p.night = 1;
    std::vector<float> buf(6 * 48000 * 3);
    a.render(p, buf.data(), 48000 * 3);
    const double front = roughEnergy(buf, 6, 0) + roughEnergy(buf, 6, 1) + roughEnergy(buf, 6, 2);
    const double rear = roughEnergy(buf, 6, 4) + roughEnergy(buf, 6, 5);
    CHECK(rear > 2.0 * front);  // (forest 3.6, desert 2.9 as measured)
  }
}

TEST(the_flood_emitter_runs_in_with_the_front_of_the_water_along_the_ground) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("forest"), 160, 36, 3);
  CHECK(world.emitter(EmitterKey::Flood).level == 0.0f);
  world.triggerRare(RarePick::Other);
  float firstX = 9, lastX = 9, peak = 0;
  for (int i = 0; i < 700; ++i) {
    world.step(0.05);
    const Emitter e = world.emitter(EmitterKey::Flood);
    CHECK(e.level == static_cast<float>(world.floodLevel()));
    if (e.level <= 0) continue;
    CHECK(std::fabs(e.x) <= kStageReach && e.y == 0.0f);  // along the ground
    if (firstX == 9) firstX = e.x;
    lastX = e.x;
    peak = std::max(peak, e.level);
  }
  CHECK(peak > 0.5f);
  CHECK(std::fabs(lastX - firstX) > 1.0f);  // the front crosses most of the stage
}

TEST(a_single_speaker_plays_the_placed_sounds_as_loud_as_the_old_mean_of_the_stereo_pair) {
  DeviceChannel one[1] = {{0, 0, false, true}};
  const Layout mono = Layout::fromDevice(one, 1), stereo = Layout::named(LayoutId::Stereo);
  auto sirenRms = [&](const Layout& layout, bool siren) {
    Ambience a(48000, 5, layout);
    AmbientParams p;
    p.kind = AmbientKind::City;
    if (siren) { p.siren = 1; p.emitters[static_cast<int>(EmitterKey::Siren)] = {1, 0, 0, 0.3f, 0}; }
    const int ch = layout.channels();
    std::vector<float> buf(static_cast<size_t>(ch) * 48000 * 3);
    a.render(p, buf.data(), 48000 * 3);
    return channelRms(buf, ch, 0);
  };
  const double monoAdded = std::sqrt(std::pow(sirenRms(mono, true), 2) - std::pow(sirenRms(mono, false), 2));
  const double stereoAdded = std::sqrt(std::pow(sirenRms(stereo, true), 2) - std::pow(sirenRms(stereo, false), 2));
  CHECK(monoAdded > 0.85 * stereoAdded && monoAdded < 1.15 * stereoAdded);  // (it was 1.41 times as loud)
}

// ------------------------------------------------------------ the moon in space ---

namespace {
/// Per-channel RMS of the last second of 4 s of the moon in `layout`, set up by `setup`.
std::vector<double> moonRms(const Layout& layout, const std::function<void(AmbientParams&)>& setup) {
  Ambience a(48000, 5, layout);
  AmbientParams p;
  p.kind = AmbientKind::Moon;
  setup(p);
  const int ch = layout.channels();
  std::vector<float> buf(static_cast<size_t>(ch) * 48000);
  for (int s = 0; s < 4; ++s) a.render(p, buf.data(), 48000);
  std::vector<double> rms(static_cast<size_t>(ch));
  for (int c = 0; c < ch; ++c) rms[static_cast<size_t>(c)] = channelRms(buf, ch, c);
  return rms;
}

/// Energy of channel `c` of an interleaved render.
double channelEnergy(const std::vector<float>& v, int ch, int c) {
  double e = 0;
  for (size_t i = 0; i < v.size() / static_cast<size_t>(ch); ++i) e += static_cast<double>(v[i * static_cast<size_t>(ch) + static_cast<size_t>(c)]) * v[i * static_cast<size_t>(ch) + static_cast<size_t>(c)];
  return e;
}

/// Renders the moon in 5.1 for 0.5 s, then `seconds` more after `setup` (and the events it sends) has run, and returns what the second part played.
std::vector<float> moonAfter(const std::function<void(Ambience&, AmbientParams&)>& setup, double seconds) {
  const Layout l51 = Layout::named(LayoutId::S5_1);
  Ambience a(48000, 3, l51);
  AmbientParams p;
  p.kind = AmbientKind::Moon;
  std::vector<float> buf(6 * 24000);
  a.render(p, buf.data(), 24000);
  setup(a, p);
  std::vector<float> out(6 * static_cast<size_t>(seconds * 48000));
  a.render(p, out.data(), static_cast<int>(seconds * 48000));
  return out;
}
}  // namespace

TEST(the_big_meteor_is_heard_from_where_it_falls_high_up_and_wide_as_it_lands) {
  const Layout layout = Layout::named(LayoutId::S5_1);  // L R C LFE Ls Rs
  auto meteor = [](float x, float y, float level) {
    return [=](AmbientParams& p) { p.menace = 1; p.emitters[static_cast<int>(EmitterKey::Meteor)] = {level, x, y, 0.1f, 0}; };
  };
  const auto calm = moonRms(layout, [](AmbientParams&) {});
  auto added = [&](const std::vector<double>& with, int a, int b) {
    return std::max(0.0, with[static_cast<size_t>(a)] * with[static_cast<size_t>(a)] - calm[static_cast<size_t>(a)] * calm[static_cast<size_t>(a)] +
                             with[static_cast<size_t>(b)] * with[static_cast<size_t>(b)] - calm[static_cast<size_t>(b)] * calm[static_cast<size_t>(b)]);
  };
  const auto left = moonRms(layout, meteor(-1.0f, 0.0f, 0.3f));
  const auto right = moonRms(layout, meteor(1.0f, 0.0f, 0.3f));
  CHECK(added(left, 0, 4) > 1.5 * added(left, 1, 5));
  CHECK(added(right, 1, 5) > 1.5 * added(right, 0, 4));
  const auto off = moonRms(layout, meteor(-1.5f, 0.0f, 0.3f));
  CHECK(off[4] > off[0]);  // off-screen it comes from the side
  // When it lands it fills the room: the same level in every speaker, wherever it hit.
  const auto landing = moonRms(layout, meteor(-1.0f, 0.0f, 1.0f));
  CHECK(landing[0] < 1.3 * landing[1] && landing[1] < 1.3 * landing[0]);
  CHECK(landing[3] > 2.0 * calm[3]);  // and the bass channel rumbles
  // High up (far) it is heard overhead.
  const Layout l714 = Layout::named(LayoutId::S7_1_4);
  const auto calmHigh = moonRms(l714, [](AmbientParams&) {});
  auto heights = [&](float y) {
    const auto rms = moonRms(l714, meteor(0.0f, y, 0.3f));
    double sum = 0;
    for (int c = 0; c < l714.channels(); ++c)
      if (l714.speaker(c).elevation > 1.0f) sum += rms[static_cast<size_t>(c)] * rms[static_cast<size_t>(c)] - calmHigh[static_cast<size_t>(c)] * calmHigh[static_cast<size_t>(c)];
    return sum;
  };
  CHECK(heights(1.0f) > 1.3 * heights(0.0f));
}

TEST(a_meteorite_thuds_where_it_struck_and_the_big_one_fills_the_whole_width) {
  auto sides = [](float x, float strength) {
    const std::vector<float> out = moonAfter([&](Ambience& a, AmbientParams& p) {
      SoundEvent e;
      e.kind = EventKind::Impact;
      e.x = x;
      e.strength = strength;
      a.event(e);
      ++p.impacts;
    }, 0.5);
    return std::make_pair(channelEnergy(out, 6, 0) + channelEnergy(out, 6, 4), channelEnergy(out, 6, 1) + channelEnergy(out, 6, 5));
  };
  const auto left = sides(-1.0f, 1.0f), right = sides(1.0f, 1.0f);
  CHECK(left.first > 1.5 * left.second);
  CHECK(right.second > 1.5 * right.first);
  const auto big = sides(-1.0f, 2.0f);  // the big one is wide: it is not on one side
  CHECK(big.first < 1.3 * big.second && big.second < 1.3 * big.first);
  // The debris that patters down lands round the impact, not only where it struck: a strike straight ahead
  // is heard in the front left and right speakers too, not only in the centre one.
  const std::vector<float> centre = moonAfter([](Ambience& a, AmbientParams& p) {
    SoundEvent e;
    e.kind = EventKind::Impact;
    a.event(e);
    ++p.impacts;
  }, 5.0);
  const double lr = roughEnergy(centre, 6, 0) + roughEnergy(centre, 6, 1), c = roughEnergy(centre, 6, 2);
  CHECK(lr > 0.5 * c);
}

TEST(the_aliens_warble_comes_from_the_alien) {
  double energy[2][2] = {};
  for (int variant = 0; variant < 2; ++variant) {
    const std::vector<float> out = moonAfter([&](Ambience&, AmbientParams& p) {
      ++p.flashes;
      p.emitters[static_cast<int>(EmitterKey::Alien)] = {1, variant ? 1.0f : -1.0f, 0, 0.4f, 0};
    }, 1.0);
    energy[variant][0] = channelEnergy(out, 6, 0) + channelEnergy(out, 6, 4);
    energy[variant][1] = channelEnergy(out, 6, 1) + channelEnergy(out, 6, 5);
  }
  CHECK(energy[0][0] > 1.5 * energy[0][1]);
  CHECK(energy[1][1] > 1.5 * energy[1][0]);
}

TEST(the_meteor_emitter_comes_down_from_the_sky_with_the_falling_meteor) {
  AssetPack pack = AssetPack::embedded();
  World world(pack, pack.scene("moon"), 160, 36, 3);
  world.setTimeMode(World::TimeMode::Day);
  CHECK(world.emitter(EmitterKey::Meteor).level == 0.0f);
  world.triggerRare(RarePick::Other);
  float firstY = -1, lastY = -1, previous = 0;
  for (int i = 0; i < 260; ++i) {
    world.step(0.05);
    const Emitter e = world.emitter(EmitterKey::Meteor);
    CHECK(e.level == static_cast<float>(world.asteroidLevel()));
    if (e.level <= 0) continue;
    CHECK(e.level >= previous);  // it falls nearer
    previous = e.level;
    CHECK(std::fabs(e.x) <= kStageReach && e.y >= 0 && e.y <= 1);
    if (firstY < 0) firstY = e.y;
    lastY = e.y;
  }
  CHECK(firstY > 0.8f && lastY < 0.4f);  // from high in the sky to low
}

int main(int argc, char** argv) {
  // Developer options: print the golden table, or dump the raw samples of the golden cases to a
  // file (to compare two builds bit for bit on one machine).
  if (argc > 1 && std::strcmp(argv[1], "--print-golden") == 0) {
    for (int n = 0; n < kGoldenCases; ++n) {
      const std::vector<float> s = goldenRender(n);
      std::printf("{\n");
      for (int ch = 0; ch < 2; ++ch) {
        std::printf("  {");
        for (int w = 0; w < kGoldenWindows; ++w) std::printf("%s%.6gf", w ? ", " : "", goldenRms(s, 2, ch, w));
        std::printf("}%s\n", ch ? "" : ",");
      }
      std::printf("}%s\n", n + 1 < kGoldenCases ? "," : "");
    }
    return 0;
  }
  if (argc > 2 && std::strcmp(argv[1], "--dump-golden") == 0) {
    std::FILE* f = std::fopen(argv[2], "wb");
    if (!f) return 1;
    for (int n = 0; n < kGoldenCases; ++n) {
      const std::vector<float> s = goldenRender(n);
      std::fwrite(s.data(), sizeof(float), s.size(), f);
    }
    std::fclose(f);
    return 0;
  }

  for (auto& [name, fn] : registry()) {
    const int before = gFailures;
    fn();
    std::printf("%s %s\n", gFailures == before ? "ok  " : "FAIL", name);
  }
  std::printf("%d failure(s)\n", gFailures);
  return gFailures ? 1 : 0;
}
