/// @file core/world.cpp
/// @brief Simulation, behaviors and scene rendering.

#include "core/world.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace dio {

namespace {

// ---------------------------------------------------------------- colors ---

/// Color pool for warm-colored creatures.
constexpr Rgb kWarm[] = {{255, 140, 40}, {255, 200, 60}, {240, 90, 70},
                         {255, 130, 170}, {250, 230, 120}};
/// Color pool for cool-colored creatures.
constexpr Rgb kCool[] = {{70, 200, 230}, {90, 140, 250}, {80, 220, 170},
                         {170, 120, 240}, {120, 230, 120}};

/// Random color from the named pool ("warm", "cool" or any other name for both).
Rgb pickFromPool(const std::string& pool, Rng& rng) {
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
    default: return e.fg;
  }
  return (m >= U'a' && m <= U'z') ? scale(base, 0.6) : base;
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
    if (other->dead || other->tag != "prey") continue;
    const bool inside = mx >= other->x - 1 && mx <= other->x + other->width() &&
                        my >= other->y - 0.5 && my <= other->y + other->height();
    if (!inside) continue;
    other->dead = true;
    w.noteEaten();
    w.spawnEffect("splat", mx - 1, my - 1, e.z - 1, 0, 0);
  }
}

/// Behavior function for a name used in a sprite header, or nullptr.
BehaviorFn findBehavior(const std::string& name) {
  static const std::map<std::string, BehaviorFn> table = {
      {"swim", swim},   {"fly", fly},           {"bob", bob},       {"rise", rise},
      {"sink", sink},   {"emit_bubbles", emitBubbles}, {"seek_food", seekFood}, {"hunt", hunt},
      {"walk", walk},   {"ballistic", ballistic},      {"meteorite", meteorite}};
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
  e.phase = rng.range(0, 6.28);
  e.grounded = p.str("habitat", "medium") == "ground";
  e.destructible = p.str("destructible", "yes") != "no";
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
  const int groundRows = std::clamp(sc.groundRows, 1, std::max(h_ / 6, 1));
  seabedY_ = std::max(h_ - groundRows, 1);
  if (!sc.hasMedium || sc.skyFraction >= 1.0) {
    surfaceY_ = seabedY_;
  } else {
    surfaceY_ = std::clamp(static_cast<int>(h_ * sc.skyFraction), sc.skyRowsMin, sc.skyRowsMax);
    if (surfaceY_ >= seabedY_) surfaceY_ = std::max(seabedY_ - 1, 0);
  }
  hasMedium_ = sc.hasMedium && surfaceY_ < seabedY_;
  eventTimer_ = rng_.range(sc.eventFirst.first, sc.eventFirst.second);
  crater_.assign(static_cast<size_t>(w_), 0.0);
  populate();
}

double World::areaFor(const std::string& habitat) const {
  if (habitat == "sky") return static_cast<double>(w_) * surfaceY_;
  if (habitat == "ground") return static_cast<double>(w_) * std::max(h_ - seabedY_, 1);
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
    if (habitat == "medium" && !hasMedium_) continue;
    want = perCells ? areaFor(habitat) / pop.n : perCols ? w_ / pop.n : pop.n;
    int n = static_cast<int>(want);
    if (pop.kind != SceneDef::Population::Kind::Count && rng_.chance(want - n)) ++n;
    for (int i = 0; i < n; ++i) spawnCreature(sprite, true);
  }
  for (const auto& d : scene_->decor) spawnDecor(d);
  flushIncoming();
}

double World::habitatY(const Props& p, int spriteHeight) {
  const std::string habitat = p.str("habitat", "medium");
  if (habitat == "ground") return seabedY_ - spriteHeight;
  if (habitat == "surface") return surfaceY_ - p.num("anchor-row", spriteHeight - 1);
  if (habitat == "sky") return rng_.rangeInt(0, std::max(surfaceY_ - spriteHeight, 0));
  const int lo = surfaceY_ + 2;
  return rng_.rangeInt(lo, std::max(seabedY_ - spriteHeight, lo));
}

void World::spawnCreature(const std::shared_ptr<const Sprite>& sprite, bool initial, bool event) {
  const Props& p = sprite->props();
  auto e = std::make_unique<Entity>();
  e->sprite = sprite;
  applyProps(*e, p, rng_);
  e->isEvent = event;

  const auto [vmin, vmax] = p.range("speed", {4, 8});
  const double speed = rng_.range(vmin, vmax);
  const bool right = rng_.chance(0.5);
  e->vx = right ? speed : -speed;
  e->vy = std::fabs(e->vx) * p.num("slope", 0);  // diagonal flyers move downwards
  e->mirrored = right != e->facesRight;

  const auto [zmin, zmax] = p.range("depth", {5, 15});
  e->z = rng_.rangeInt(static_cast<int>(zmin), static_cast<int>(zmax));
  e->y = e->anchorY = habitatY(p, e->height());

  e->x = initial ? rng_.range(-e->width(), w_) : (right ? -e->width() : w_);
  if (e->vy > 0 && !initial) e->y = e->anchorY = -e->height() + rng_.range(0, h_ / 3.0);
  if (sprite->frameCount() > 1 && e->fps > 0) {
    e->frame = rng_.rangeInt(0, static_cast<int>(sprite->frameCount()) - 1);
  }

  if (!event) e->onDeath = [sprite](World& w) { w.spawnCreature(sprite, false); };
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
  spawn(std::move(e));
}

void World::spawnEffect(const std::string& spriteName, double x, double y, int z, double vx,
                        double vy) {
  auto sprite = pack_->find(spriteName);
  if (!sprite) return;
  auto e = std::make_unique<Entity>();
  e->sprite = sprite;
  applyProps(*e, sprite->props(), rng_);
  e->x = x;
  e->y = y;
  e->z = z;
  e->vx = vx;
  e->vy = vy;
  if (vx != 0) e->mirrored = (vx > 0) != e->facesRight;
  spawn(std::move(e));
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
  return 0;
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
  const int maxDepth = std::clamp(h_ - seabedY_ - 1, 1, 3);  // always leave some ground

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

Entity* World::nearest(const std::string& tag, double x, double y, double maxDist) {
  Entity* best = nullptr;
  double bestD = maxDist * maxDist;
  for (auto& e : entities_) {
    if (e->dead || e->tag != tag) continue;
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
  bool eventAlive = false;
  for (auto& e : entities_) {
    if (e->dead) continue;
    eventAlive |= e->isEvent;
    e->age += dt;
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

  std::vector<std::function<void(World&)>> callbacks;
  for (auto& e : entities_) {
    if (e->dead && e->onDeath) callbacks.push_back(std::move(e->onDeath));
  }
  std::erase_if(entities_, [](const auto& e) { return e->dead; });
  for (auto& cb : callbacks) cb(*this);
  flushIncoming();
}

// ---------------------------------------------------------------- render ---

void World::renderBackground(Frame& out, Style style) const {
  const SceneDef& sc = *scene_;
  for (int y = 0; y < h_; ++y) {
    for (int x = 0; x < w_; ++x) {
      Cell c;
      const uint32_t h = hash2(x, y);
      if (y < surfaceY_) {
        c.bg = lerp(sc.skyTop, sc.skyLow, surfaceY_ > 1 ? double(y) / (surfaceY_ - 1) : 1.0);
        if (sc.starDensity > 0 && h % static_cast<uint32_t>(sc.starDensity) == 0) {  // twinkling stars
          const bool bright = std::sin(time_ * 2.0 + (h >> 8)) > 0;
          c.ch = bright ? U'*' : U'.';
          c.fg = bright ? Rgb{250, 250, 220} : Rgb{150, 150, 170};
        }
      } else if (y >= seabedY_) {
        const int row = y - seabedY_;
        const int dug = craterRows(x);
        c.bg = (y == seabedY_) ? sc.groundA : sc.groundB;
        const auto& g = sc.groundGlyphs;
        c.ch = g.empty() ? U' ' : g[h % g.size()];
        c.fg = scale(c.bg, 1.35);
        if (row < dug) {  // dug out: open sky with the crater walls drawn as slopes
          c.bg = sc.skyLow;
          c.ch = craterRows(x - 1) <= row ? U'\\' : craterRows(x + 1) <= row ? U'/' : U' ';
          c.fg = scale(sc.groundA, 0.9);
        } else if (row == dug && dug > 0) {
          c.bg = sc.groundB;  // freshly exposed, darker floor
        }
      } else {
        const double t = double(y - surfaceY_) / std::max(seabedY_ - surfaceY_, 1);
        c.bg = lerp(sc.mediumTop, sc.mediumDeep, std::min(t * 1.2, 1.0));
        if (y == surfaceY_ + 1 && hash2(x + static_cast<int>(time_ * 3), y) % 5 == 0) {
          c.ch = U'.';
          c.fg = scale(sc.surfaceFg, 0.7);
        }
      }
      if (hasMedium_ && y == surfaceY_ && sc.surfaceGlyphs.size() == 3) {  // animated waterline
        const double v = std::sin(x * 0.45 + time_ * 1.6) + 0.5 * std::sin(x * 0.17 - time_ * 0.7);
        c.ch = v > 0.5 ? sc.surfaceGlyphs[0] : v > -0.5 ? sc.surfaceGlyphs[1] : sc.surfaceGlyphs[2];
        c.fg = sc.surfaceFg;
        c.bg = sc.mediumTop;
      }
      if (style != Style::Full) c.defaultBg = true;  // no backdrop in the terminal styles
      if (style == Style::Mono) c.defaultFg = true;
      out.at(x, y) = c;
    }
  }
}

void World::render(Frame& out, Style style) const {
  out.resize(w_, h_);
  renderBackground(out, style);

  std::vector<const Entity*> order;
  order.reserve(entities_.size());
  for (const auto& e : entities_) order.push_back(e.get());
  std::stable_sort(order.begin(), order.end(),
                   [](const Entity* a, const Entity* b) { return a->z > b->z; });

  for (const Entity* e : order) {
    const SpriteFrame& f = e->sprite->frame(e->mirrored, e->frame);
    const int ox = static_cast<int>(std::floor(e->x));
    const int oy = static_cast<int>(std::floor(e->y));
    for (int r = 0; r < e->height(); ++r) {
      for (int c = 0; c < e->width(); ++c) {
        const int px = ox + c, py = oy + r;
        if (!out.inBounds(px, py)) continue;
        const char32_t ch = f.art[r][c], m = f.mask[r][c];
        Cell& cell = out.at(px, py);
        if (ch == U' ') {
          if (m == U' ') continue;
          cell.ch = U' ';
          if (m != U'_') {  // solid block: keeps its color unless the style is monochrome
            cell.bg = maskColor(m, *e);
            cell.defaultBg = style == Style::Mono;
          }
        } else {
          cell.ch = ch;
          cell.fg = maskColor(m, *e);
          cell.defaultFg = style == Style::Mono;
        }
      }
    }
  }
}

}  // namespace dio
