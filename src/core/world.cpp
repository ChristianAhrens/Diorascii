/// @file core/world.cpp
/// @brief Simulation, behaviors and scene rendering.

#include "core/world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "core/textutil.h"

namespace dio {

namespace {

// ---------------------------------------------------------------- colors ---

/// Color pool for warm-colored creatures.
constexpr Rgb kWarm[] = {{255, 140, 40}, {255, 200, 60}, {240, 90, 70},
                         {255, 130, 170}, {250, 230, 120}};
/// Color pool for cool-colored creatures.
constexpr Rgb kCool[] = {{70, 200, 230}, {90, 140, 250}, {80, 220, 170},
                         {170, 120, 240}, {120, 230, 120}};

/// Color pool for earth-toned creatures (horses, camels): creams, tans, greys and browns.
constexpr Rgb kEarth[] = {{240, 205, 150}, {228, 231, 238}, {205, 160, 110},
                          {190, 140, 90},  {235, 220, 190}};

/// Random color from the named pool ("warm", "earth", "cool" or any other name for warm and cool).
Rgb pickFromPool(const std::string& pool, Rng& rng) {
  if (pool == "earth") return kEarth[rng.rangeInt(0, 4)];
  const bool warm = pool == "warm" || (pool != "cool" && rng.chance(0.5));
  const auto& set = warm ? kWarm : kCool;
  return set[rng.rangeInt(0, 4)];
}

/// Color a mask character stands for: digits index the entity palette, letters are
/// fixed colors (lower case is darker), anything else is the entity default.
Rgb maskColor(char32_t m, const Entity& e) {
  if (m >= U'1' && m <= U'9') return e.pal[m - U'1'];
  Rgb base;
  switch (m | 0x20) {  // fold to lower case
    case U'w': base = {240, 240, 240}; break;
    case U'k': base = {70, 70, 85}; break;
    case U'r': base = {225, 70, 70}; break;
    case U'g': base = {80, 195, 100}; break;
    case U'b': base = {80, 120, 235}; break;
    case U'c': base = {90, 215, 225}; break;
    case U'y': base = {240, 215, 80}; break;
    case U'm': base = {205, 100, 205}; break;
    case U'o': base = {245, 155, 60}; break;
    case U't': base = {240, 205, 150}; break;
    case U'n': base = {140, 92, 52}; break;
    case U'p': base = {245, 150, 175}; break;
    case U'l': base = {228, 231, 238}; break;
    case U's': base = {214, 176, 112}; break;
    default: return e.fg;
  }
  return (m >= U'a' && m <= U'z') ? scale(base, 0.6) : base;
}

/// Backdrop color at the given light: fades towards a dark blue at night and towards the haze
/// color in a sandstorm.
Rgb backdrop(Rgb c, const Light& l) {
  Rgb out = c;
  if (l.day < 1.0) out = lerp(lerp(c, {6, 9, 30}, 0.82), c, l.day);
  return l.haze > 0 ? lerp(out, l.hazeColor, l.haze) : out;
}

/// Color of a thing in the world at the given light: darker and bluer at night, dusty in a
/// sandstorm (less so than the backdrop, so animals stay visible).
Rgb shade(Rgb c, const Light& l) {
  Rgb out = c;
  if (l.day < 1.0) {
    const Rgb night{static_cast<uint8_t>(c.r * 0.72), static_cast<uint8_t>(c.g * 0.78),
                    static_cast<uint8_t>(c.b * 0.95)};
    out = lerp(night, c, l.day);
  }
  return l.haze > 0 ? lerp(out, l.hazeColor, l.haze * 0.6) : out;
}

/// True if the entity is shown at the given light.
bool visibleNow(const Entity& e, const Light& l, bool hasCycle) {
  if (!hasCycle) return true;
  if (e.sleepBelow > 0 && l.sun < e.sleepBelow) return false;  // asleep
  if (e.active == Entity::Active::Always) return true;
  return e.active == Entity::Active::Day ? l.sun > 0.35 : l.sun < 0.35;
}

/// Draws one entity into out, shaded for the light, skipping rows from maxRow down.
void blit(Frame& out, const Entity& e, Style style, const Light& l, int maxRow) {
  const SpriteFrame& f = e.sprite->frame(e.mirrored, e.frame);
  const int ox = static_cast<int>(std::floor(e.x));
  const int oy = static_cast<int>(std::floor(e.y));
  auto colorOf = [&](char32_t m) {
    const Rgb c = maskColor(m, e);
    return e.glow ? c : shade(c, l);
  };
  for (int r = 0; r < e.height(); ++r) {
    for (int c = 0; c < e.width(); ++c) {
      const int px = ox + c, py = oy + r;
      if (!out.inBounds(px, py) || py >= maxRow) continue;
      const char32_t ch = f.art[r][c], m = f.mask[r][c];
      Cell& cell = out.at(px, py);
      if (ch == U' ') {
        if (m == U' ') continue;
        cell.ch = U' ';
        if (m != U'_') {  // solid block: keeps its color unless the style is monochrome
          cell.bg = colorOf(m);
          cell.defaultBg = style == Style::Mono;
        }
      } else {
        cell.ch = ch;
        cell.fg = colorOf(m);
        cell.defaultFg = style == Style::Mono;
        if (e.backing > 0 && style == Style::Full) cell.bg = scale(cell.bg, 1 - e.backing);
        if (e.contrast && style == Style::Full) {  // dark strokes on light backgrounds
          const double lum = (0.2126 * cell.bg.r + 0.7152 * cell.bg.g + 0.0722 * cell.bg.b) / 255.0;
          if (lum > 0.5) cell.fg = scale(cell.fg, 0.32);
        }
      }
    }
  }
}

/// Cheap deterministic hash of a cell position, for static noise such as stars.
uint32_t hash2(int x, int y) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

// -------------------------------------------------------------- behaviors ---
// Named in a sprite's `behaviors:` header. Order matters (seek_food before swim).

/// Horizontal travel; leaves the world when fully past the edge it is heading for.
void swim(Entity& e, World& w, double dt) {
  e.x += e.vx * dt;
  if ((e.vx > 0 && e.x > w.width()) || (e.vx < 0 && e.x + e.width() < 0)) e.dead = true;
}

/// Free flight in both axes (meteors, birds).
void fly(Entity& e, World& w, double dt) {
  e.x += e.vx * dt;
  e.y += e.vy * dt;
  if ((e.vx > 0 && e.x > w.width()) || (e.vx < 0 && e.x + e.width() < 0) ||
      (e.vy > 0 && e.y > w.height()) || (e.vy < 0 && e.y + e.height() < 0))
    e.dead = true;
}

/// Gentle vertical bobbing around the starting row.
void bob(Entity& e, World&, double) {
  e.y = e.anchorY + e.bobAmp * std::sin(e.age * e.bobFreq + e.phase);
}

/// Rises and disappears at the surface (bubbles).
void rise(Entity& e, World& w, double dt) {
  e.y += e.vy * dt;
  if (e.y <= w.surfaceY() + 1) e.dead = true;
}

/// Falls, drifts a little, then lies on the ground for a while before vanishing.
void sink(Entity& e, World& w, double dt) {
  e.x += 0.7 * std::sin(e.age * 1.6 + e.phase) * dt;
  e.y += e.vy * dt;
  if (e.y >= w.seabedY() - 1) {
    e.y = w.seabedY() - 1;
    e.vy = 0;
    if (e.ttl < 0) e.ttl = e.age + 10;
  }
}

/// Like swim, but the feet follow the ground: walkers dip into craters.
void walk(Entity& e, World& w, double dt) {
  swim(e, w, dt);
  e.y = e.anchorY + w.craterRows(static_cast<int>(std::floor(e.x + e.width() / 2.0)));
}

/// Thrown debris and dust: falls under the scene's gravity, gone once it lands.
void ballistic(Entity& e, World& w, double dt) {
  e.vy += w.gravity() * dt;
  e.x += e.vx * dt;
  e.y += e.vy * dt;
  if (e.vy > 0 && e.y >= w.seabedY() - 1) e.dead = true;
}

/// Streaks down and strikes the ground at the column of its head.
void meteorite(Entity& e, World& w, double dt) {
  e.x += e.vx * dt;
  e.y += e.vy * dt;
  if (e.y + e.height() >= w.seabedY()) {
    w.impactAt(e.movingRight() ? e.x + e.width() - 1 : e.x);
    e.dead = true;
  }
}

/// Drifts like swim, with an organic wobble on top (butterflies, fireflies, birds).
void wander(Entity& e, World& w, double dt) {
  e.x += 2.0 * std::sin(e.age * 1.7 + e.phase) * dt;
  swim(e, w, dt);
  e.y = e.anchorY + e.bobAmp * std::sin(e.age * e.bobFreq + e.phase) + 0.6 * std::sin(e.age * 2.9 + e.phase * 2);
}

/// Moves in hops: it rests, then jumps forward in an arc, and repeats.
void hop(Entity& e, World& w, double dt) {
  constexpr double kAirborne = 0.4;  // share of the cycle spent in the air
  const double cycle = std::fmod(e.age + e.phase, e.hopPeriod) / e.hopPeriod;
  double lift = 0;
  if (cycle < kAirborne) {
    e.x += e.vx * dt / kAirborne;  // all the travel happens in the air, so the average speed is vx
    lift = e.hopHeight * std::sin(3.14159265358979 * cycle / kAirborne);
  }
  if ((e.vx > 0 && e.x > w.width()) || (e.vx < 0 && e.x + e.width() < 0)) e.dead = true;
  e.y = e.anchorY + w.craterRows(static_cast<int>(std::floor(e.x + e.width() / 2.0))) - lift;
}

/// A raindrop: falls to the row chosen for it, makes a splash there and is gone.
void rain(Entity& e, World& w, double dt) {
  e.x += e.vx * dt;
  e.y += e.vy * dt;
  if (e.y >= e.anchorY) {
    e.dead = true;
    w.spawnEffect("splash", e.x, e.anchorY, 1, 0, 0);
  } else if (e.x < -3 || e.x > w.width() + 3) {
    e.dead = true;
  }
}

/// Now and then releases a bubble from the mouth.
void emitBubbles(Entity& e, World& w, double dt) {
  if (!w.rng().chance(e.bubbleRate * dt)) return;
  const double bx = e.movingRight() ? e.x + e.width() : e.x - 1;
  w.spawnEffect("bubble", bx, e.y + e.height() / 2, e.z - 1, 0, -3.5);
}

/// Turn towards the closest food flake, swim to it and eat it.
void seekFood(Entity& e, World& w, double dt) {
  const bool right = e.movingRight();
  Entity* f = w.nearest("food", e.x + e.width() / 2.0, e.y + e.height() / 2.0, 45);
  if (!f) return;
  const double mouthX = right ? e.x + e.width() - 1 : e.x;
  const double mouthY = e.y + e.mouthRow;
  const double dx = f->x - mouthX, dy = f->y - mouthY;
  if (std::fabs(dx) <= 1.5 && std::fabs(dy) <= 1.0) {
    f->dead = true;
    w.noteFoodEaten();
    return;
  }
  if (std::fabs(dx) > 1.5 && (dx > 0) != right && e.vx != 0) {
    e.vx = -e.vx;
    e.mirrored = !e.mirrored;
  }
  const double step = std::min(std::fabs(dy), 4.0 * dt);
  e.y += dy > 0 ? step : -step;
}

/// Eats any "prey" whose bounding box the mouth touches.
void hunt(Entity& e, World& w, double) {
  const double mx = e.movingRight() ? e.x + e.width() - 1 : e.x;
  const double my = e.y + e.mouthRow;
  for (auto& other : w.entities()) {
    if (other->dead || other->tag != "prey" || w.isAsleep(*other)) continue;
    const bool inside = mx >= other->x - 1 && mx <= other->x + other->width() &&
                        my >= other->y - 0.5 && my <= other->y + other->height();
    if (!inside) continue;
    other->dead = true;
    w.noteEaten();
    w.spawnEffect(e.huntEffect, mx - 1, my - 1, e.z - 1, 0, 0);
    ++e.kills;
    e.pauseLeft = e.feastTime;  // stays put for a while to eat
  }
}

/// Kicks up a puff of dust behind its feet now and then (galloping horses).
void emitDust(Entity& e, World& w, double dt) {
  if (!w.rng().chance(e.dustRate * dt)) return;
  const double behind = e.movingRight() ? e.x - 1 : e.x + e.width();
  w.spawnEffect("sandpuff", behind, e.y + e.height() - 1, e.z + 1, -e.vx * 0.15, -w.rng().range(0.5, 1.5));
}

/// A catastrophe: crosses the scene and, when it is the given way across, strikes once: it wipes
/// out what its header names (`wipes:`), may light up the screen, bring rain and drop a bolt.
void catastrophe(Entity& e, World& w, double dt) {
  e.x += e.vx * dt;
  if ((e.vx > 0 && e.x > w.width()) || (e.vx < 0 && e.x + e.width() < 0)) e.dead = true;
  if (e.struck) return;
  const double middle = e.x + e.width() / 2.0;
  const double progress = e.vx > 0 ? middle / w.width() : 1.0 - middle / w.width();
  if (progress < e.strikeAt) return;
  e.struck = true;
  if (e.stormTime > 0) w.startRain(e.stormTime);
  if (!e.strikeSprite.empty()) {  // a bolt from the sky to the ground right below it
    if (Entity* bolt = w.spawnEffect(e.strikeSprite, middle, 0, e.z - 1, 0, 0)) {
      bolt->x = middle - bolt->width() / 2.0;
      bolt->y = w.seabedY() - bolt->height();
    }
  }
  w.wipeOut(e.wipes, e.strikeEffect, &e);
  if (e.flashTime > 0) w.flash(e.flashTime);
}

/// Hunts the closest prey in sight: turns towards it, changes lane to line up with it and closes
/// in. It gives up chasing after its first kill (see hunt).
void chase(Entity& e, World& w, double dt) {
  if (e.kills > 0) return;
  Entity* prey = w.nearest("prey", e.x + e.width() / 2.0, e.y + e.height() / 2.0, 70);
  if (!prey) return;
  const double dx = (prey->x + prey->width() / 2.0) - (e.x + e.width() / 2.0);
  if (std::fabs(dx) > 2 && (dx > 0) != e.movingRight() && e.vx != 0) {  // turn round
    e.vx = -e.vx;
    e.mirrored = !e.mirrored;
  }
  const double lane = prey->anchorY + prey->height() - e.height();  // feet on the prey's row
  e.anchorY += std::clamp(lane - e.anchorY, -3.0 * dt, 3.0 * dt);
}

/// A tornado: crosses the scene weaving a little, whirls sand up from its foot and throws every
/// animal it meets into the air. Scenery that does not move and sleeping animals are left alone.
void twister(Entity& e, World& w, double dt) {
  e.x += (e.vx + 1.8 * std::sin(e.age * 1.3 + e.phase)) * dt;
  if ((e.vx > 0 && e.x > w.width()) || (e.vx < 0 && e.x + e.width() < 0)) e.dead = true;
  for (auto& other : w.entities()) {
    Entity& o = *other;
    if (&o == &e || o.dead || o.flung || !o.grounded || o.vx == 0 || o.tag == "tornado") continue;
    if (w.isAsleep(o)) continue;
    const double overlap = std::min(o.x + o.width(), e.x + e.width() - 1.0) - std::max(o.x, e.x + 1.0);
    if (overlap <= 0 || o.y + o.height() <= e.y) continue;  // not in the funnel's path
    // Up and away: the rise is a random height up to most of the sky, the sideways kick spins it out.
    const double rise = w.rng().range(5.0, std::max(6.0, std::min(16.0, w.height() * 0.6)));
    o.flungVx = o.vx;
    o.vx = w.rng().range(-9, 9);
    o.vy = -std::sqrt(2 * w.gravity() * rise);
    o.flung = true;
    for (int i = 0; i < 4; ++i) {
      w.spawnEffect("sandspray", o.x + w.rng().range(0, o.width()), o.y + o.height() - 1, o.z - 1,
                    w.rng().range(-5, 5), -w.rng().range(2, 6));
    }
  }
  if (w.rng().chance(16 * dt)) {
    const double foot = e.x + e.width() / 2.0 + w.rng().range(-2, 2);
    w.spawnEffect("sandspray", foot, e.y + e.height() - 1, e.z - 1, w.rng().range(-7, 7),
                  -w.rng().range(3, 9));
  }
}

/// Behavior function for a name used in a sprite header, or nullptr.
BehaviorFn findBehavior(const std::string& name) {
  static const std::map<std::string, BehaviorFn> table = {
      {"swim", swim},   {"fly", fly},           {"bob", bob},       {"rise", rise},
      {"sink", sink},   {"emit_bubbles", emitBubbles}, {"seek_food", seekFood}, {"hunt", hunt},
      {"walk", walk},   {"ballistic", ballistic},      {"meteorite", meteorite},
      {"wander", wander}, {"hop", hop},                {"rain", rain},
      {"chase", chase},   {"twister", twister},        {"emit_dust", emitDust},
      {"catastrophe", catastrophe}};
  auto it = table.find(name);
  return it == table.end() ? nullptr : it->second;
}

/// Advances the animation clock and picks the current frame.
void animate(Entity& e, double dt) {
  const size_t n = e.sprite->frameCount();
  if (e.fps <= 0 || n < 2) return;
  const double period = 1.0 / e.fps;
  e.clock += dt;
  while (e.clock >= period) {
    e.clock -= period;
    if (!e.loop && e.frame + 1 >= n) {
      if (e.dieAfterAnim) e.dead = true;
      continue;
    }
    ++e.frame;
  }
  e.frame = e.loop ? e.frame % n : std::min(e.frame, n - 1);
}

/// Everything a sprite's header can configure on an entity.
void applyProps(Entity& e, const Props& p, Rng& rng) {
  e.tag = p.str("tag");
  e.facesRight = p.str("facing", "right") == "right";
  e.fps = p.num("fps", 0);
  e.loop = p.str("loop", "yes") != "no";
  e.dieAfterAnim = p.str("die-after-animation", "no") == "yes";
  e.fg = p.color("color", {200, 210, 220});
  e.bubbleRate = p.num("bubble-rate", 0.15);
  e.bobAmp = p.num("bob-amplitude", 1.5);
  e.bobFreq = p.num("bob-frequency", 1.0);
  e.mouthRow = static_cast<int>(p.num("mouth-row", e.height() / 2));
  e.glow = p.str("glow", "no") == "yes";
  const std::string active = p.str("active", "always");
  e.active = active == "day" ? Entity::Active::Day : active == "night" ? Entity::Active::Night : Entity::Active::Always;
  e.huntEffect = p.str("hunt-effect", "splat");
  e.feastTime = p.num("feast-time", 0);
  e.dustRate = p.num("dust-rate", 3.0);
  e.contrast = p.str("contrast", "no") == "yes";
  e.life = p.str("life", "no") == "yes";
  e.strikeAt = std::clamp(p.num("strike-at", 0.5), 0.0, 1.0);
  e.wipes = p.str("wipes", "life");
  e.strikeEffect = p.str("strike-effect", "flash");
  e.strikeSprite = p.str("strike-sprite");
  e.flashTime = p.num("screen-flash", 0);
  e.stormTime = p.num("storm-rain", 0);
  e.hopPeriod = std::max(p.num("hop-period", 1.2), 0.1);
  e.hopHeight = p.num("hop-height", 1.5);
  e.phase = rng.range(0, 6.28);
  e.grounded = p.str("habitat", "medium") == "ground";
  e.destructible = p.str("destructible", "yes") != "no";
  e.backing = std::clamp(p.num("backing", 0.0), 0.0, 1.0);
  if (p.has("ttl")) {
    const auto r = p.range("ttl", {0, 0});
    e.ttl = rng.range(r.first, r.second);
  }
  if (p.str("random-frame") == "yes" && e.sprite->frameCount() > 1) {
    e.frame = rng.rangeInt(0, static_cast<int>(e.sprite->frameCount()) - 1);
  }
  const std::string pool = p.str("pool", "any");
  for (auto& c : e.pal) c = pickFromPool(pool, rng);
  for (const auto& b : p.list("behaviors")) {
    if (BehaviorFn fn = findBehavior(b)) e.behaviors.push_back(fn);
  }
}

}  // namespace

// ------------------------------------------------------------------ World ---

World::World(const AssetPack& pack, std::shared_ptr<const SceneDef> scene, int width, int height,
             uint64_t seed)
    : pack_(&pack),
      scene_(std::move(scene)),
      w_(std::max(width, 1)),
      h_(std::max(height, 1)),
      seed_(seed),
      rng_(seed) {
  const SceneDef& sc = *scene_;
  const int groundRows = std::clamp(sc.groundRows, 1, std::max(h_ / 4, 1));
  seabedY_ = std::max(h_ - groundRows, 1);
  // The river takes at most half of the ground, so there is always some bank left.
  riverRows_ = std::clamp(sc.riverRows, 0, groundRows / 2);
  riverTopY_ = h_ - riverRows_;
  if (!sc.hasMedium || sc.skyFraction >= 1.0) {
    surfaceY_ = seabedY_;
  } else {
    surfaceY_ = std::clamp(static_cast<int>(h_ * sc.skyFraction), sc.skyRowsMin, sc.skyRowsMax);
    if (surfaceY_ >= seabedY_) surfaceY_ = std::max(seabedY_ - 1, 0);
  }
  hasMedium_ = sc.hasMedium && surfaceY_ < seabedY_;
  eventTimer_ = rng_.range(sc.eventFirst.first, sc.eventFirst.second);
  rareTimer_ = sc.rare.empty() ? -1.0 : rng_.range(sc.rareFirst.first, sc.rareFirst.second);
  crater_.assign(static_cast<size_t>(w_), 0.0);
  phase_ = std::fmod(sc.startHour / 24.0, 1.0);
  populate();
}

double World::areaFor(const std::string& habitat) const {
  if (habitat == "sky") return static_cast<double>(w_) * surfaceY_;
  if (habitat == "ground") return static_cast<double>(w_) * std::max(riverTopY_ - seabedY_, 1);
  if (habitat == "river") return static_cast<double>(w_) * riverRows_;
  if (habitat == "surface") return w_;
  return static_cast<double>(w_) * std::max(seabedY_ - surfaceY_ - 1, 0);  // medium
}

void World::populate() {
  for (const auto& pop : scene_->population) {
    const bool perCells = pop.kind == SceneDef::Population::Kind::PerCells;
    const bool perCols = pop.kind == SceneDef::Population::Kind::PerCols;
    double want;
    if (pop.sprite == "@seaweed") {
      if (!hasMedium_) continue;
      want = perCols ? w_ / pop.n : pop.n;
      int n = static_cast<int>(want);
      if (perCols && rng_.chance(want - n)) ++n;
      for (int i = 0; i < n; ++i) spawnSeaweed();
      continue;
    }
    auto sprite = pack_->find(pop.sprite);
    if (!sprite) continue;
    const std::string habitat = sprite->props().str("habitat", "medium");
    if ((habitat == "medium" && !hasMedium_) || (habitat == "river" && riverRows_ == 0)) continue;
    want = perCells ? areaFor(habitat) / pop.n : perCols ? w_ / pop.n : pop.n;
    int n = static_cast<int>(want);
    if (pop.kind != SceneDef::Population::Kind::Count && rng_.chance(want - n)) ++n;
    // A `herd` sprite is spawned in groups (n counts groups, not animals).
    const bool herded = sprite->props().has("herd");
    for (int i = 0; i < n; ++i) {
      if (herded) spawnHerd(sprite);
      else spawnCreature(sprite, true);
    }
  }
  for (const auto& d : scene_->decor) spawnDecor(d);
  flushIncoming();
}

double World::habitatY(const Props& p, int spriteHeight) {
  if (p.has("row")) {  // pinned to a row; negative counts from the bottom
    const double row = p.num("row", 0);
    return row >= 0 ? row : h_ + row;
  }
  const std::string habitat = p.str("habitat", "medium");
  if (habitat == "ground") return seabedY_ - spriteHeight;
  if (habitat == "river") return rng_.rangeInt(riverTopY_, std::max(h_ - spriteHeight, riverTopY_));
  if (habitat == "surface") return surfaceY_ - p.num("anchor-row", spriteHeight - 1);
  if (habitat == "sky") {
    if (p.has("altitude")) {  // hovering within a few rows above the ground
      return std::max(surfaceY_ - spriteHeight - rng_.rangeInt(0, static_cast<int>(p.num("altitude", 0))), 0);
    }
    return rng_.rangeInt(0, std::max(surfaceY_ - spriteHeight, 0));
  }
  const int lo = surfaceY_ + 2;
  return rng_.rangeInt(lo, std::max(seabedY_ - spriteHeight, lo));
}

void World::spawnHerd(const std::shared_ptr<const Sprite>& sprite) {
  const Props& p = sprite->props();
  const auto [lo, hi] = p.range("herd", {2, 4});
  const int members = rng_.rangeInt(static_cast<int>(lo), static_cast<int>(hi));
  const double spacing = p.num("herd-spacing", 4.0);
  const double leader = rng_.range(-sprite->width(), w_);
  Placement where;
  where.dir = rng_.chance(0.5) ? 1 : -1;
  where.hasX = true;
  for (int i = 0; i < members; ++i) {
    where.x = leader - where.dir * i * spacing + rng_.range(-1.5, 1.5);  // strung out behind the leader
    where.speedMul = rng_.range(0.92, 1.08);
    spawnCreature(sprite, true, false, &where);
  }
}

void World::spawnCreature(const std::shared_ptr<const Sprite>& sprite, bool initial, bool event,
                          const Placement* where) {
  const Props& p = sprite->props();
  auto e = std::make_unique<Entity>();
  e->sprite = sprite;
  applyProps(*e, p, rng_);
  e->isEvent = event;

  const auto [vmin, vmax] = p.range("speed", {4, 8});
  const double speed = rng_.range(vmin, vmax) * (where ? where->speedMul : 1.0);
  const bool coin = rng_.chance(0.5);
  const bool right = (where && where->dir != 0) ? where->dir > 0 : coin;
  e->vx = right ? speed : -speed;
  e->vy = std::fabs(e->vx) * p.num("slope", 0);  // diagonal flyers move downwards
  e->mirrored = right != e->facesRight;

  const auto [zmin, zmax] = p.range("depth", {5, 15});
  e->z = rng_.rangeInt(static_cast<int>(zmin), static_cast<int>(zmax));
  e->y = e->anchorY = habitatY(p, e->height());

  // Ground sprites with `lanes: N` stand N rows deep on the bank; lanes further
  // forward are drawn in front, so crowds of trees and animals get some depth.
  const int lanes = static_cast<int>(p.num("lanes", 1));
  if (lanes > 1 && p.str("habitat", "medium") == "ground") {
    const int bank = (riverRows_ > 0 ? riverTopY_ : h_) - seabedY_;
    const int usable = std::min(lanes, std::max(bank, 1));
    const int lane = rng_.rangeInt(0, usable - 1);
    e->y += lane;
    e->anchorY = e->y;
    if (usable > 1) e->z = static_cast<int>(zmax) - lane * static_cast<int>(zmax - zmin) / (usable - 1);
  }

  // Members of a herd that left come back from the same side, a little apart, still facing the same way.
  const double rejoin = (where && !initial) ? rng_.range(0, 12) : 0.0;
  e->x = (where && where->hasX) ? where->x
         : initial              ? rng_.range(-e->width(), w_)
                                : (right ? -e->width() - rejoin : w_ + rejoin);
  if (e->vy > 0 && !initial) e->y = e->anchorY = -e->height() + rng_.range(0, h_ / 3.0);
  if (sprite->frameCount() > 1 && e->fps > 0) {
    e->frame = rng_.rangeInt(0, static_cast<int>(sprite->frameCount()) - 1);
  }

  // `sleeps: 0.7` = about 70% of these animals sleep at night, each from its own sun height.
  const double sleeps = std::clamp(p.num("sleeps", 0.0), 0.0, 1.0);
  if (sleeps > 0 && rng_.chance(sleeps)) e->sleepBelow = rng_.range(0.05, 0.9);

  if (!event) {
    const bool herded = p.has("herd");
    const int dir = right ? 1 : -1;
    e->onDeath = [sprite, herded, dir](World& w) {
      Placement again;
      again.dir = dir;
      if (herded) w.spawnCreature(sprite, false, false, &again);
      else w.spawnCreature(sprite, false);
    };
  }
  spawn(std::move(e));
}

void World::spawnDecor(const SceneDef::Decor& d) {
  auto sprite = pack_->find(d.sprite);
  if (!sprite) return;
  const Props& p = sprite->props();
  auto e = std::make_unique<Entity>();
  e->sprite = sprite;
  applyProps(*e, p, rng_);
  e->z = static_cast<int>(p.range("depth", {20, 20}).first);

  if (d.align == "left") e->x = d.margin;
  else if (d.align == "right") e->x = w_ - e->width() - d.margin;
  else if (d.align == "center") e->x = (w_ - e->width()) / 2 + d.margin;
  else e->x = rng_.rangeInt(0, std::max(w_ - e->width(), 0));

  if (d.hasY) e->y = d.y >= 0 ? d.y : h_ + d.y;
  else if (p.str("habitat", "ground") == "sky") e->y = 1;
  else e->y = habitatY(p, e->height());
  e->anchorY = e->y;
  e->restock = [d](World& w) { w.spawnDecor(d); };  // planted again after a catastrophe
  spawn(std::move(e));
}

Entity* World::spawnEffect(const std::string& spriteName, double x, double y, int z, double vx,
                           double vy) {
  auto sprite = pack_->find(spriteName);
  if (!sprite) return nullptr;
  auto e = std::make_unique<Entity>();
  e->sprite = sprite;
  applyProps(*e, sprite->props(), rng_);
  e->x = x;
  e->y = y;
  e->z = z;
  e->vx = vx;
  e->vy = vy;
  if (vx != 0) e->mirrored = (vx > 0) != e->facesRight;
  Entity* raw = e.get();
  spawn(std::move(e));
  return raw;
}

void World::spawnSeaweed() {
  const int maxH = std::max(seabedY_ - surfaceY_ - 2, 2);
  const int h = std::min(rng_.rangeInt(3, 7), maxH);
  constexpr int kFrames = 6;
  constexpr int kWidth = 5;
  const double phase0 = rng_.range(0, 6.28);

  std::vector<SpriteFrame> frames;
  for (int k = 0; k < kFrames; ++k) {
    std::vector<int> col(h);
    for (int i = 0; i < h; ++i) {  // i = 0 is the tip
      const double amp = 1.6 * (h - 1 - i) / h;
      col[i] = 2 + static_cast<int>(std::lround(amp * std::sin(phase0 + k * 1.047 + i * 0.8)));
    }
    SpriteFrame f;
    for (int i = 0; i < h; ++i) {
      std::u32string row(kWidth, U' ');
      const int below = i + 1 < h ? col[i + 1] : col[i];
      const int d = col[i] - below;
      row[col[i]] = d > 0 ? U'/' : d < 0 ? U'\\' : (i % 2 ? U'(' : U')');
      f.art.push_back(std::move(row));
    }
    frames.push_back(std::move(f));
  }

  auto e = std::make_unique<Entity>();
  e->sprite = Sprite::fromFrames("seaweed", std::move(frames));
  e->x = rng_.rangeInt(0, std::max(w_ - kWidth, 0));
  e->y = seabedY_ - h;
  e->z = 21;
  e->fps = rng_.range(2.0, 3.0);
  e->frame = rng_.rangeInt(0, kFrames - 1);
  e->fg = lerp({40, 150, 70}, {150, 170, 60}, rng_.uniform());
  e->ttl = rng_.range(8 * 60, 12 * 60);  // seaweed regrows every 8-12 minutes
  e->onDeath = [](World& w) { w.spawnSeaweed(); };
  spawn(std::move(e));
}

void World::spawnEvent() {
  double total = 0;
  for (const auto& ev : scene_->events) total += ev.weight;
  if (total <= 0) return;
  double pick = rng_.range(0, total);
  for (const auto& ev : scene_->events) {
    pick -= ev.weight;
    if (pick > 0) continue;
    if (auto sprite = pack_->find(ev.sprite)) spawnCreature(sprite, false, true);
    return;
  }
}

void World::spawnRare() {
  double total = 0;
  for (const auto& ev : scene_->rare) total += ev.weight;
  if (total <= 0) return;
  double pick = rng_.range(0, total);
  for (const auto& ev : scene_->rare) {
    pick -= ev.weight;
    if (pick > 0) continue;
    if (auto sprite = pack_->find(ev.sprite)) {
      spawnCreature(sprite, false, true);
      if (!incoming_.empty()) incoming_.back()->isRare = true;
    }
    return;
  }
}

bool World::triggerRare() {
  if (scene_->rare.empty()) return false;
  for (const auto& e : entities_) {
    if (e->isRare && !e->dead) return false;
  }
  spawnRare();
  flushIncoming();
  return true;
}

void World::wipeOut(const std::string& wipes, const std::string& effect, const Entity* except) {
  const bool all = wipes == "life";
  const std::vector<std::string> names = all ? std::vector<std::string>{} : splitWs(wipes);
  auto take = [&](Entity& o) {
    if (&o == except || o.isRare) return;
    const bool victim = all ? o.life : std::find(names.begin(), names.end(), o.sprite->name()) != names.end();
    if (!victim) return;
    if (o.dead) {  // it just left the scene and would be replaced right away: make that replacement wait
      if (o.onDeath) restock_.push_back({rng_.range(8.0, 45.0), std::move(o.onDeath)});
      o.onDeath = nullptr;
      return;
    }
    if (!effect.empty() && !isAsleep(o)) {  // a sleeper is gone without a sight
      spawnEffect(effect, o.x + o.width() / 2.0 - 1, o.y + o.height() / 2.0 - 1, o.z - 1, 0, 0);
    }
    std::function<void(World&)> comeBack = o.restock ? o.restock : o.onDeath;
    if (comeBack) restock_.push_back({rng_.range(8.0, 45.0), std::move(comeBack)});
    o.onDeath = nullptr;
    o.restock = nullptr;
    o.dead = true;
    ++killed_;
  };
  for (auto& other : entities_) take(*other);
  // Entities spawned during this very step have not joined the world yet, but are just as dead.
  // (spawnEffect above adds to the same list, and effects are never victims.)
  for (size_t i = 0; i < incoming_.size(); ++i) take(*incoming_[i]);
}

void World::flash(double seconds) {
  flashLen_ = seconds;
  flashElapsed_ = 0;
  ++flashCount_;
}

bool World::flashing() const {
  if (flashElapsed_ >= flashLen_) return false;
  return flashElapsed_ < 0.12 || (flashElapsed_ > 0.22 && flashElapsed_ < 0.34);  // two quick flashes
}

void World::startRain(double seconds) {
  if (scene_->action.kind != SceneDef::Action::Kind::Weather || !pack_->find(scene_->action.sprite)) return;
  rainLeft_ = std::max(rainLeft_, seconds);
}

int World::interact() {
  const SceneDef::Action& a = scene_->action;
  if (a.kind == SceneDef::Action::Kind::Drop) {
    if (!hasMedium_ || !pack_->find(a.sprite)) return 0;
    const int room = 30 - static_cast<int>(countTag("food"));
    const int n = std::min(8, room);
    const int center = rng_.rangeInt(1, std::max(w_ - 2, 1));
    for (int i = 0; i < n; ++i) {
      spawnEffect(a.sprite, std::clamp(center + rng_.rangeInt(-7, 7), 1, std::max(w_ - 2, 1)), surfaceY_ + 1, 2, 0, 1.8);
    }
    flushIncoming();
    return std::max(n, 0);
  }
  if (a.kind == SceneDef::Action::Kind::Impact) {
    auto sprite = pack_->find(a.sprite);
    if (!sprite || countTag("meteorite") > 0) return 0;  // one at a time
    const int hw = craterHalfWidth();
    const double target = rng_.range(hw + 2, std::max(w_ - hw - 3, hw + 3));
    const double vy = 24, vx = (rng_.chance(0.5) ? 1 : -1) * 0.8 * vy;
    const double flight = (seabedY_ + sprite->height()) / vy;
    const bool right = vx > 0;
    const double headOffset = right ? sprite->width() - 1 : 0;  // x of the head inside the sprite
    spawnEffect(a.sprite, target - headOffset - vx * flight, -sprite->height(), 1, vx, vy);
    flushIncoming();
    return 1;
  }
  if (a.kind == SceneDef::Action::Kind::Weather) {
    if (!pack_->find(a.sprite)) return 0;
    stormDir_ = rng_.chance(0.5) ? 1 : -1;
    auto funnel = a.extra.empty() ? nullptr : pack_->find(a.extra);
    if (funnel && countTag("tornado") == 0 && rng_.chance(0.5)) {  // a tornado instead of the plain storm
      const auto [vmin, vmax] = funnel->props().range("speed", {3, 5});
      const double speed = rng_.range(vmin, vmax);
      spawnEffect(a.extra, stormDir_ > 0 ? -funnel->width() : w_, seabedY_ - funnel->height(), 6,
                  stormDir_ * speed, 0);
      flushIncoming();
      return 1;
    }
    rainLeft_ = scene_->weatherDuration;  // pressing again while it lasts keeps it going
    return 1;
  }
  return 0;
}

void World::setPhase(double phase) {
  phase_ = std::fmod(std::fmod(phase, 1.0) + 1.0, 1.0);
}

void World::setTimeMode(TimeMode mode) {
  if (!scene_->dayNight) return;
  timeMode_ = mode;
  switch (mode) {
    case TimeMode::Dawn: phase_ = 0.27; break;
    case TimeMode::Day: phase_ = 0.5; break;
    case TimeMode::Dusk: phase_ = 0.73; break;
    case TimeMode::Night: phase_ = 0.0; break;
    case TimeMode::Cycle: break;  // carry on from the current time
  }
}

bool World::isAsleep(const Entity& e) const {
  return e.sleepBelow > 0 && scene_->dayNight && light().sun < e.sleepBelow;
}

Light World::light() const {
  Light l;
  l.haze = scene_->overcastTintAmount * overcast_;
  l.hazeColor = scene_->overcastTint;
  if (!scene_->dayNight) return l;
  constexpr double kTwoPi = 6.283185307179586;
  const double height = -std::cos(kTwoPi * phase_);  // sun height: -1 at midnight, +1 at noon
  const double t = std::clamp((height + 0.12) / 0.47, 0.0, 1.0);
  l.sun = t * t * (3 - 2 * t);
  l.day = l.sun * (1 - 0.5 * overcast_);
  l.glow = std::exp(-(height / 0.22) * (height / 0.22)) * (1 - 0.6 * overcast_);
  l.stars = 1 - l.sun;
  return l;
}

std::string World::clockText() const {
  const int minutes = static_cast<int>(std::lround(phase_ * 1440.0)) % 1440;  // rounds away float noise
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
  return buf;
}

void World::spawnWeather(double dt) {
  const std::string& particle = scene_->action.sprite;
  auto sprite = pack_->find(particle);
  if (!sprite) return;
  const bool wind = sprite->props().str("wind", "no") == "yes";  // sand blown across, not falling
  const double want = (wind ? 0.012 * w_ * h_ : 0.45 * w_) * dt;
  int n = static_cast<int>(want);
  if (rng_.chance(want - n)) ++n;
  for (int i = 0; i < n; ++i) {
    if (wind) {  // grains enter at the upwind edge and sweep across the whole scene
      const auto [vmin, vmax] = sprite->props().range("speed", {22, 38});
      spawnEffect(particle, stormDir_ > 0 ? -1 : w_, rng_.range(0, h_), 1, stormDir_ * rng_.range(vmin, vmax),
                  rng_.range(-1.5, 1.5));
      continue;
    }
    Entity* e = spawnEffect(particle, rng_.range(-4, w_), -1, 1, 3.0, 14 + rng_.range(-2, 3));
    // Drops land at random depths on the ground, so the rain reaches the bank and the river.
    if (e) e->anchorY = rng_.rangeInt(std::min(seabedY_, h_ - 1), h_ - 1);
  }
}

int World::craterHalfWidth() const { return std::clamp(w_ / 6, 2, 7); }

int World::craterRows(int x) const {
  if (x < 0 || x >= w_) return 0;
  return static_cast<int>(std::floor(crater_[static_cast<size_t>(x)] + 0.5));
}

double World::deepestCrater() const {
  double d = 0;
  for (double c : crater_) d = std::max(d, c);
  return d;
}

void World::impactAt(double cx) {
  const int c = static_cast<int>(std::lround(cx));
  const int hw = craterHalfWidth();
  const int bank = (riverRows_ > 0 ? riverTopY_ : h_) - seabedY_;
  const int maxDepth = std::clamp(bank - 1, 1, 3);  // always leave some ground

  for (int dx = -hw; dx <= hw; ++dx) {
    const int x = c + dx;
    if (x < 0 || x >= w_) continue;
    const double t = static_cast<double>(dx) / (hw + 1);
    crater_[static_cast<size_t>(x)] = std::max(crater_[static_cast<size_t>(x)], maxDepth * (1 - t * t));
  }

  // Wreck whatever stands in the blast, leaving debris behind.
  for (auto& e : entities_) {
    if (e->dead || !e->grounded || !e->destructible) continue;
    if (e->x + e->width() < c - hw || e->x > c + hw) continue;
    e->dead = true;
    ++destroyed_;
    const int pieces = std::clamp(e->width() * e->height() / 5, 4, 14);
    for (int i = 0; i < pieces; ++i) {
      spawnEffect("debris", e->x + rng_.range(0, e->width()), e->y + rng_.range(0, e->height()), 1,
                  rng_.range(-5, 5), -rng_.range(1.5, 5));
    }
  }

  // The strike itself: a flash, thrown rock and a cloud of dust that drifts back down.
  if (auto flash = pack_->find("flash")) {
    spawnEffect("flash", c - flash->width() / 2.0, seabedY_ - flash->height(), 1, 0, 0);
  }
  for (int i = 0; i < 14; ++i) {
    spawnEffect("debris", c + rng_.range(-1, 1), seabedY_ - 1, 1, rng_.range(-6, 6), -rng_.range(2, 5.5));
  }
  for (int i = 0; i < 18; ++i) {
    spawnEffect("dust", c + rng_.range(-hw, hw), seabedY_ - 1, 1, rng_.range(-3, 3), -rng_.range(0.5, 2.5));
  }
}

size_t World::countTag(const std::string& tag) const {
  size_t n = 0;
  for (const auto& e : entities_) n += (!e->dead && e->tag == tag);
  return n;
}

double World::stormProximity() const {
  double best = 0;
  for (const auto& e : entities_) {
    if (e->dead || e->tag != "gloom") continue;
    const double cx = e->x + e->width() / 2.0;
    const double reach = w_ / 2.0 + e->width() / 2.0 + w_ * 0.25;  // starts before it is in view
    best = std::max(best, std::clamp(1.0 - std::fabs(cx - w_ / 2.0) / reach, 0.0, 1.0));
  }
  return best;
}

Entity* World::nearest(const std::string& tag, double x, double y, double maxDist) {
  Entity* best = nullptr;
  double bestD = maxDist * maxDist;
  const double sun = scene_->dayNight ? light().sun : 1.0;
  for (auto& e : entities_) {
    if (e->dead || e->tag != tag) continue;
    if (e->sleepBelow > 0 && sun < e->sleepBelow) continue;  // a sleeping animal is out of sight
    const double cx = e->x + e->width() / 2.0 - x, cy = (e->y + e->height() / 2.0 - y) * 2.0;
    const double d = cx * cx + cy * cy;
    if (d < bestD) { bestD = d; best = e.get(); }
  }
  return best;
}

void World::flushIncoming() {
  for (auto& e : incoming_) entities_.push_back(std::move(e));
  incoming_.clear();
}

void World::step(double dt) {
  time_ += dt;
  for (double& c : crater_) c = std::max(0.0, c - dt / scene_->dustSettle);  // dust fills craters
  if (rainLeft_ > 0) {
    rainLeft_ = std::max(0.0, rainLeft_ - dt);
    spawnWeather(dt);
  }
  if (scene_->dayNight && timeMode_ == TimeMode::Cycle) phase_ = std::fmod(phase_ + dt / scene_->dayLength, 1.0);
  const double cloudTarget =
      (rainLeft_ > 0 || countTag("tornado") > 0 || countTag("gloom") > 0) ? 1.0 : 0.0;
  flashElapsed_ += dt;
  overcast_ += std::clamp(cloudTarget - overcast_, -0.25 * dt, 0.4 * dt);
  const double sun = scene_->dayNight ? light().sun : 1.0;
  bool eventAlive = false, rareAlive = false;
  for (auto& e : entities_) {
    if (e->dead) continue;
    eventAlive |= e->isEvent;
    rareAlive |= e->isRare;
    if (e->sleepBelow > 0 && sun < e->sleepBelow) continue;  // asleep: frozen where it is
    if (e->flung) {  // thrown by a tornado: flies on an arc, lands dazed and walks on afterwards
      e->age += dt;
      e->vy += scene_->gravity * dt;
      e->x += e->vx * dt;
      e->y += e->vy * dt;
      if (e->vy > 0 && e->y >= e->anchorY) {
        e->y = e->anchorY;
        e->vy = 0;
        e->vx = e->flungVx;
        e->flung = false;
        e->pauseLeft = 1.5;
      }
      continue;
    }
    e->age += dt;
    if (e->pauseLeft > 0) {  // busy (eating): stays frozen where it is
      e->pauseLeft = std::max(0.0, e->pauseLeft - dt);
      continue;
    }
    animate(*e, dt);
    for (BehaviorFn b : e->behaviors) b(*e, *this, dt);
    if (e->ttl > 0 && e->age > e->ttl) e->dead = true;
  }

  if (!scene_->events.empty() && !eventAlive) {
    if (eventTimer_ < 0) eventTimer_ = rng_.range(scene_->eventInterval.first, scene_->eventInterval.second);
    eventTimer_ -= dt;
    if (eventTimer_ <= 0) {
      spawnEvent();
      eventTimer_ = -1;
    }
  }

  if (!scene_->rare.empty() && !rareAlive) {
    if (rareTimer_ < 0) rareTimer_ = rng_.range(scene_->rareInterval.first, scene_->rareInterval.second);
    rareTimer_ -= dt;
    if (rareTimer_ <= 0) {
      spawnRare();
      rareTimer_ = -1;
    }
  }

  std::vector<std::function<void(World&)>> callbacks;
  for (auto& r : restock_) r.left -= dt;  // what a catastrophe wiped out comes back, one by one
  for (size_t i = restock_.size(); i-- > 0;) {
    if (restock_[i].left > 0) continue;
    callbacks.push_back(std::move(restock_[i].fn));
    restock_.erase(restock_.begin() + static_cast<std::ptrdiff_t>(i));
  }
  for (auto& e : entities_) {
    if (e->dead && e->onDeath) callbacks.push_back(std::move(e->onDeath));
  }
  std::erase_if(entities_, [](const auto& e) { return e->dead; });
  for (auto& cb : callbacks) cb(*this);
  flushIncoming();
}

// ---------------------------------------------------------------- render ---

void World::renderBackground(Frame& out, Style style, const Light& light) const {
  const SceneDef& sc = *scene_;
  // The scene's colors are its daytime look; the light turns them into the current look.
  const Rgb skyTop = lerp(backdrop(sc.skyTop, light), Rgb{110, 70, 130}, light.glow * 0.3);
  const Rgb skyLow = lerp(backdrop(sc.skyLow, light), Rgb{255, 150, 80}, light.glow * 0.55);
  const Rgb mediumTop = backdrop(sc.mediumTop, light), mediumDeep = backdrop(sc.mediumDeep, light);
  const Rgb groundA = backdrop(sc.groundA, light), groundB = backdrop(sc.groundB, light);
  const Rgb surfaceFg = backdrop(sc.surfaceFg, light);
  const Rgb riverTop = backdrop(sc.riverTop, light), riverDeep = backdrop(sc.riverDeep, light);

  for (int y = 0; y < h_; ++y) {
    for (int x = 0; x < w_; ++x) {
      Cell c;
      const uint32_t h = hash2(x, y);
      if (y < surfaceY_) {
        c.bg = lerp(skyTop, skyLow, surfaceY_ > 1 ? double(y) / (surfaceY_ - 1) : 1.0);
        if (sc.starDensity > 0 && light.stars > 0.2 && h % static_cast<uint32_t>(sc.starDensity) == 0) {
          const bool bright = std::sin(time_ * 2.0 + (h >> 8)) > 0;  // twinkling stars
          c.ch = bright ? U'*' : U'.';
          c.fg = lerp(c.bg, bright ? Rgb{250, 250, 220} : Rgb{150, 150, 170}, light.stars);
        }
      } else if (riverRows_ > 0 && y >= riverTopY_) {  // river: flowing ripples
        const double t = riverRows_ > 1 ? double(y - riverTopY_) / (riverRows_ - 1) : 0.0;
        c.bg = lerp(riverTop, riverDeep, t);
        const double flow = time_ * sc.riverFlow;
        const double v = std::sin(x * 0.6 - flow + y * 1.7) + 0.5 * std::sin(x * 0.23 - flow * 0.6);
        c.ch = v > 0.8 ? U'~' : v > 0.2 ? U'-' : (h % 9 == 0 ? U'.' : U' ');
        c.fg = scale(c.bg, 1.9);
      } else if (y >= seabedY_) {
        const int row = y - seabedY_;
        const int dug = craterRows(x);
        c.bg = (y == seabedY_) ? groundA : groundB;
        const auto& g = sc.groundGlyphs;
        c.ch = g.empty() ? U' ' : g[h % g.size()];
        c.fg = scale(c.bg, 1.35);
        if (row < dug) {  // dug out: open sky with the crater walls drawn as slopes
          c.bg = skyLow;
          c.ch = craterRows(x - 1) <= row ? U'\\' : craterRows(x + 1) <= row ? U'/' : U' ';
          c.fg = scale(groundA, 0.9);
        } else if (row == dug && dug > 0) {
          c.bg = groundB;  // freshly exposed, darker floor
        }
      } else {
        const double t = double(y - surfaceY_) / std::max(seabedY_ - surfaceY_, 1);
        c.bg = lerp(mediumTop, mediumDeep, std::min(t * 1.2, 1.0));
        if (y == surfaceY_ + 1 && hash2(x + static_cast<int>(time_ * 3), y) % 5 == 0) {
          c.ch = U'.';
          c.fg = scale(surfaceFg, 0.7);
        }
      }
      if (hasMedium_ && y == surfaceY_ && sc.surfaceGlyphs.size() == 3) {  // animated waterline
        const double v = std::sin(x * 0.45 + time_ * 1.6) + 0.5 * std::sin(x * 0.17 - time_ * 0.7);
        c.ch = v > 0.5 ? sc.surfaceGlyphs[0] : v > -0.5 ? sc.surfaceGlyphs[1] : sc.surfaceGlyphs[2];
        c.fg = surfaceFg;
        c.bg = mediumTop;
      }
      if (style != Style::Full) c.defaultBg = true;  // no backdrop in the terminal styles
      if (style == Style::Mono) c.defaultFg = true;
      out.at(x, y) = c;
    }
  }
}

void World::renderCelestial(Frame& out, Style style, const Light& light) const {
  if (!scene_->dayNight || surfaceY_ <= 0 || overcast_ > 0.6) return;
  constexpr double kPi = 3.14159265358979;
  const bool byDay = phase_ >= 0.25 && phase_ <= 0.75;
  // How far along its arc the body is: 0 as it rises, 1 as it sets.
  const double u = byDay ? (phase_ - 0.25) / 0.5 : std::fmod(phase_ + 0.25, 1.0) / 0.5;
  auto sprite = pack_->find(byDay ? scene_->sunSprite : scene_->moonSprite);
  if (!sprite) return;
  Entity body;
  body.sprite = sprite;
  body.glow = true;
  body.fg = sprite->props().color("color", {255, 240, 180});
  body.x = u * (w_ - 1) - sprite->width() / 2.0;
  // At the horizon the whole body is just below it; it climbs to the top of the sky at mid-arc.
  body.y = surfaceY_ * (1 - std::sin(kPi * u));
  blit(out, body, style, light, surfaceY_);  // rows below the horizon stay hidden
}

void World::render(Frame& out, Style style) const {
  out.resize(w_, h_);
  const Light l = light();
  renderBackground(out, style, l);
  renderCelestial(out, style, l);

  std::vector<const Entity*> order;
  order.reserve(entities_.size());
  for (const auto& e : entities_) {
    if (visibleNow(*e, l, scene_->dayNight)) order.push_back(e.get());
  }
  std::stable_sort(order.begin(), order.end(),
                   [](const Entity* a, const Entity* b) { return a->z > b->z; });
  for (const Entity* e : order) blit(out, *e, style, l, h_);
  if (flashing()) {  // lightning: everything lights up, or flips its colors in the terminal styles
    for (int y = 0; y < h_; ++y) {
      for (int x = 0; x < w_; ++x) {
        Cell& c = out.at(x, y);
        if (style == Style::Full) {
          c.fg = lerp(c.fg, Rgb{255, 255, 255}, 0.75);
          c.bg = lerp(c.bg, Rgb{255, 255, 255}, 0.75);
        } else {
          c.reverse = !c.reverse;
        }
      }
    }
  }
}

}  // namespace dio
