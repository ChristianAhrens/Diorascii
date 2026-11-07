/// @file core/world.h
/// @brief The simulation: entities, behaviors, layout and rendering.

#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/assets.h"
#include "core/color.h"
#include "core/frame.h"
#include "core/rng.h"

namespace dio {

class World;
struct Entity;
/// A behavior: called once per step for every entity that lists it.
/// @param dt length of the step in seconds
using BehaviorFn = void (*)(Entity&, World&, double dt);

/// A live thing in the world: a sprite with a position, motion and behaviors.
struct Entity {
  /// What it looks like.
  std::shared_ptr<const Sprite> sprite;
  /// Draw the left/right flipped art.
  bool mirrored = false;
  /// Top-left position in cells (fractional between steps).
  double x = 0, y = 0;
  double anchorY = 0;  ///< resting row for bobbing behaviors
  /// Velocity in cells per second.
  double vx = 0, vy = 0;
  int z = 10;  ///< larger = further away, drawn first
  /// Current animation frame.
  size_t frame = 0;
  /// Animation speed in frames per second, and time collected towards the next frame.
  double fps = 0, clock = 0;
  /// Loop the animation. If false it stops on the last frame.
  bool loop = true;
  std::array<Rgb, 9> pal{};  ///< colors for mask digits 1-9
  Rgb fg{220, 220, 220};     ///< color for ' ' in the mask
  /// Seconds alive, lifetime in seconds (negative means unlimited), and a
  /// random phase offset for bobbing.
  double age = 0, ttl = -1, phase = 0;
  /// Tuning for emit_bubbles (bubbles per second) and bob (amplitude in
  /// rows, frequency in radians per second).
  double bubbleRate = 0, bobAmp = 0, bobFreq = 1;
  /// Marked for removal at the end of the step.
  bool dead = false;
  /// Behaviors, run in order every step.
  std::vector<BehaviorFn> behaviors;
  /// Called after removal, for example to spawn a replacement.
  std::function<void(World&)> onDeath;

  /// Sprite width in cells.
  int width() const { return sprite->width(); }
  /// Sprite height in cells.
  int height() const { return sprite->height(); }
};

/// The simulation: owns entities, advances them in fixed-size steps and renders
/// the result into a Frame. No I/O and no clocks - fully deterministic per seed.
class World {
 public:
  /// Builds the scene layout for a width by height cell area and populates it.
  /// @param pack sprites and scenes; must outlive the world
  /// @param scene the scene to build
  /// @param seed seeds all randomness, so equal seeds give equal worlds
  World(const AssetPack& pack, int width, int height, uint64_t seed);

  /// Advances the simulation by dt seconds.
  void step(double dt);
  /// Draws the scene into out.
  /// @param style how colorful to draw
  void render(Frame& out) const;

  /// Width in cells.
  int width() const { return w_; }
  /// Height in cells.
  int height() const { return h_; }
  /// Row of the surface line. Equals seabedY when the scene has no medium.
  int surfaceY() const { return surfaceY_; }
  /// First row of the ground.
  int seabedY() const { return seabedY_; }
  /// Simulated seconds since the start.
  double time() const { return time_; }
  /// Number of entities currently in the world.
  size_t entityCount() const { return entities_.size(); }
  /// Seed the world was built with.
  uint64_t seed() const { return seed_; }
  /// Random source shared by the world and its behaviors.
  Rng& rng() { return rng_; }

  /// Spawning is deferred to the end of the step so behaviors can call it freely.
  void spawn(std::unique_ptr<Entity> e) { incoming_.push_back(std::move(e)); }
  /// Spawns an entity from a sprite according to its header (speed, depth,
  /// habitat, behaviors).
  /// @param initial true while populating: place it anywhere instead of just off screen
  /// @param event true if it is a scheduled visitor
  void spawnCreature(const std::shared_ptr<const Sprite>& sprite, bool initial);
  /// Spawns an entity at an exact position with an exact velocity (bubbles,
  /// debris, food...).
  void spawnEffect(const std::string& spriteName, double x, double y, int z, double vx, double vy);
  /// Sprites and scenes the world was built from.
  const AssetPack& pack() const { return *pack_; }

 private:
  /// Spawns the scene population and decorations.
  void populate();
  /// Grows one procedural seaweed plant.
  void spawnSeaweed(bool initial);
  /// Moves entities spawned during the step into the world.
  void flushIncoming();
  /// Draws sky, medium, surface line and ground.
  void renderBackground(Frame& out) const;

  /// Not owned; must outlive the world.
  const AssetPack* pack_;
  /// Size in cells.
  int w_, h_;
  /// Layout rows: the surface line and the first ground row.
  int surfaceY_ = 0, seabedY_ = 0;
  /// Seed the world was built with.
  uint64_t seed_;
  /// All randomness comes from here.
  Rng rng_;
  /// Simulated seconds since the start.
  double time_ = 0;
  /// Entities in the world.
  std::vector<std::unique_ptr<Entity>> entities_;
  /// Entities spawned during the current step.
  std::vector<std::unique_ptr<Entity>> incoming_;
};

}  // namespace dio
