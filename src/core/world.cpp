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

/// Horizontal travel. The entity leaves the world when fully past the edge it heads for.
void swim(Entity& e, World& w, double dt) {
  e.x += e.vx * dt;
  if ((e.vx > 0 && e.x > w.width()) || (e.vx < 0 && e.x + e.width() < 0)) e.dead = true;
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

/// Now and then releases a bubble from the mouth.
void emitBubbles(Entity& e, World& w, double dt) {
  if (!w.rng().chance(e.bubbleRate * dt)) return;
  const bool movingRight = e.vx >= 0;
  const double bx = movingRight ? e.x + e.width() : e.x - 1;
  w.spawnEffect("bubble", bx, e.y + e.height() / 2, e.z - 1, 0, -3.5);
}

/// Behavior function for a name used in a sprite header, or nullptr.
BehaviorFn findBehavior(const std::string& name) {
  static const std::map<std::string, BehaviorFn> table = {
      {"swim", swim}, {"bob", bob}, {"rise", rise}, {"emit_bubbles", emitBubbles}};
  auto it = table.find(name);
  return it == table.end() ? nullptr : it->second;
}

/// Advances the animation clock and picks the current frame.
void animate(Entity& e, double dt) {
  const size_t n = e.sprite->frameCount();
  if (e.fps <= 0 || n < 2) return;
  e.clock += dt;
  const double period = 1.0 / e.fps;
  while (e.clock >= period) {
    e.clock -= period;
    ++e.frame;
  }
  e.frame = e.loop ? e.frame % n : std::min(e.frame, n - 1);
}

/// Gives every mask digit a random color from the pool.
void initPalette(Entity& e, const std::string& pool, Rng& rng) {
  for (auto& c : e.pal) c = pickFromPool(pool, rng);
}

}  // namespace

// ------------------------------------------------------------------ World ---

World::World(const AssetPack& pack, int width, int height, uint64_t seed)
    : pack_(&pack), w_(std::max(width, 1)), h_(std::max(height, 1)), seed_(seed), rng_(seed) {
  const int sandRows = h_ >= 12 ? 2 : 1;
  seabedY_ = std::max(h_ - sandRows, 1);
  surfaceY_ = std::clamp(h_ / 5, 1, 5);
  if (surfaceY_ >= seabedY_) surfaceY_ = std::max(seabedY_ - 1, 0);
  populate();
}

void World::populate() {
  const int waterRows = std::max(seabedY_ - surfaceY_ - 1, 0);
  const double cells = static_cast<double>(w_) * waterRows;

  for (int i = 0; i < w_ / 15; ++i) spawnSeaweed(true);

  for (const auto& [name, sprite] : pack_->sprites()) {
    const double per = sprite->props().num("spawn-per-cells", 0);
    if (per <= 0 || sprite->props().str("kind") == "effect") continue;
    const double want = cells / per;
    int n = static_cast<int>(want);
    if (rng_.chance(want - n)) ++n;
    for (int i = 0; i < n; ++i) spawnCreature(sprite, true);
  }
  flushIncoming();
}

void World::spawnCreature(const std::shared_ptr<const Sprite>& sprite, bool initial) {
  const Props& p = sprite->props();
  auto e = std::make_unique<Entity>();
  e->sprite = sprite;

  const auto [vmin, vmax] = p.range("speed", {4, 8});
  const double speed = rng_.range(vmin, vmax);
  const bool right = rng_.chance(0.5);
  e->vx = right ? speed : -speed;
  e->mirrored = right != (p.str("facing", "right") == "right");

  const auto [zmin, zmax] = p.range("depth", {5, 15});
  e->z = rng_.rangeInt(static_cast<int>(zmin), static_cast<int>(zmax));

  const int lo = surfaceY_ + 2;
  const int hi = std::max(seabedY_ - e->height(), lo);
  e->y = e->anchorY = rng_.rangeInt(lo, hi);
  e->phase = rng_.range(0, 6.28);

  if (initial) {
    e->x = rng_.range(-e->width(), w_);
  } else {
    e->x = right ? -e->width() : w_;
  }

  e->fps = p.num("fps", 0);
  e->loop = p.str("loop", "yes") != "no";
  e->frame = sprite->frameCount() > 1 ? rng_.rangeInt(0, static_cast<int>(sprite->frameCount()) - 1) : 0;
  e->bubbleRate = p.num("bubble-rate", 0.15);
  e->bobAmp = p.num("bob-amplitude", 1.5);
  e->bobFreq = p.num("bob-frequency", 1.0);
  initPalette(*e, p.str("pool", "any"), rng_);

  for (const auto& b : p.list("behaviors")) {
    if (BehaviorFn fn = findBehavior(b)) e->behaviors.push_back(fn);
  }
  e->onDeath = [sprite](World& w) { w.spawnCreature(sprite, false); };
  spawn(std::move(e));
}

void World::spawnEffect(const std::string& spriteName, double x, double y, int z, double vx,
                        double vy) {
  auto sprite = pack_->find(spriteName);
  if (!sprite) return;
  auto e = std::make_unique<Entity>();
  e->sprite = sprite;
  e->x = x;
  e->y = y;
  e->z = z;
  e->vx = vx;
  e->vy = vy;
  e->fps = sprite->props().num("fps", 0);
  e->loop = sprite->props().str("loop", "yes") != "no";
  e->fg = {160, 225, 240};
  for (const auto& b : sprite->props().list("behaviors")) {
    if (BehaviorFn fn = findBehavior(b)) e->behaviors.push_back(fn);
  }
  spawn(std::move(e));
}

void World::spawnSeaweed(bool initial) {
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
  e->onDeath = [](World& w) { w.spawnSeaweed(false); };
  (void)initial;
  spawn(std::move(e));
}

void World::flushIncoming() {
  for (auto& e : incoming_) entities_.push_back(std::move(e));
  incoming_.clear();
}

void World::step(double dt) {
  time_ += dt;
  for (auto& e : entities_) {
    if (e->dead) continue;
    e->age += dt;
    animate(*e, dt);
    for (BehaviorFn b : e->behaviors) b(*e, *this, dt);
    if (e->ttl > 0 && e->age > e->ttl) e->dead = true;
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

void World::renderBackground(Frame& out) const {
  const Rgb skyTop{6, 8, 28}, skyLow{34, 52, 98};
  const Rgb waterTop{30, 120, 150}, waterDeep{5, 22, 66};
  const Rgb sandA{150, 124, 82}, sandB{118, 96, 60};

  for (int y = 0; y < h_; ++y) {
    for (int x = 0; x < w_; ++x) {
      Cell c;
      if (y < surfaceY_) {
        c.bg = lerp(skyTop, skyLow, surfaceY_ > 1 ? double(y) / (surfaceY_ - 1) : 1.0);
        const uint32_t h = hash2(x, y);
        if (h % 23 == 0) {  // twinkling stars
          const bool bright = std::sin(time_ * 2.0 + (h >> 8)) > 0;
          c.ch = bright ? U'*' : U'.';
          c.fg = bright ? Rgb{250, 250, 220} : Rgb{150, 150, 170};
        }
      } else if (y >= seabedY_) {
        c.bg = (y == seabedY_) ? sandA : sandB;
        const uint32_t h = hash2(x, y);
        static const char32_t grains[] = {U'.', U',', U'\'', U'`', U'.', U' ', U' ', U':'};
        c.ch = grains[h % 8];
        c.fg = scale(c.bg, 1.35);
      } else {
        const double t = double(y - surfaceY_) / std::max(seabedY_ - surfaceY_, 1);
        c.bg = lerp(waterTop, waterDeep, std::min(t * 1.2, 1.0));
        if (y == surfaceY_ + 1 && hash2(x + static_cast<int>(time_ * 3), y) % 5 == 0) {
          c.ch = U'.';
          c.fg = {120, 200, 220};
        }
      }
      if (y == surfaceY_) {  // animated waterline
        const double v = std::sin(x * 0.45 + time_ * 1.6) + 0.5 * std::sin(x * 0.17 - time_ * 0.7);
        c.ch = v > 0.5 ? U'~' : v > -0.5 ? U'-' : U'_';
        c.fg = {175, 232, 245};
        c.bg = waterTop;
      }
      out.at(x, y) = c;
    }
  }
}

void World::render(Frame& out) const {
  out.resize(w_, h_);
  renderBackground(out);

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
          if (m != U'_') cell.bg = maskColor(m, *e);
        } else {
          cell.ch = ch;
          cell.fg = maskColor(m, *e);
        }
      }
    }
  }
}

}  // namespace dio
