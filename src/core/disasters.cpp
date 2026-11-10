/// @file core/disasters.cpp
/// @brief The second catastrophes of the scenes: the leaking submarine's poison cloud (aquarium).

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

}  // namespace dio
