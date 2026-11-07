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
#include "core/scene.h"

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
  std::string tag;  ///< "prey", "food", ... - looked up by behaviors
  /// Draw the left/right flipped art.
  bool mirrored = false;
  bool facesRight = true;  ///< direction the unmirrored art points
  /// Sent by the event scheduler. At most one such entity exists at a time.
  bool isEvent = false;
  /// Remove the entity once its non-looping animation has played.
  bool dieAfterAnim = false;
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
  /// Sprite row where it eats or bites.
  int mouthRow = 0;
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
  /// True if it travels towards the right edge.
  bool movingRight() const { return facesRight ? !mirrored : mirrored; }
};

/// The simulation: owns entities, advances them in fixed-size steps and renders
/// the result into a Frame. No I/O and no clocks - fully deterministic per seed.
class World {
 public:
  /// Builds the scene layout for a width by height cell area and populates it.
  /// @param pack sprites and scenes; must outlive the world
  /// @param scene the scene to build
  /// @param seed seeds all randomness, so equal seeds give equal worlds
  World(const AssetPack& pack, std::shared_ptr<const SceneDef> scene, int width, int height,
        uint64_t seed);

  /// Advances the simulation by dt seconds.
  void step(double dt);
  /// Draws the scene into out.
  /// @param style how colorful to draw
  void render(Frame& out) const;

  /// Drops food into the scene (up to 10 flakes at once). Returns how many were added.
  int feed();

  /// Width in cells.
  int width() const { return w_; }
  /// Height in cells.
  int height() const { return h_; }
  /// Row of the surface line. Equals seabedY when the scene has no medium.
  int surfaceY() const { return surfaceY_; }
  /// First row of the ground.
  int seabedY() const { return seabedY_; }
  /// True if there is a medium (water) between the surface and the ground.
  bool hasMedium() const { return hasMedium_; }
  /// Simulated seconds since the start.
  double time() const { return time_; }
  /// Number of entities currently in the world.
  size_t entityCount() const { return entities_.size(); }
  /// All entities, for behaviors and tests.
  std::vector<std::unique_ptr<Entity>>& entities() { return entities_; }
  /// Number of live entities carrying the tag.
  size_t countTag(const std::string& tag) const;
  /// Prey caught by predators so far.
  size_t eatenCount() const { return eaten_; }          // prey caught by predators
  /// Food flakes eaten by fish so far.
  size_t foodEatenCount() const { return foodEaten_; }  // food flakes eaten by fish
  /// Seed the world was built with.
  uint64_t seed() const { return seed_; }
  /// The scene being simulated.
  const SceneDef& scene() const { return *scene_; }
  /// Random source shared by the world and its behaviors.
  Rng& rng() { return rng_; }
  /// Sprites and scenes the world was built from.
  const AssetPack& pack() const { return *pack_; }

  /// Closest live entity with `tag` within `maxDist` columns (rows count double).
  Entity* nearest(const std::string& tag, double x, double y, double maxDist);

  /// Spawning is deferred to the end of the step so behaviors can call it freely.
  void spawn(std::unique_ptr<Entity> e) { incoming_.push_back(std::move(e)); }
  /// Spawns an entity from a sprite according to its header (speed, depth,
  /// habitat, behaviors).
  /// @param initial true while populating: place it anywhere instead of just off screen
  /// @param event true if it is a scheduled visitor
  void spawnCreature(const std::shared_ptr<const Sprite>& sprite, bool initial, bool event = false);
  /// Spawns an entity at an exact position with an exact velocity (bubbles,
  /// debris, food...).
  void spawnEffect(const std::string& spriteName, double x, double y, int z, double vx, double vy);
  /// Counts a catch. Called by predator behaviors.
  void noteEaten() { ++eaten_; }
  /// Counts an eaten food flake. Called by feeding behaviors.
  void noteFoodEaten() { ++foodEaten_; }

 private:
  /// Spawns the scene population and decorations.
  void populate();
  /// Places one decoration.
  void spawnDecor(const SceneDef::Decor& d);
  /// Grows one procedural seaweed plant.
  void spawnSeaweed();
  /// Sends one weighted-random scene event.
  void spawnEvent();
  /// Number of cells in a habitat, used for per-cells populations.
  double areaFor(const std::string& habitat) const;
  /// Picks a starting row for a sprite of the given height in its habitat.
  double habitatY(const Props& p, int spriteHeight);
  /// Moves entities spawned during the step into the world.
  void flushIncoming();
  /// Draws sky, medium, surface line and ground.
  void renderBackground(Frame& out) const;

  /// Not owned; must outlive the world.
  const AssetPack* pack_;
  /// The scene being simulated.
  std::shared_ptr<const SceneDef> scene_;
  /// Size in cells.
  int w_, h_;
  /// Layout rows: the surface line and the first ground row.
  int surfaceY_ = 0, seabedY_ = 0;
  /// True if the scene has a medium and there is room for it.
  bool hasMedium_ = true;
  /// Seed the world was built with.
  uint64_t seed_;
  /// All randomness comes from here.
  Rng rng_;
  /// Simulated seconds since the start.
  double time_ = 0;
  /// Seconds until the next event. Negative means none is scheduled yet.
  double eventTimer_ = 0;
  /// Counters behind eatenCount, foodEatenCount and destroyedCount.
  size_t eaten_ = 0, foodEaten_ = 0;
  /// Entities in the world.
  std::vector<std::unique_ptr<Entity>> entities_;
  /// Entities spawned during the current step.
  std::vector<std::unique_ptr<Entity>> incoming_;
};

}  // namespace dio
