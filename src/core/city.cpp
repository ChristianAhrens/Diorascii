/// @file core/city.cpp
/// @brief The city scene: skyline of skyscrapers, elevated railway, street and street lamps.

#include <algorithm>
#include <cmath>

#include "core/shading.h"
#include "core/world.h"

namespace dio {

namespace {


/// Wall colors of the buildings: blues, greys, sandstone, brick and dark slate.
constexpr Rgb kWalls[] = {{104, 120, 152}, {126, 112, 130}, {138, 143, 156}, {92, 116, 136},
                          {140, 123, 107}, {112, 128, 138}, {154, 138, 120}, {88, 98, 128},
                          {78, 112, 150}, {176, 150, 112}, {70, 76, 92},    {168, 170, 178},
                          {150, 98, 80},  {118, 140, 124}};
/// Number of wall colors.
constexpr int kWallCount = static_cast<int>(sizeof(kWalls) / sizeof(kWalls[0]));

/// Light of a lit window.
constexpr Rgb kLit{255, 214, 120};

/// How far the street lamps are apart, and the column of the first one.
constexpr int kLampGap = 17, kLampFirst = 6;
/// Rows a street lamp stands tall above the sidewalk.
constexpr int kLampHeight = 5;

/// Cells of a sky cell's neighbor in the frame, or nullptr outside it.
Cell* cellAt(Frame& f, int x, int y) { return f.inBounds(x, y) ? &f.at(x, y) : nullptr; }

/// Seconds a skyscraper burns, and how long it is wrapped in scaffolding afterwards.
constexpr double kBurn = 24.0, kScaffold = 8.0;
/// A fire the engine cannot beat: how long a burnt-out ruin stands, how long the rebuilding takes,
/// how long a collapsing building takes to fall in, and the chance that a fire gets out of hand.
constexpr double kRuin = 32.0, kRebuild = 16.0, kCollapse = 3.5, kFireFails = 0.3;

/// Rows of rubble left of a building of height h that has burnt down.
int rubbleRows(int h) { return std::max(2, h / 6); }

/// How fiercely it burns after `t` seconds: it catches, rages, dies down.
double fireAt(double t) {
  if (t < 5) return t / 5;
  if (t > kBurn - 5) return std::max(0.0, (kBurn - t) / 5);
  return 1.0;
}

}  // namespace

size_t World::troubledBuildings() const {
  size_t n = 0;
  for (const Building& b : city_) n += b.state != Building::State::Normal ? 1 : 0;
  return n;
}

size_t World::ruinedBuildings() const {
  size_t n = 0;
  for (const Building& b : city_) n += b.state == Building::State::Ruin ? 1 : 0;
  return n;
}

bool World::fireBurning() const {
  for (const Building& b : city_) {
    if (b.state == Building::State::Burning) return true;
  }
  return false;
}

void World::fitCordon() {
  int lo = w_, hi = 0;
  for (int i : blazeBuildings_) {
    const Building& b = city_[static_cast<size_t>(i)];
    lo = std::min(lo, b.x);
    hi = std::max(hi, b.x + b.w);
  }
  cordonX_ = (lo + hi) / 2.0;
  cordonHalf_ = std::max(18.0, (hi - lo) / 2.0 + 6.0);
}

void World::blazeStep(Entity& e, double dt) {
  if (blazeOrigin_ < 0) {  // sets one of the front skyscrapers alight, away from the edges
    std::vector<int> choices;
    for (int minVisible : {6, 3, 1}) {  // a building whose top shows above the front row; the side streets cover their own cells
      for (size_t i = 0; i < city_.size(); ++i) {
        const Building& b = city_[i];
        const int mid = b.x + b.w / 2;
        bool inStreet = false;
        for (const Avenue& a : avenues_) inStreet |= b.x < a.cx + a.halfW + 2 && b.x + b.w > a.cx - a.halfW - 2;
        int hidden = 0;  // rows of it that the front row hides
        if (b.layer < 2) {
          for (const Building& f : city_) {
            if (f.layer == 2 && f.x < b.x + b.w && f.x + f.w > b.x) hidden = std::max(hidden, f.h);
          }
        }
        if (b.layer >= 1 && b.h - hidden >= minVisible && !inStreet && b.state == Building::State::Normal && mid > w_ / 8 && mid < w_ - w_ / 8) {
          choices.push_back(static_cast<int>(i));
        }
      }
      if (!choices.empty()) break;
    }
    if (choices.empty()) {
      e.dead = true;  // nothing to burn on this screen
      return;
    }
    blazeOrigin_ = choices[static_cast<size_t>(rng_.rangeInt(0, static_cast<int>(choices.size()) - 1))];
    blazeBuildings_ = {blazeOrigin_};
    city_[static_cast<size_t>(blazeOrigin_)].state = Building::State::Burning;
    city_[static_cast<size_t>(blazeOrigin_)].timer = 0;
    // Sometimes the fire engine does not win: the fire spreads to 3-5 neighbours and they all burn down.
    blazeFails_ = forcedOutcome_ >= 0 ? forcedOutcome_ == 1 : rng_.chance(kFireFails);
    spreadLeft_ = blazeFails_ ? rng_.rangeInt(3, 5) : 0;
    nextSpread_ = rng_.range(7.0, 10.0);
    blazeClock_ = 0;
    fitCordon();
    if (auto truck = pack_->find("firetruck"); truck && countSprite("firetruck") == 0) spawnCreature(truck, false, true);
  }
  blazeClock_ += dt;
  if (spreadLeft_ > 0 && blazeClock_ >= nextSpread_) {  // it jumps to the nearest building that is not burning yet
    const Building& origin = city_[static_cast<size_t>(blazeOrigin_)];
    const double from = origin.x + origin.w / 2.0;
    int best = -1;
    double bestDist = 1e9;
    for (size_t i = 0; i < city_.size(); ++i) {
      const Building& b = city_[i];
      bool inStreet = false;
      for (const Avenue& a : avenues_) inStreet |= b.x < a.cx + a.halfW + 2 && b.x + b.w > a.cx - a.halfW - 2;
      const double dist = std::fabs(b.x + b.w / 2.0 - from);
      if (b.layer >= 1 && b.state == Building::State::Normal && !inStreet && b.x + b.w > 0 && b.x < w_ && dist < bestDist) {
        best = static_cast<int>(i);
        bestDist = dist;
      }
    }
    if (best < 0) {
      spreadLeft_ = 0;
    } else {
      city_[static_cast<size_t>(best)].state = Building::State::Burning;
      city_[static_cast<size_t>(best)].timer = 0;
      blazeBuildings_.push_back(best);
      --spreadLeft_;
      nextSpread_ += rng_.range(2.5, 4.0);
      fitCordon();
      if (cordon_) {  // the barriers move out to the new edges
        liftBarriers();
        raiseBarriers();
      }
    }
  }
  double level = 0;
  bool allDone = spreadLeft_ == 0;
  for (int idx : blazeBuildings_) {
    Building& b = city_[static_cast<size_t>(idx)];
    if (b.state == Building::State::Normal) continue;
    allDone = false;
    b.timer += dt;
    if (b.state == Building::State::Burning) {
      if (!blazeFails_ && sprayTime_ > 0) b.timer += 0.7 * dt;  // the water puts a fire it can beat out sooner
      const double now = fireAt(b.timer);
      level = std::max(level, now);
      smokePuffs_ += 6.0 * now * dt;  // smoke billows up from the roof
      for (; smokePuffs_ >= 1; smokePuffs_ -= 1) {
        spawnEffect("smoke", b.x + rng_.range(0, std::max(b.w - 3, 1)), seabedY_ - b.h - rng_.range(0, 3), 4,
                    rng_.range(0.5, 2.0), -rng_.range(1.2, 2.6));
      }
      if (b.timer >= kBurn) {
        b.state = blazeFails_ ? Building::State::Ruin : Building::State::Scaffold;
        b.ruined = blazeFails_;
        b.timer = 0;
      }
    } else if (b.state == Building::State::Ruin) {
      if (b.timer < 14.0 && rng_.chance(2.0 * (1.0 - b.timer / 14.0) * dt)) {  // the ruin smoulders for a while
        spawnEffect("smoke", b.x + rng_.range(0, std::max(b.w - 3, 1)), seabedY_ - std::max(2, b.h / 6) - rng_.range(0, 2), 4,
                    rng_.range(-0.5, 0.5), -rng_.range(0.8, 1.6));
      }
      if (b.timer >= kRuin) {  // after a long while the rebuilding starts
        b.state = Building::State::Scaffold;
        b.timer = 0;
      }
    } else if (b.timer >= (b.ruined ? kRebuild : kScaffold)) {  // renovated or rebuilt: new looks, maybe a new height
      rollBuilding(b);
      b.state = Building::State::Normal;
      b.ruined = false;
      b.timer = 0;
    }
  }
  sprayTime_ = std::max(0.0, sprayTime_ - dt);
  fireLevel_ = level;
  if (allDone) {  // everything is standing again: the site is open
    blazeBuildings_.clear();
    blazeOrigin_ = -1;
    cordon_ = false;
    liftBarriers();
    e.dead = true;
  }
}

void World::placeAvenues() {
  avenues_.clear();
  avenueTimer_.clear();
  if (scene_->avenues <= 0 || seabedY_ < 12) return;
  const int n = std::min(scene_->avenues, w_ / 55);
  if (n <= 0) return;
  const int halfW = 6, depth = std::clamp(seabedY_ / 6, 4, 7);
  auto clear = [&](int c) { return c >= halfW + 4 && c <= w_ - halfW - 5; };
  for (int i = 0; i < n; ++i) {
    const int target = w_ * (i + 1) / (n + 1);
    int chosen = -1;
    for (int d = 0; d <= w_ / (2 * (n + 1)) && chosen < 0; ++d) {
      for (int sign : {1, -1}) {
        const int c = target + sign * d;
        bool apart = true;
        for (const Avenue& a : avenues_) apart &= std::abs(a.cx - c) >= 2 * halfW + 10;
        if (apart && clear(c)) { chosen = c; break; }
      }
    }
    if (chosen < 0) continue;
    avenues_.push_back({chosen, halfW, depth});
    avenueTimer_.push_back(rng_.range(2.0, 6.0));
  }
}

void World::stepCity(double dt) {
  static const char* const kinds[] = {"car_sedan", "car_hatch", "taxi", "truck"};
  const int rows = std::max(h_ - seabedY_, 1);
  for (size_t i = 0; i < avenues_.size(); ++i) {
    avenueTimer_[i] -= dt;
    if (avenueTimer_[i] > 0) continue;
    avenueTimer_[i] = rng_.range(3.5, 9.0);
    if (!rng_.chance(trafficLevel())) continue;  // fewer cars come down the side streets when it is quiet
    const Avenue& a = avenues_[i];
    const std::string name = kinds[rng_.rangeInt(0, 3)];
    Entity* e = spawnEffect(name, 0, 0, 24, 0, 0);  // comes down the avenue from far away
    if (!e) continue;
    e->driveState = 3;
    if (e->sprite->frameCount() >= 5) e->frame = 4;  // seen from the front
    e->driveAvenue = static_cast<int>(i);
    e->driveDir = rng_.chance(0.5) ? 1 : -1;
    e->driveSpeed = rng_.range(5.0, 8.0);
    e->mirrored = (e->driveDir > 0) != e->facesRight;
    const int lo = 1, hi = std::max(rows - 2, 1);
    const int feet = e->driveDir > 0 ? hi : lo;  // the lane it will join
    e->driveY0 = seabedY_ - e->height() + feet + 1;
    e->driveZ = 12 - feet;
    e->x = a.cx - e->width() / 2.0;
    e->y = (seabedY_ - a.depth + 1) - e->height() + 1;  // its feet at the far end of the avenue
  }
}

void World::driveStep(Entity& e, double dt) {
  auto smooth = [](double t) { t = std::clamp(t, 0.0, 1.0); return t * t * (3 - 2 * t); };
  // Which way the car faces the camera: frame 0 side, 1 and 2 from behind (three-quarters, straight),
  // 3 and 4 from the front. A sprite with fewer frames keeps its side view.
  auto setView = [&](size_t frame) { if (e.sprite->frameCount() >= 5) e.frame = frame; };
  if (e.driveAvenue == -2) {  // decide once whether to turn off, and into which street
    e.driveAvenue = -1;
    if (e.driveState == 0 && !avenues_.empty() && rng_.chance(e.turnChance)) {
      const double mid = e.x + e.width() / 2.0;
      double best = 1e9;
      for (size_t i = 0; i < avenues_.size(); ++i) {
        const double ahead = (avenues_[i].cx - mid) * (e.vx > 0 ? 1 : -1);
        if (ahead > 2 && ahead < best) { best = ahead; e.driveAvenue = static_cast<int>(i); }
      }
    }
  }
  switch (e.driveState) {
    case 0: {  // along the main road
      e.x += e.vx * dt;
      if ((e.vx > 0 && e.x > w_) || (e.vx < 0 && e.x + e.width() < 0)) { e.dead = true; return; }
      if (e.driveAvenue >= 0) {
        const Avenue& a = avenues_[static_cast<size_t>(e.driveAvenue)];
        const double mid = e.x + e.width() / 2.0;
        if ((e.vx > 0 && mid >= a.cx - 3) || (e.vx < 0 && mid <= a.cx + 3)) {
          e.driveState = 1;
          e.driveT = 0;
          e.driveX0 = e.x;
          e.driveY0 = e.y;
          e.driveZ = e.z;
          e.z = 24;  // further away than the traffic on the main road, and behind the viaduct
        }
      }
      break;
    }
    case 1: {  // swinging into the side street
      const Avenue& a = avenues_[static_cast<size_t>(e.driveAvenue)];
      e.driveT += dt;
      const double k = smooth(e.driveT / 1.1);
      e.x = e.driveX0 + (a.cx - e.width() / 2.0 - e.driveX0) * k;
      e.y = e.driveY0 - 2.0 * k;
      setView(k < 0.3 ? 0 : k < 0.7 ? 1 : 2);  // side, then three-quarters from behind, then from behind
      if (e.driveT >= 1.1) e.driveState = 2;
      break;
    }
    case 2: {  // up the avenue, smaller steps as it gets far away, until it is gone
      const Avenue& a = avenues_[static_cast<size_t>(e.driveAvenue)];
      const double feet = e.y + e.height() - 1;
      const double progress = std::clamp((seabedY_ - 1 - feet) / a.depth, 0.0, 1.0);
      setView(2);
      e.y -= 3.6 * (1.0 - 0.6 * progress) * dt;
      if (e.y + e.height() - 1 <= seabedY_ - a.depth + 1) e.dead = true;
      break;
    }
    case 3: {  // coming down the avenue, faster as it gets near
      const Avenue& a = avenues_[static_cast<size_t>(e.driveAvenue)];
      const double feet = e.y + e.height() - 1;
      const double progress = std::clamp((seabedY_ - 1 - feet) / a.depth, 0.0, 1.0);  // 1 = far away
      e.y += (2.0 + 2.2 * (1.0 - progress)) * dt;
      setView(e.y >= e.driveY0 - 4.0 ? 3 : 4);  // facing the camera, then turning to the side
      if (e.y >= e.driveY0 - 2.0) {
        e.driveState = 4;
        e.driveT = 0;
        e.driveX0 = e.x;
      }
      break;
    }
    default: {  // turning out onto the main road
      e.driveT += dt;
      const double k = smooth(e.driveT / 1.1);
      setView(k < 0.45 ? 3 : 0);
      e.vx = e.driveDir * e.driveSpeed * k;
      e.x += e.vx * dt;
      e.y += (e.driveY0 - e.y) * std::min(1.0, 4.0 * dt);
      if (e.driveT >= 1.1) {
        e.driveState = 0;
        e.driveAvenue = -1;
        e.vx = e.driveDir * e.driveSpeed;
        e.y = e.anchorY = e.driveY0;
        e.z = e.driveZ;
      }
      break;
    }
  }
}

void World::paintAvenue(Frame& out, const Avenue& a, Style style, const Light& light) const {
  const bool full = style == Style::Full;
  const Rgb pavement = backdrop(scene_->groundA, light), asphalt = backdrop(scene_->groundB, light);
  const Rgb haze = backdrop(scene_->skyLow, light);
  for (int r = 0; r < a.depth; ++r) {
    const int y = seabedY_ - 1 - r;
    const double far = static_cast<double>(r) / a.depth;
    const int hw = std::max(2, static_cast<int>(std::lround(a.halfW * (1.0 - 0.35 * far))));  // a little narrower further back
    for (int x = a.cx - hw - 1; x <= a.cx + hw + 1; ++x) {
      Cell* c = cellAt(out, x, y);
      if (!c) continue;
      const int dx = x - a.cx;
      const bool road = std::abs(dx) <= hw;
      c->ch = road ? ((dx == 0 && r % 2 == 1) ? U':' : U' ') : (dx < 0 ? U'/' : U'\\');
      c->fg = road ? backdrop(Rgb{225, 200, 90}, light) : scale(pavement, 0.7);
      c->bg = lerp(road ? asphalt : pavement, haze, 0.3 * far);
      c->defaultBg = !full;
      c->defaultFg = style == Style::Mono;
    }
  }
}

void World::raiseBarriers() {
  // Barriers across the whole street at both ends of the site: sidewalks and road.
  const int rows = std::max(h_ - seabedY_, 1);
  for (int side = -1; side <= 1; side += 2) {
    const double x = cordonX_ + side * cordonHalf_ - 1;
    for (int g = 0; g < rows; ++g) {
      Entity* b = spawnEffect("barrier", x, seabedY_ - 4 + g + 1, 4, 0, 0);
      if (b) b->y = b->anchorY = seabedY_ + g - b->height() + 1;
    }
  }
}

void World::liftBarriers() {
  for (auto& e : entities_) {
    if (e->tag == "barrier") e->dead = true;
  }
  for (auto& e : incoming_) {
    if (e->tag == "barrier") e->dead = true;
  }
}

void World::engineStep(Entity& e, double dt) {
  const bool burning = blazeOrigin_ >= 0 && fireBurning();
  if (e.driveState == 0 && e.driveSpeed == 0) e.driveSpeed = std::fabs(e.vx);  // its cruising speed
  if (e.driveState == 0) {
    e.x += e.vx * dt;  // on its way to the fire
    if ((e.vx > 0 && e.x > w_) || (e.vx < 0 && e.x + e.width() < 0)) { e.dead = true; return; }
    if (burning) {
      const Building& b = city_[static_cast<size_t>(blazeOrigin_)];
      const double target = b.x + b.w / 2.0, mid = e.x + e.width() / 2.0;
      if ((e.vx > 0 && mid >= target) || (e.vx < 0 && mid <= target)) {  // arrived: park and cordon the site off
        e.driveDir = e.vx > 0 ? 1 : -1;
        e.vx = 0;
        e.driveState = 1;
        cordon_ = true;
        fitCordon();
        raiseBarriers();
      }
    }
  } else if (e.driveState == 1) {
    if (burning) {  // fighting the fire: a jet of water arcs up to the facade
      sprayTime_ = 0.3;
      const double mine = e.x + e.width() / 2.0;
      const Building* target = nullptr;  // the burning building nearest to it
      for (int idx : blazeBuildings_) {
        const Building& cand = city_[static_cast<size_t>(idx)];
        if (cand.state != Building::State::Burning) continue;
        if (!target || std::fabs(cand.x + cand.w / 2.0 - mine) < std::fabs(target->x + target->w / 2.0 - mine)) target = &cand;
      }
      if (!target) return;
      const Building& b = *target;
      const double nx = e.x + e.width() / 2.0, ny = e.y + 1;
      const double reachTime = 0.55;
      const int drops = static_cast<int>(40 * dt) + (rng_.chance(40 * dt - static_cast<int>(40 * dt)) ? 1 : 0);
      for (int i = 0; i < drops; ++i) {
        const double tx = b.x + b.w / 2.0 + rng_.range(-b.w / 3.0, b.w / 3.0);
        const double ty = seabedY_ - 1 - b.h * rng_.range(0.3, 0.8);
        spawnEffect("water", nx, ny, 3, (tx - nx) / reachTime, (ty - ny) / reachTime);
      }
    } else {  // the fire is out, put out or burnt out: drive on, siren off
      e.driveState = 2;
      e.vx = e.driveDir * e.driveSpeed;
      e.tag.clear();  // it no longer sounds its siren
    }
  } else {
    e.x += e.vx * dt;
    if ((e.vx > 0 && e.x > w_) || (e.vx < 0 && e.x + e.width() < 0)) e.dead = true;
  }
}

int World::skylineTop() const {
  int top = h_;
  for (const Building& b : city_) top = std::min(top, seabedY_ - 1 - b.h - b.antenna + 1);
  return top;
}

void World::rollBuilding(Building& b) {
  const int ground = seabedY_;
  const int lo = std::max(3, static_cast<int>(scene_->skyline.first * ground));
  const int hi = std::clamp(static_cast<int>(scene_->skyline.second * ground), lo, std::max(ground - 2, lo));
  b.roof = rng_.rangeInt(0, 5);
  b.antenna = b.roof == 1 ? rng_.rangeInt(2, 4) : (b.roof == 3 || b.roof == 4) ? 2 : 0;
  // Heights: the far row is tall and pale, the middle row mixes low offices, mid-rise and towers,
  // the front row keeps to low and mid-rise buildings.
  const double span = hi - lo;
  double share;
  if (b.layer == 0) share = rng_.range(0.35, 1.0);
  else if (b.layer == 1) {
    const double r = rng_.uniform();
    share = r < 0.3 ? rng_.range(0.0, 0.3) : r < 0.7 ? rng_.range(0.25, 0.6) : rng_.range(0.6, 1.0);
  } else {
    share = rng_.range(0.0, 0.55);
  }
  const int room = std::max(ground - 1 - b.antenna, 3);
  b.h = std::clamp(lo + static_cast<int>(share * span), 3, room);
  b.tint = rng_.rangeInt(0, kWallCount - 1);
  b.facade = rng_.rangeInt(0, 3);
  b.seed = static_cast<uint32_t>(rng_.rangeInt(1, 1 << 24));
}

void World::buildCity() {
  city_.clear();
  glimpses_.clear();
  railY_ = -1;
  if (seabedY_ < 8) return;  // too small for a skyline
  if (seabedY_ >= 16) railY_ = seabedY_ - 7;
  // The front row keeps out of the side streets; this is the column where the way is free again.
  auto gapEnd = [&](int x0, int x1) {
    for (const Avenue& a : avenues_) {
      if (x1 > a.cx - a.halfW - 1 && x0 < a.cx + a.halfW + 2) return a.cx + a.halfW + 2;
    }
    return -1;
  };
  for (int layer = 0; layer < 3; ++layer) {
    int x = -rng_.rangeInt(0, 6);
    while (x < w_) {
      Building b;
      b.layer = layer;
      b.w = layer == 0 ? rng_.rangeInt(9, 20) : layer == 1 ? rng_.rangeInt(6, 15) : rng_.rangeInt(5, 12);
      if (layer == 2) {
        for (int end = gapEnd(x, x + b.w); end >= 0; end = gapEnd(x, x + b.w)) x = end;
        if (x >= w_) break;
      }
      b.x = x;
      rollBuilding(b);
      city_.push_back(b);
      // The far row overlaps so that there are no holes in the sky; the front row has gaps in places.
      x += b.w + (layer == 0 ? rng_.rangeInt(-2, 2) : layer == 1 ? rng_.rangeInt(1, 4) : (rng_.chance(0.4) ? rng_.rangeInt(6, 12) : rng_.rangeInt(2, 6)));
    }
  }
  // Where the front row has a gap, another side street can be seen running back behind it.
  std::vector<bool> covered(static_cast<size_t>(w_), false);
  for (const Building& b : city_) {
    if (b.layer != 2) continue;
    for (int x = std::max(b.x, 0); x < std::min(b.x + b.w, w_); ++x) covered[static_cast<size_t>(x)] = true;
  }
  const int most = std::max(1, w_ / 55);
  for (int x = 0; x < w_ && static_cast<int>(glimpses_.size()) < most;) {
    if (covered[static_cast<size_t>(x)]) { ++x; continue; }
    int end = x;
    while (end < w_ && !covered[static_cast<size_t>(end)]) ++end;
    const int width = end - x, cx = (x + end) / 2;
    bool near = false;
    for (const Avenue& a : avenues_) near |= std::abs(a.cx - cx) < a.halfW + 8;
    for (const Avenue& g : glimpses_) near |= std::abs(g.cx - cx) < 14;
    if (width >= 6 && !near && x > 0 && end < w_) glimpses_.push_back({cx, std::min(3, width / 2 - 1), std::clamp(seabedY_ / 4, 4, 9)});
    x = end + 1;
  }
}

void World::renderCityBack(Frame& out, Style style, const Light& light) const {
  if (!city_.empty()) renderSkyline(out, style, light);
  for (const Avenue& a : avenues_) paintAvenue(out, a, style, light);  // the viaduct crosses over them
}

void World::renderSkyline(Frame& out, Style style, const Light& light) const {
  const int base = seabedY_ - 1;  // the lowest sky row: where the buildings stand
  const bool full = style == Style::Full;
  // How many windows are lit: none by day, more and more as it gets dark.
  const double dark = std::clamp((0.8 - light.sun) / 0.6, 0.0, 1.0);
  const double lightsOn = 0.58 * dark;
  const int bucket = static_cast<int>(time_ / 14.0);  // a few windows switch on and off now and then

  bool glimpsed = false;
  for (const Building& b : city_) {
    if (!glimpsed && b.layer == 2) {  // streets seen between the front buildings are behind them
      for (const Avenue& g : glimpses_) paintAvenue(out, g, style, light);
      glimpsed = true;
    }
    Rgb wall = kWalls[b.tint];
    if (b.layer == 0) wall = lerp(wall, scene_->skyLow, 0.45);       // far away: pale
    else if (b.layer == 1) wall = lerp(wall, scene_->skyLow, 0.18);  // in the middle: a little hazy
    const Rgb wallLit = backdrop(wall, light);
    const Rgb glass = backdrop(lerp(Rgb{128, 176, 214}, wall, b.layer == 0 ? 0.5 : 0.3), light);
    // A building that burns down falls in; one that is rebuilt from the rubble grows back up.
    const int rubble = rubbleRows(b.h);
    int shown = b.h;
    if (b.state == Building::State::Ruin) shown = rubble + static_cast<int>((b.h - rubble) * std::max(0.0, 1.0 - b.timer / kCollapse));
    else if (b.state == Building::State::Scaffold && b.ruined) shown = rubble + static_cast<int>((b.h - rubble) * std::min(1.0, b.timer / kRebuild));
    const int top = base - shown + 1;
    if (b.state == Building::State::Ruin) {  // charred rubble with embers that fade
      const double embers = std::max(0.0, 1.0 - b.timer / 24.0);
      for (int y = top; y <= base; ++y) {
        for (int x = b.x; x < b.x + b.w; ++x) {
          Cell* c = cellAt(out, x, y);
          if (!c) continue;
          const uint32_t hv = hash2(x + static_cast<int>(b.seed % 97), y);
          const bool jagged = y == top && hv % 3 == 0;  // a ragged top edge
          if (jagged) continue;
          const bool glow = (hv >> 9) % 100 < 22 * embers;
          c->ch = full ? (hv % 4 == 0 ? U'%' : U'#') : U'#';
          c->fg = glow ? Rgb{255, 120, 40} : backdrop(Rgb{96, 80, 70}, light);
          c->defaultFg = style == Style::Mono;
          if (full) {
            c->bg = glow ? lerp(Rgb{70, 30, 20}, Rgb{150, 50, 20}, ((hv >> 4) % 10) / 10.0) : backdrop(Rgb{40, 36, 34}, light);
            c->defaultBg = false;
          } else {
            c->defaultBg = true;
          }
        }
      }
      continue;
    }
    for (int y = top; y <= base; ++y) {
      // A stepped roof is narrower in its top two rows.
      const int inset = (b.roof == 2 && y < top + 2) ? 2 : 0;
      for (int x = b.x + inset; x < b.x + b.w - inset; ++x) {
        Cell* c = cellAt(out, x, y);
        if (!c) continue;
        const int cx = x - b.x, cy = base - y;  // column in the building, rows above the street
        const bool edge = x == b.x + inset || x == b.x + b.w - 1 - inset;
        const bool roofLine = y == top;
        bool window = false;
        if (!edge && !roofLine && cy >= 1 && y > top + 1) {
          switch (b.facade) {
            case 0: window = cx % 2 == 1 && cy % 2 == 1; break;               // a grid of windows
            case 1: window = cx % 3 != 0 && cy % 4 != 0; break;               // a glass curtain wall
            case 2: window = cy % 3 == 1; break;                              // ribbons of windows
            default: window = cx % 2 == 0 && cy % 2 == 1; break;              // small windows
          }
        }
        bool lit = false;
        if (window) {
          const uint32_t h = hash2(x + static_cast<int>(b.seed % 997), y);
          lit = (h % 1000) < lightsOn * 1000.0;
          if (lightsOn > 0.05 && (hash2(x, y + bucket * 31) % 100) < 3) lit = !lit;  // someone turns a light on or off
        }
        if (full) {
          Rgb bg = wallLit;
          if (x >= b.x + b.w - 2 - inset) bg = scale(bg, 0.84);  // the far side in shade
          if (roofLine) bg = scale(wallLit, 0.7);
          if (window) bg = lit ? kLit : glass;
          c->bg = bg;
          c->ch = U' ';
          c->defaultBg = false;
        } else {
          c->defaultBg = true;
          c->ch = roofLine ? U'_' : edge ? U'|' : window ? (lit ? U'o' : U'.') : U' ';
          c->fg = window && lit ? kLit : scale(wall, 1.1);
          c->defaultFg = style == Style::Mono;
        }
      }
    }
    if (b.state == Building::State::Burning) {
      const double level = fireAt(b.timer);
      const int flameRows = std::max(2, static_cast<int>(b.h * 0.6 * level));
      const int tick = static_cast<int>(time_ * 9);
      for (int y = top - 2; y < top + flameRows; ++y) {
        for (int x = b.x; x < b.x + b.w; ++x) {
          Cell* c = cellAt(out, x, y);
          if (!c || y > base) continue;
          const uint32_t hv = hash2(x + tick * 13, y);
          const bool above = y < top;  // flames reaching over the roof
          const double chance = above ? 0.55 * level * (1.0 - 0.4 * (top - y)) : (0.75 - 0.4 * (y - top) / flameRows) * level;
          if ((hv % 100) >= chance * 100) continue;
          const double heat = ((hv >> 8) % 100) / 100.0;
          static const char32_t kFlame[] = {U'^', U'*', U'"', U'\'', U'('};
          c->ch = kFlame[(hv >> 16) % 5];
          c->fg = lerp(Rgb{255, 150, 40}, Rgb{255, 235, 110}, heat);
          c->defaultFg = style == Style::Mono;
          if (full && !above) {
            c->bg = lerp(Rgb{130, 24, 10}, Rgb{250, 120, 30}, heat);
            c->defaultBg = false;
          }
        }
      }
      if (full) {  // the windows below glow red
        for (int y = top + flameRows; y < top + static_cast<int>(b.h * 0.85); ++y) {
          for (int x = b.x + 1; x < b.x + b.w - 1; ++x) {
            Cell* c = cellAt(out, x, y);
            if (c && (x - b.x) % 2 == 1 && (base - y) % 2 == 1 && (hash2(x, y + tick) % 3) == 0) c->bg = Rgb{235, 70, 35};
          }
        }
      }
    } else if (b.state == Building::State::Scaffold) {
      const Rgb pole = backdrop(Rgb{168, 124, 72}, light);
      for (int y = top; y <= base; ++y) {
        for (int x = b.x; x < b.x + b.w; ++x) {
          Cell* c = cellAt(out, x, y);
          if (!c) continue;
          if (full) c->bg = scale(c->bg, 0.7);
          if (y % 3 == 0 || (x - b.x) % 4 == 0) {
            c->ch = y % 3 == 0 ? U'=' : U'|';
            c->fg = pole;
            c->defaultFg = style == Style::Mono;
          }
        }
      }
      const int cx = b.x + b.w / 2;  // a crane on the roof
      for (int k = 1; k <= 3; ++k) {
        if (Cell* c = cellAt(out, cx, top - k)) { c->ch = U'|'; c->fg = pole; c->defaultFg = style == Style::Mono; }
      }
      for (int k = -5; k <= 3; ++k) {
        if (Cell* c = cellAt(out, cx + k, top - 3)) { c->ch = k == 3 ? U'v' : U'-'; c->fg = pole; c->defaultFg = style == Style::Mono; }
      }
    }
    // What stands on the roof: an antenna with a red light that blinks at night, a crown, a water
    // tower or a helipad.
    if (b.roof == 1 && b.antenna > 0) {
      const int ax = b.x + b.w / 2;
      for (int k = 1; k <= b.antenna; ++k) {
        Cell* c = cellAt(out, ax, top - k);
        if (!c) continue;
        const bool tip = k == b.antenna;
        c->ch = tip ? U'*' : U'|';
        const bool blink = std::sin(time_ * 3.0) > 0;
        c->fg = tip ? (blink || light.sun > 0.5 ? Rgb{235, 60, 50} : Rgb{120, 40, 40}) : backdrop(Rgb{150, 152, 160}, light);
        c->defaultFg = style == Style::Mono;
      }
    } else if (b.roof == 3 || b.roof == 4) {
      const Rgb trim = scale(wallLit, 0.65);
      auto block = [&](int x0, int x1, int y, char32_t ch) {
        for (int x = x0; x < x1; ++x) {
          Cell* c = cellAt(out, x, y);
          if (!c) continue;
          c->ch = full ? U' ' : ch;
          c->bg = trim;
          c->defaultBg = !full;
          c->fg = scale(wall, 1.1);
          c->defaultFg = style == Style::Mono;
        }
      };
      if (b.roof == 3) {  // a crown of two narrowing steps
        block(b.x + 2, b.x + b.w - 2, top - 1, U'_');
        block(b.x + b.w / 2 - 1, b.x + b.w / 2 + 1, top - 2, U'^');
      } else {  // a water tower on legs
        const int tx = b.x + std::max(1, b.w / 2 - 2);
        block(tx, tx + 4, top - 2, U'_');
        block(tx, tx + 4, top - 1, U'#');
      }
    } else if (b.roof == 5 && b.w >= 6) {  // a helipad
      if (Cell* c = cellAt(out, b.x + b.w / 2, top)) {
        c->ch = U'H';
        c->fg = backdrop(Rgb{245, 245, 245}, light);
        c->defaultFg = style == Style::Mono;
      }
    }
  }
}

void World::renderStreet(Frame& out, Style style, const Light& light) const {
  const bool full = style == Style::Full;
  const int rows = h_ - seabedY_;
  const Rgb pavement = backdrop(scene_->groundA, light), asphalt = backdrop(scene_->groundB, light);

  // The elevated railway: catenary masts and a wire above the train, a dark deck with two
  // bright rails, a concrete beam under it, and heavy pillars down to the street.
  if (railY_ >= 0) {
    const Rgb steel = backdrop(Rgb{215, 218, 228}, light);
    const Rgb deck = backdrop(Rgb{58, 60, 70}, light);
    const Rgb beam = backdrop(Rgb{134, 132, 126}, light);
    const Rgb mast = backdrop(Rgb{92, 94, 104}, light);
    for (int x = 0; x < w_; ++x) {
      Cell& rail = out.at(x, railY_);   // the rails
      rail.ch = U'=';
      rail.fg = steel;
      rail.bg = deck;
      rail.defaultFg = style == Style::Mono;
      rail.defaultBg = !full;
      Cell& under = out.at(x, railY_ + 1);  // the beam under the deck
      under.ch = U'_';
      under.fg = scale(beam, 0.7);
      under.bg = beam;
      under.defaultFg = style == Style::Mono;
      under.defaultBg = !full;
      if (railY_ - 5 >= 0 && x % 2 == 0) {  // the wire the train draws its current from
        Cell& wire = out.at(x, railY_ - 5);
        wire.ch = U'-';
        wire.fg = mast;
        wire.defaultFg = style == Style::Mono;
      }
      if (x % 14 == 3) {  // a mast
        for (int y = std::max(railY_ - 5, 0); y < railY_; ++y) {
          Cell& m = out.at(x, y);
          m.ch = U'|';
          m.fg = mast;
          m.defaultFg = style == Style::Mono;
        }
      }
      if (x % 12 < 2) {  // a pillar, two columns wide
        for (int y = railY_ + 2; y < seabedY_; ++y) {
          Cell& p = out.at(x, y);
          p.ch = full ? U' ' : U'#';
          p.bg = scale(beam, y % 2 ? 0.9 : 1.0);
          p.fg = scale(beam, 0.7);
          p.defaultBg = !full;
          p.defaultFg = style == Style::Mono;
        }
      }
    }
  }

  // The street: a sidewalk behind, the road with its lane line, a sidewalk in front.
  for (int g = 0; g < rows; ++g) {
    const int y = seabedY_ + g;
    const bool walk = g == 0 || g == rows - 1;
    for (int x = 0; x < w_; ++x) {
      Cell c;
      const uint32_t h = hash2(x, y);
      if (walk) {
        c.bg = pavement;
        c.ch = (h % 7 == 0) ? scene_->groundGlyphs.empty() ? U' ' : scene_->groundGlyphs[h % scene_->groundGlyphs.size()] : (x % 5 == 0 ? U'_' : U' ');
        c.fg = scale(pavement, 0.75);
      } else {
        c.bg = asphalt;
        c.ch = U' ';
        c.fg = scale(asphalt, 1.5);
        // The lane line: along the upper edge of the lower lane if the road is two rows, else down the middle.
        const bool lane = rows <= 4 ? g == 2 : g == 1 + (rows - 2) / 2;
        if (lane && (x % 6) < 3) {
          c.ch = rows <= 4 ? U'_' : U'-';
          c.fg = backdrop(Rgb{225, 200, 90}, light);
        }
      }
      if (!full) c.defaultBg = true;
      if (style == Style::Mono) c.defaultFg = true;
      out.at(x, y) = c;
    }
  }
  // Where a side street meets the main street: the sidewalk gives way to the road, and a zebra
  // crossing runs across the side street.
  for (const Avenue& a : avenues_) {
    for (int x = a.cx - a.halfW - 1; x <= a.cx + a.halfW + 1; ++x) {
      Cell* c = cellAt(out, x, seabedY_);
      if (!c) continue;
      const bool edge = std::abs(x - a.cx) > a.halfW;
      c->ch = edge ? (x < a.cx ? U'/' : U'\\') : U' ';
      c->fg = scale(pavement, 0.7);
      c->bg = edge ? pavement : asphalt;
      c->defaultBg = !full;
      c->defaultFg = style == Style::Mono;
    }
    for (int x = a.cx - a.halfW; x <= a.cx + a.halfW; ++x) {  // the zebra crossing, one row up
      Cell* c = cellAt(out, x, seabedY_ - 1);
      if (!c) continue;
      const bool stripe = (x - a.cx) % 2 == 0;
      c->ch = stripe ? U'=' : U' ';
      c->fg = backdrop(Rgb{232, 232, 236}, light);
      c->bg = stripe ? scale(backdrop(Rgb{210, 210, 216}, light), 0.55) : asphalt;
      c->defaultBg = !full;
      c->defaultFg = style == Style::Mono;
    }
  }
  // Zebra crossings here and there.
  for (int x = 4; x + 3 < w_; x += 29) {
    const int at = x + static_cast<int>(hash2(x, 3) % 9);
    for (int g = 1; g < rows - 1; ++g) {
      for (int k = 0; k < 3; ++k) {
        if (at + k >= w_) continue;
        Cell& c = out.at(at + k, seabedY_ + g);
        c.ch = U'=';
        c.fg = backdrop(Rgb{210, 210, 215}, light);
        c.defaultFg = style == Style::Mono;
      }
    }
  }

  // Street lamps along the back sidewalk, lit from dusk on, with a warm pool of light below.
  const double on = std::clamp((0.55 - light.sun) / 0.3, 0.0, 1.0);
  for (int lx = kLampFirst; lx < w_; lx += kLampGap) {
    bool inStreet = false;  // no lamp stands in the mouth of a side street
    for (const Avenue& a : avenues_) inStreet |= std::abs(lx - a.cx) <= a.halfW + 2;
    if (inStreet) continue;
    const int topY = seabedY_ - kLampHeight + 1;
    for (int y = topY + 1; y <= seabedY_; ++y) {
      if (Cell* c = cellAt(out, lx, y)) {
        c->ch = U'|';
        c->fg = backdrop(Rgb{96, 98, 108}, light);
        c->defaultFg = style == Style::Mono;
      }
    }
    if (Cell* c = cellAt(out, lx, topY)) {
      c->ch = U'O';
      c->fg = lerp(Rgb{150, 150, 150}, kLit, on);
      c->defaultFg = style == Style::Mono;
    }
    if (full && on > 0.02) {
      for (int y = topY + 1; y < h_; ++y) {
        for (int x = lx - 6; x <= lx + 6; ++x) {
          Cell* c = cellAt(out, x, y);
          if (!c) continue;
          const double dx = (x - lx) / 6.0, dy = (y - topY) / static_cast<double>(std::max(h_ - topY, 1));
          const double k = 1.0 - std::sqrt(dx * dx + dy * dy * 0.6);
          if (k > 0) c->bg = lerp(c->bg, Rgb{255, 220, 140}, 0.4 * k * on);
        }
      }
    }
  }
}

}  // namespace dio
