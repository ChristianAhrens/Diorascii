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
  bool grounded = false;      ///< habitat: ground
  bool destructible = true;   ///< can be wrecked by an impact
  /// Darkens the background behind its glyphs by this share, 0 to 1 (full style only), so
  /// light sprites stay readable against both bright sky and dark trees.
  double backing = 0;
  /// When the entity is shown, in scenes with a day and night cycle.
  enum class Active { Always, Day, Night };
  /// Visible only by day or only by night (ignored in scenes without a cycle).
  Active active = Active::Always;
  /// Emits its own light: not dimmed at night (fireflies, sun, moon).
  bool glow = false;
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
  /// Tuning for the hop behavior: seconds per hop cycle, and hop height in rows.
  double hopPeriod = 1.2, hopHeight = 1.5;
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

/// How bright the world is right now. All values run from 0 to 1.
struct Light {
  double day = 1;    ///< overall brightness of the surroundings, reduced by cloud cover
  double sun = 1;    ///< how far the sun is up, ignoring clouds
  double glow = 0;   ///< strength of the red horizon glow at sunrise and sunset
  double stars = 1;  ///< how visible the stars are
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
  void render(Frame& out, Style style = Style::Full) const;

  /// The scene's primary action (the F key): drop food, or call down a meteorite
  /// strike. Returns how many things it started (0 = nothing happened).
  int interact();

  /// A meteorite hits the ground around column `cx`: digs a crater, wrecks what
  /// stands there and throws up debris and dust. Dust refills the crater over time.
  void impactAt(double cx);

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
  /// Ground objects wrecked by impacts so far.
  size_t destroyedCount() const { return destroyed_; } // ground objects wrecked by impacts
  /// First row of the river (equals height() when the scene has no river).
  int riverTop() const { return riverTopY_; }
  /// Number of river rows at the bottom of the ground (0 = no river).
  int riverRows() const { return riverRows_; }
  /// True while a rain shower is going on.
  bool raining() const { return rainLeft_ > 0; }
  /// How the time of day advances. The other modes freeze it at a fixed moment.
  enum class TimeMode { Cycle, Dawn, Day, Dusk, Night };
  /// Runs the day and night cycle, or freezes the time of day. Without a cycle in
  /// the scene this changes nothing.
  void setTimeMode(TimeMode mode);
  /// Current time mode.
  TimeMode timeMode() const { return timeMode_; }
  /// Sets the time of day as a fraction of a day: 0 is midnight, 0.25 sunrise,
  /// 0.5 noon and 0.75 sunset.
  void setPhase(double phase);
  /// Sets the time of day in hours, 0 to 24.
  void setHour(double hour) { setPhase(hour / 24.0); }
  /// Time of day as a fraction of a day, see setPhase().
  double phase() const { return phase_; }
  /// True if the scene has a day and night cycle.
  bool hasDayNight() const { return scene_->dayNight; }
  /// Current light levels (everything at full day in scenes without a cycle).
  Light light() const;
  /// Time of day as hours and minutes, for example 14:30.
  std::string clockText() const;
  /// How overcast the sky is, from 0 (clear) to 1 (rain clouds).
  double overcast() const { return overcast_; }
  /// Gravity of the scene in cells per second squared.
  double gravity() const { return scene_->gravity; }
  /// Rows of ground missing at column x (0 = intact).
  int craterRows(int x) const;
  /// Depth of the deepest crater column, in rows (fractional while dust settles).
  double deepestCrater() const;
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
  /// @return the new entity (valid until the end of the step), or nullptr if the sprite is unknown
  Entity* spawnEffect(const std::string& spriteName, double x, double y, int z, double vx, double vy);
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
  /// Half the width of a crater in columns.
  int craterHalfWidth() const;
  /// Lets drops fall for this step while it rains.
  void spawnRain(double dt);
  /// Moves entities spawned during the step into the world.
  void flushIncoming();
  /// Draws sky, medium, surface line and ground.
  void renderBackground(Frame& out, Style style, const Light& light) const;
  /// Draws the sun or the moon on its arc across the sky.
  void renderCelestial(Frame& out, Style style, const Light& light) const;

  /// Not owned; must outlive the world.
  const AssetPack* pack_;
  /// The scene being simulated.
  std::shared_ptr<const SceneDef> scene_;
  /// Size in cells.
  int w_, h_;
  /// Layout rows: the surface line and the first ground row.
  int surfaceY_ = 0, seabedY_ = 0;
  /// River band: first row and number of rows (rows is 0 without a river).
  int riverTopY_ = 0, riverRows_ = 0;
  /// Seconds of rain left; 0 when it is dry.
  double rainLeft_ = 0;
  /// Cloud cover from 0 to 1; follows the rain with a delay.
  double overcast_ = 0;
  /// Time of day as a fraction of a day.
  double phase_ = 0.4;
  /// Whether the time of day runs or stands still.
  TimeMode timeMode_ = TimeMode::Cycle;
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
  size_t eaten_ = 0, foodEaten_ = 0, destroyed_ = 0;
  std::vector<double> crater_;  ///< per column, in rows; shrinks as dust settles
  /// Entities in the world.
  std::vector<std::unique_ptr<Entity>> entities_;
  /// Entities spawned during the current step.
  std::vector<std::unique_ptr<Entity>> incoming_;
};

}  // namespace dio
