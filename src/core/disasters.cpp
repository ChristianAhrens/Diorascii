/// @file core/disasters.cpp
/// @brief The second catastrophes of the scenes: the leaking submarine's poison cloud (aquarium), the flood (forest), the poachers' helicopter (desert), the yeti (mountains).

#include <algorithm>
#include <cmath>

#include "core/shading.h"
#include "core/world.h"

namespace dio {

namespace {

/// Cells per second each puff of the poison trail spreads (sideways; rows count 2.2 times as much).
constexpr double kToxinSpeed = 6.0;
/// How much taller than wide a cell is, for round clouds.
constexpr double kCellAspect = 2.2;
/// Seconds the cloud stays thick once it covers the sea, and the seconds it then takes to thin out.
constexpr double kToxinLinger = 9.0, kToxinFade = 12.0;

/// Cells per second the front of the flood runs, and the columns over which the water rises behind it
/// (it is as deep as it gets almost at once).
constexpr double kFloodSpeed = 17.0, kFloodRise = 2.0;
/// Columns behind the front over which the water goes from a sparse, wavy look to the solid flood.
constexpr double kWaveLength = 14.0;
/// Seconds the water stands over the land once it has covered it, and the seconds it takes to recede.
constexpr double kFloodHold = 8.0, kFloodRecede = 9.0;
/// Seconds the mud takes to dry, and how long the torn-off things stay away at the least after the
/// flood is over, at the most.
constexpr double kMudDry = 110.0, kReplantMin = 25.0, kReplantMax = 75.0;
/// Seconds of downpour the flood brings.
constexpr double kFloodRain = 24.0;

/// Seconds the poachers hunt at the most, and the seconds with nothing left to shoot at before they go.
constexpr double kPoachMax = 75.0, kPoachIdle = 3.0;
/// Seconds the yeti takes to climb over the ridge.
constexpr double kClimb = 5.0;

/// Palette of the poison: a sickly green.
constexpr Rgb kToxinColor{150, 210, 40};

}  // namespace

void World::leakStep(Entity& e, double dt) {
  if (!toxin_.active) {  // first step: cruising at a third of the depth of the sea, the trail begins
    e.y = surfaceY_ + std::max(1.0, (seabedY_ - surfaceY_ - e.height()) * 0.3);
    e.anchorY = e.y;
    toxin_ = Toxin{};
    toxin_.active = true;
    const double cy = e.y + e.height() / 2.0;
    toxin_.reach = std::max(8.0, std::max(cy - surfaceY_, seabedY_ - cy) * kCellAspect + 3);
  }
  e.x += e.vx * dt;
  e.y = e.anchorY + 0.6 * std::sin(e.age * 0.8);
  // Poison leaks from the stern: a puff every column or so, bubbles rise from the hull.
  const double stern = e.movingRight() ? e.x : e.x + e.width() - 1;
  const double cy = e.y + e.height() / 2.0;
  if (stern >= 0 && stern < w_ && (toxin_.puffs.empty() || std::fabs(toxin_.puffs.back().x - stern) >= 1.0)) {
    toxin_.puffs.push_back({stern, cy, toxin_.age});
  }
  if (rng_.chance(6.0 * dt)) {
    spawnEffect("toxinpuff", stern + rng_.range(-1, 1), cy + rng_.range(-2, 2), e.z - 1, rng_.range(-1, 1), -rng_.range(1, 3));
  }
  if ((e.vx > 0 && e.x > w_) || (e.vx < 0 && e.x + e.width() < 0)) {  // gone: the trail stays
    toxin_.sourceGone = true;
    e.dead = true;
  }
}

double World::puffRadius(const Toxin::Puff& p) const {
  return std::clamp((toxin_.age - p.born) * kToxinSpeed, 0.0, toxin_.reach);
}

void World::stepToxin(double dt) {
  if (!toxin_.active) {
    toxinLevel_ = 0;
    return;
  }
  toxin_.age += dt;
  if (toxin_.sourceGone) toxin_.gone += dt;
  toxin_.strength = toxin_.gone < kToxinLinger ? std::min(1.0, toxin_.age / 2.0)
                                               : std::max(0.0, 1.0 - (toxin_.gone - kToxinLinger) / kToxinFade);
  double widest = 0;
  for (const auto& p : toxin_.puffs) widest = std::max(widest, puffRadius(p));
  toxinLevel_ = toxin_.strength * (0.3 + 0.7 * widest / toxin_.reach);
  if (toxin_.gone >= kToxinLinger + kToxinFade) {
    toxin_.active = false;
    toxin_.puffs.clear();
    return;
  }
  // Everything alive that a puff has reached dies; what would come back waits until it has thinned.
  for (auto& o : entities_) {
    if (o->dead || o->isRare || !o->life) continue;
    const double ox = o->x + o->width() / 2.0, oy = o->y + o->height() / 2.0;
    for (const auto& p : toxin_.puffs) {
      const double r = puffRadius(p);
      if (std::fabs(ox - p.x) > r) continue;
      const double dy = (oy - p.y) * kCellAspect;
      if ((ox - p.x) * (ox - p.x) + dy * dy <= r * r) {
        claim(*o, "toxinpuff");
        break;
      }
    }
  }
  const double remaining = (toxin_.sourceGone ? kToxinLinger + kToxinFade - toxin_.gone : 30.0 + kToxinFade);
  for (auto& r : restock_) {
    if (r.left < remaining) r.left = remaining + rng_.range(0.0, 20.0);
  }
}

void World::renderToxin(Frame& out, Style style, const Light& light) const {
  if (!toxin_.active) return;
  // One puff per column (the latest), so that a cell only looks at the columns around it.
  std::vector<const Toxin::Puff*> byColumn(static_cast<size_t>(w_), nullptr);
  for (const auto& p : toxin_.puffs) {
    const int col = static_cast<int>(p.x);
    if (col >= 0 && col < w_) byColumn[static_cast<size_t>(col)] = &p;
  }
  const int span = static_cast<int>(toxin_.reach);
  for (int y = surfaceY_; y < seabedY_; ++y) {
    for (int x = 0; x < w_; ++x) {
      double depth = -1e9;  // how far inside the nearest edge of the cloud the cell lies
      for (int c = std::max(0, x - span); c <= std::min(w_ - 1, x + span); ++c) {
        const Toxin::Puff* p = byColumn[static_cast<size_t>(c)];
        if (!p) continue;
        const double dx = x - p->x, dy = (y - p->y) * kCellAspect;
        depth = std::max(depth, puffRadius(*p) - std::sqrt(dx * dx + dy * dy));
      }
      if (depth <= 0) continue;
      // Thick inside, a ragged edge, and slowly rolling.
      const double edge = std::clamp(depth / 5.0, 0.0, 1.0);
      const double roll = 0.7 + 0.3 * std::sin(time_ * 1.3 + hash2(x / 3, y / 2) * 0.01 + x * 0.21 + y * 0.5);
      const double density = edge * roll * toxin_.strength;
      if (density < 0.05) continue;
      Cell& c = out.at(x, y);
      if (style == Style::Full) {
        c.bg = lerp(c.bg, shade(kToxinColor, light), 0.62 * density);
        c.fg = lerp(c.fg, shade(kToxinColor, light), 0.5 * density);
      }
      if (c.ch == U' ' && hash2(x, y + static_cast<int>(time_ * 2)) % 9 == 0) {
        c.ch = hash2(x, y) % 2 ? U'o' : U'.';
        c.fg = shade(scale(kToxinColor, 1.15), light);
        c.defaultFg = style == Style::Mono;
      }
    }
  }
}

void World::floodStep(Entity& e, double dt) {
  if (!flood_.started) {  // the river bursts its banks
    flood_ = Flood{};
    flood_.active = flood_.started = true;
    flood_.dir = rng_.chance(0.5) ? 1 : -1;
    flood_.depthMax = std::max(4.0, static_cast<double>(h_ - seabedY_) + 2.0);
    startRain(kFloodRain);
    e.x = flood_.dir > 0 ? 0 : w_ - 1;
    return;
  }
  if (!flood_.active) e.dead = true;
  (void)dt;
}

double World::floodDepth(int x) const {
  if (!flood_.active) return 0;
  const double dist = flood_.dir > 0 ? x : w_ - 1 - x;
  const double rise = std::clamp((flood_.age * kFloodSpeed - dist) / kFloodRise, 0.0, 1.0);
  return flood_.depthMax * rise * (1.0 - flood_.recede);
}

void World::stepFlood(double dt) {
  for (float& m : mud_) m = std::max(0.0f, m - static_cast<float>(dt / kMudDry));
  if (!flood_.active) {
    floodLevel_ = 0;
    return;
  }
  flood_.age += dt;
  const double covered = (w_ + kFloodRise) / kFloodSpeed;  // when the water has reached the far side
  flood_.recede = std::clamp((flood_.age - covered - kFloodHold) / kFloodRecede, 0.0, 1.0);
  if (flood_.recede >= 1.0) {
    flood_.active = false;
    return;
  }
  const double front = flood_.age * kFloodSpeed;
  floodLevel_ = std::min(1.0, 0.25 + front / (w_ + kFloodRise)) * (1.0 - flood_.recede);
  for (int x = 0; x < w_; ++x) {
    if (floodDepth(x) > 0.5) mud_[static_cast<size_t>(x)] = 1.0f;
  }
  // Whatever the water reaches at its feet is torn off: trees, bushes, animals on the bank and on the river.
  for (auto& o : entities_) {
    if (o->dead || o->isRare || !o->destructible || !(o->life || o->grounded)) continue;
    const int column = std::clamp(static_cast<int>(o->x + o->width() / 2.0), 0, w_ - 1);
    const double feet = o->y + o->height() - 1;
    if (floodDepth(column) < h_ - feet) continue;
    const bool plant = !o->life;
    claim(*o, "splash");
    if (plant) {  // a trunk goes down the flood
      spawnEffect("log", o->x + o->width() / 2.0, h_ - flood_.depthMax + rng_.range(0, 1), 3, flood_.dir * kFloodSpeed * 0.75, 0);
    }
  }
  // Branches and logs ride the front.
  if (flood_.recede == 0 && front < w_ && rng_.chance(9 * dt)) {
    const double x = flood_.dir > 0 ? front - rng_.range(0, kFloodRise) : w_ - 1 - front + rng_.range(0, kFloodRise);
    spawnEffect("log", x, h_ - flood_.depthMax + rng_.range(0, 2), 3, flood_.dir * kFloodSpeed * 0.8, 0);
  }
  // What was torn off is not replanted for a good while: the land stays devastated.
  const double remaining = covered + kFloodHold + kFloodRecede - flood_.age;
  for (auto& r : restock_) {
    if (r.left < remaining + kReplantMin) r.left = remaining + rng_.range(kReplantMin, kReplantMax);
  }
}

void World::renderFlood(Frame& out, Style style, const Light& light) const {
  if (!flood_.active) return;
  const Rgb deep = backdrop({104, 78, 46}, light), shallow = backdrop({150, 116, 70}, light);
  const double front = flood_.age * kFloodSpeed;
  for (int x = 0; x < w_; ++x) {
    const int depth = static_cast<int>(std::floor(floodDepth(x)));
    if (depth < 1) continue;
    // Just behind the front the water is a sparse tumble of waves; it fills in to the solid flood.
    const double behind = front - (flood_.dir > 0 ? x : w_ - 1 - x);
    const double solid = std::clamp(behind / kWaveLength, 0.0, 1.0);
    for (int y = h_ - depth; y < h_; ++y) {
      if (y < 0) continue;
      Cell& c = out.at(x, y);
      const bool top = y == h_ - depth;
      const double wave = std::sin(x * 0.7 - time_ * 5.0 * flood_.dir + y * 1.3) + std::sin(x * 0.31 - time_ * 3.0);
      const bool filled = solid >= 1.0 || (hash2(x * 5 + y, static_cast<int>(time_ * 6)) % 100) < solid * 100.0 - 5.0;
      if (!filled) {  // a gap in the waves: only a splash of foam on the land behind
        if (hash2(x, y + static_cast<int>(time_ * 8)) % 3 == 0) {
          c.ch = wave > 0.4 ? U'~' : wave > -0.4 ? U'-' : U'\'';
          c.fg = shade(scale(shallow, 1.6), light);
          c.defaultFg = style == Style::Mono;
        }
        continue;
      }
      c.bg = lerp(shallow, deep, std::min(1.0, 0.25 + 0.15 * (y - (h_ - depth))));
      c.fg = shade(scale(shallow, 1.5), light);
      c.ch = top ? (wave > 0.2 ? U'~' : U'-') : wave > 1.0 ? U'~' : wave > 0.2 ? U'-' : (hash2(x, y) % 6 == 0 ? U'.' : U' ');
      c.bold = top;
      c.defaultBg = style != Style::Full;
      c.defaultFg = style == Style::Mono;
    }
  }
}

void World::poachStep(Entity& e, double dt) {
  if (e.cruise <= 0) e.cruise = std::max(std::fabs(e.vx), 8.0);  // the speed the sprite was given
  const double cx = e.x + e.width() / 2.0, cy = e.y + e.height() / 2.0;
  const Entity* target = nullptr;
  double best = 1e9;
  for (const auto& o : entities_) {
    if (o->dead || o->isRare || !o->life || isAsleep(*o)) continue;
    const double d = std::fabs(o->x + o->width() / 2.0 - cx) + 2.0 * std::fabs(o->y + o->height() / 2.0 - cy);
    if (d < best) {
      best = d;
      target = o.get();
    }
  }
  e.idle = target ? 0 : e.idle + dt;
  if (e.age <= kPoachMax) {  // what has been shot does not come back while the poachers are about
    for (auto& r : restock_) r.left = std::max(r.left, 8.0);
  }
  const bool leaving = e.age > kPoachMax || e.idle > kPoachIdle;
  double wantX = e.vx, wantY = e.y;
  if (leaving) {  // off to the nearer edge, a little higher
    wantX = (cx < w_ / 2.0 ? -1 : 1) * e.cruise * 1.3;
    wantY = e.y - 2;
  } else if (target) {
    const double tx = target->x + target->width() / 2.0;
    wantX = std::clamp((tx - cx) * 1.2, -e.cruise, e.cruise);
    // Hovers a few rows above its target, never lower than the ground.
    wantY = std::clamp(target->y - e.height() - 3.0, 1.0, std::max(1.0, seabedY_ - e.height() - 2.0));
  }
  e.vx += std::clamp(wantX - e.vx, -14.0 * dt, 14.0 * dt);
  e.x += e.vx * dt;
  e.y += std::clamp(wantY - e.y, -3.0 * dt, 3.0 * dt);
  if (std::fabs(e.vx) > 1.0) e.mirrored = (e.vx > 0) != e.facesRight;
  if (!leaving) {
    lash(e);
  } else if (e.x > w_ || e.x + e.width() < 0) {
    e.dead = true;
  }
}

void World::maulStep(Entity& e, double dt) {
  if (e.cruise <= 0) {  // the first step: it comes over the highest ridge
    e.cruise = std::max(std::fabs(e.vx), 4.0);
    int peak = w_ / 2;
    for (int x = 0; x < w_; ++x) {
      if (ridgeRow(x) < ridgeRow(peak)) peak = x;
    }
    e.x = peak - e.width() / 2.0;
    e.vx = 0.01;
    e.behindRidge = true;
  }
  if (e.age < kClimb) {  // it climbs over the ridge from the back: first hidden by the mountain, then rising above it
    const int peak = static_cast<int>(e.x + e.width() / 2.0);
    e.fps = 3.0;
    const double foot = ridgeRow(peak) + 1 + e.height() * (1.0 - e.age / kClimb);
    e.y = foot - e.height() + 1;
    e.z = 22;
    return;
  }
  e.behindRidge = false;
  const double cx = e.x + e.width() / 2.0;
  const Entity* target = nullptr;
  double best = 1e9;
  for (const auto& o : entities_) {
    if (o->dead || o->isRare || !o->life || !o->grounded || isAsleep(*o)) continue;
    const double d = std::fabs(o->x + o->width() / 2.0 - cx) + 3.0 * std::fabs(o->y + o->height() - (e.y + e.height()));  // near it, and on its level
    if (d < best) {
      best = d;
      target = o.get();
    }
  }
  e.idle = target ? 0 : e.idle + dt;
  if (e.age <= kPoachMax) {
    for (auto& r : restock_) r.left = std::max(r.left, 8.0);
  }
  const bool leaving = e.age > kPoachMax || e.idle > kPoachIdle;
  double want = 0;
  if (leaving) {  // back over the mountain, off to the nearer side
    want = (cx < w_ / 2.0 ? -1 : 1) * e.cruise;
  } else if (target) {
    const double gap = target->x + target->width() / 2.0 - cx;
    // It has to get to the animal itself: it walks until the animal is under its body (and then kills it).
    want = std::fabs(gap) > 2.0 ? (gap > 0 ? 1 : -1) * e.cruise : 0.0;
    if (std::fabs(gap) > 1) e.vx = (gap > 0 ? 1 : -1) * std::max(std::fabs(e.vx), 0.01);  // faces its target
  }
  const double dir = e.vx > 0 ? 1 : -1;
  e.vx = want != 0 ? want : dir * 0.01;
  e.x += want * dt;
  e.mirrored = (e.vx > 0) != e.facesRight;
  // Its feet go where its prey stands (the mountain is a face, with the meadow at the bottom): down
  // the slope into the valley. Never above the skyline, and not into the stream. Leaving, it climbs.
  const int column = static_cast<int>(e.x + e.width() / 2.0);
  const double lowest = static_cast<double>(riverRows_ > 0 ? riverTopY_ - 1 : h_ - 1);
  double foot = ridgeRow(column) + 2.0;
  if (!leaving && target) foot = std::clamp(target->y + target->height() - 1.0, ridgeRow(column) + 1.0, lowest);
  const double wantY = foot - e.height() + 1;
  e.y += std::clamp(wantY - e.y, -7.0 * dt, 7.0 * dt);
  e.anchorY = e.y;
  e.z = depthFor(e.y + e.height());
  // Walking shakes the ground: the legs move only while it travels.
  e.fps = want != 0 ? 3.0 : 0.0;
  if (!leaving) {  // whatever is under its body when it gets there is grabbed and killed
    for (auto& o : entities_) {
      if (o->dead || o->isRare || !o->life || !o->grounded || isAsleep(*o)) continue;
      const double mid = o->x + o->width() / 2.0;
      if (mid >= e.x + 4 && mid <= e.x + e.width() - 4 && std::fabs((o->y + o->height()) - (e.y + e.height() - 1)) <= 3.5) {
        claim(*o, "snowpuff");
        spawnEffect("snowpuff", mid - 1, o->y, e.z - 1, rng_.range(-2, 2), -rng_.range(0.5, 2));
      }
    }
  } else if (e.x > w_ || e.x + e.width() < 0) {
    e.dead = true;
  }
}

}  // namespace dio
