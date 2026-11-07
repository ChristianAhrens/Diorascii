// Dependency-free test runner: each TEST registers itself, main() runs them all.
#include <algorithm>
#include <cstdio>
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
                        "dust", "flag", "rock"})
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
      CHECK(term.at(x, y).defaultBg);                     // the terminal's own background
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
