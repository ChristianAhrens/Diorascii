/// @file core/world.h
/// @brief The simulation: entities, behaviors, layout and rendering.

#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
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

/// Where on a mountain a thing lives (a sprite's `zone:` header). The ground zones are bands of
/// altitude whose limits wobble from column to column; the air zones are flying bands.
enum class Zone {
  None,     ///< no zone: placed by the sprite's habitat
  Meadow,   ///< the valley floor above the stream
  Forest,   ///< the belt of trees
  Woods,    ///< forest and meadow together
  Rock,     ///< bare rock above the tree line
  Snow,     ///< snow and ice above the snow line
  Alpine,   ///< rock and snow together
  Sky,      ///< the sky around the peaks
  Air,      ///< the air down to the rock, where eagles soar
  LowAir    ///< the air over the forest and the valley, where small birds fly
};

/// Which of a scene's rare catastrophes to send.
enum class RarePick {
  Weighted,  ///< any, by the weights in the scene file (the common one four times in five)
  Common,    ///< the one with the highest weight
  Other      ///< the one with the lowest weight: the scene's second catastrophe
};

/// A live thing in the world: a sprite with a position, motion and behaviors.
struct Entity {
  /// Unique within the world (0 until it has joined it); lets a tongue find its victim again.
  uint64_t id = 0;
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
  /// A living thing: catastrophes that wipe out life take it (animals have `life: yes`).
  bool life = false;
  /// A rare catastrophe (kraken, thunderstorm...): at most one exists at a time.
  bool isRare = false;
  /// For catastrophes: how far across the scene (0 to 1) it is when it strikes.
  double strikeAt = 0.5;
  /// For catastrophes: true once it has struck.
  bool struck = false;
  /// How a catastrophe strikes. Centre: once, all victims at the same moment, when its middle is
  /// the `strikeAt` way across (a storm). Sweep: each victim is taken as the middle of the
  /// attacker passes it (an alien beaming up things). Devour: each victim is taken as the front
  /// of the attacker reaches it (a sandworm).
  enum class StrikeMode { Centre, Sweep, Devour };
  /// How it strikes, see StrikeMode.
  StrikeMode strikeMode = StrikeMode::Centre;
  /// Sweep and devour: where its striking point was at the last step, and whether there was one.
  double lastPoint = 0;
  bool hasLastPoint = false;
  /// What a catastrophe wipes out when it strikes: "life", or a list of sprite names.
  std::string wipes = "life";
  /// Devour: how many cells it reaches with a tongue or tentacle at a victim (0 = none).
  double strikeReach = 0;
  /// False: it lashes out only at walking things ahead of its front (the sandworm's tongue). True:
  /// at any living thing around it, from anywhere along its underside (the kraken's tentacles).
  bool strikeAround = false;
  /// How many tongues or tentacles may be out at once.
  int strikeLashes = 1;
  /// Color of its tongue or tentacles.
  Rgb lashColor{232, 96, 128};
  /// True: its lashes are rifle shots (straight, quick, counted by World::shotCount()) instead of
  /// tongues or tentacles.
  bool lashShot = false;
  /// Top speed of a hunting catastrophe (the helicopter, the yeti) in cells per second.
  double cruise = 0;
  /// Seconds a hunting catastrophe has had nothing left to hunt; it leaves after a few.
  double idle = 0;
  /// Sprite shown on every victim of a strike.
  std::string strikeEffect = "flash";
  /// Sprite that hits the ground at a strike (a lightning bolt), or empty.
  std::string strikeSprite;
  /// Seconds the whole screen flashes at the strike (0 = no flash).
  double flashTime = 0;
  /// Seconds of rain a catastrophe brings with it (0 = none).
  double stormTime = 0;
  /// Brings back this entity after a catastrophe wiped it out (a replacement walks in, a planted
  /// thing is planted again).
  std::function<void(World&)> restock;
  /// True while a tornado has thrown it into the air.
  bool flung = false;
  /// The walking speed it gets back once it lands after being flung.
  double flungVx = 0;
  /// How many prey it has caught. A hunter stops chasing after its first kill.
  int kills = 0;
  /// Seconds it still has to stay put (eating after a kill, dazed after a landing).
  double pauseLeft = 0;
  /// How long it stays put after a kill (a feast).
  double feastTime = 0;
  /// Sprite shown where it makes a kill.
  std::string huntEffect = "splat";
  /// Picks its stroke color per cell from the background behind it (full style only): dark
  /// strokes on light backgrounds such as pale sand, the sprite's own color on dark ones.
  bool contrast = false;
  /// Darkens the background behind its glyphs by this share, 0 to 1 (full style only), so
  /// light sprites stay readable against both bright sky and dark trees.
  double backing = 0;
  /// When the entity is shown, in scenes with a day and night cycle.
  enum class Active { Always, Day, Night };
  /// Visible only by day or only by night (ignored in scenes without a cycle).
  Active active = Active::Always;
  /// Emits its own light: not dimmed at night (fireflies, sun, moon).
  bool glow = false;
  /// Sun height (0 to 1) below which the animal is asleep: frozen and not shown. 0 means it
  /// never sleeps. Chosen per animal, so a herd goes to bed and wakes up one after another.
  double sleepBelow = 0;
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
  /// Where it lives on a mountain, and for flyers the rows (top of the sprite) it may use.
  Zone zone = Zone::None;
  double rowLo = 0, rowHi = 0;
  /// Row a ledge walker is climbing to (negative = none), and whether it moves in hops.
  double laneTarget = -1;
  bool hops = false;
  /// Seconds alive, lifetime in seconds (negative means unlimited), and a
  /// random phase offset for bobbing.
  double age = 0, ttl = -1, phase = 0;
  /// Tuning for emit_bubbles (bubbles per second) and bob (amplitude in
  /// rows, frequency in radians per second).
  double bubbleRate = 0, bobAmp = 0, bobFreq = 1;
  /// Sprite row where it eats or bites.
  int mouthRow = 0;
  /// Puffs of dust kicked up behind its feet per second (emit_dust).
  double dustRate = 0;
  /// Tuning for the hop behavior: seconds per hop cycle, and hop height in rows.
  double hopPeriod = 1.2, hopHeight = 1.5;
  /// Marked for removal at the end of the step.
  bool dead = false;
  /// Vehicles of the city: 0 on the main road, 1 turning into a side street, 2 driving up it, 3 coming
  /// down it, 4 turning out onto the main road (see World::driveStep()).
  int driveState = 0;
  /// Which side street it turns into: -2 not decided yet, -1 none, else its index.
  int driveAvenue = -2;
  /// Chance (0 to 1) that the vehicle turns off into the next side street it comes to.
  double turnChance = 0;
  /// Direction (1 right, -1 left) and speed it takes up on the main road after coming down a side street.
  int driveDir = 1;
  double driveSpeed = 0;
  /// Bookkeeping of a turn: time spent, where it started, and the row it is heading for.
  double driveT = 0, driveX0 = 0, driveY0 = 0;
  /// Draw order it takes up again on the main road.
  int driveZ = 10;
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
  double haze = 0;   ///< how far things are tinted towards hazeColor (a sandstorm), 0 to 1
  Rgb hazeColor{0, 0, 0};  ///< color of the haze, already dimmed to the time of day
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
  /// Number of live entities made from the sprite with this name.
  size_t countSprite(const std::string& name) const;
  /// Number of live entities carrying the tag.
  size_t countTag(const std::string& tag) const;
  /// Prey caught by predators so far.
  size_t eatenCount() const { return eaten_; }          // prey caught by predators
  /// Food flakes eaten by fish so far.
  size_t foodEatenCount() const { return foodEaten_; }  // food flakes eaten by fish
  /// Ground objects wrecked by impacts so far.
  size_t destroyedCount() const { return destroyed_; } // ground objects wrecked by impacts
  /// Meteorite strikes so far (impactAt() calls); sound follows each one.
  unsigned impactCount() const { return impacts_; }
  /// First row of the river (equals height() when the scene has no river).
  int riverTop() const { return riverTopY_; }
  /// Number of river rows at the bottom of the ground (0 = no river).
  int riverRows() const { return riverRows_; }
  /// True while a rain shower (or a snowstorm) is going on.
  bool raining() const { return rainLeft_ > 0; }
  /// A side street that runs off into the city from the main street.
  struct Avenue {
    int cx = 0;       ///< center column
    int halfW = 6;    ///< half the width of the road at the main street
    int depth = 6;    ///< rows it runs up before it disappears behind the buildings
  };
  /// The side streets of the city.
  const std::vector<Avenue>& avenues() const { return avenues_; }
  /// Side streets seen between the buildings of the front row (cars do not drive there).
  const std::vector<Avenue>& glimpses() const { return glimpses_; }
  /// Moves a vehicle that may turn off the main road into a side street, or comes down one.
  /// Called by the `drive` behavior.
  void driveStep(Entity& e, double dt);
  /// True for a city scene.
  bool city() const { return scene_->city; }
  /// Number of skyscrapers in the skyline (0 in other scenes and where the screen is too small).
  size_t buildingCount() const { return city_.size(); }
  /// How fiercely a skyscraper burns right now, from 0 (no fire) to 1; sound follows it.
  double fireLevel() const { return fireLevel_; }
  /// Number of skyscrapers that are on fire or being renovated.
  size_t troubledBuildings() const;
  /// How busy the traffic is at this time of day, from about 0.06 at night to 1 in the morning (8:00)
  /// and evening (17:30) rush hours; 1 in scenes without a day and night cycle.
  double trafficLevel() const;
  /// Half the width in columns of the site that is cordoned off around a fire (at least 18, more
  /// when the fire has spread).
  double cordonHalfWidth() const { return cordonHalf_; }
  /// For tests: decides whether the next fire is beaten by the fire engine (false) or gets out of
  /// hand, spreads to 3-5 more buildings and burns them to the ground (true).
  void forceFireOutcome(bool fails) { forcedOutcome_ = fails ? 1 : 0; }
  /// Number of skyscrapers that have burnt down and are still ruins.
  size_t ruinedBuildings() const;
  /// True while some skyscraper is on fire.
  bool fireBurning() const;
  /// True while a fire is being fought: other vehicles and people keep away from the site.
  bool cordoned() const { return cordon_; }
  /// Column in the middle of the cordoned site.
  double cordonCenter() const { return cordonX_; }
  /// Drives a fire engine to the burning skyscraper, parks it in front, sprays water until the fire is
  /// out and drives on. Called by the `engine` behavior.
  void engineStep(Entity& e, double dt);
  /// Runs the fire that the `blaze` behavior stands for: sets a skyscraper alight, calls the fire
  /// engine, lets it burn out and renovates the building.
  void blazeStep(Entity& e, double dt);
  /// Row of the elevated railway, or -1 where there is none.
  int railRow() const { return railY_; }
  /// Top row (smallest) of the tallest skyscraper including its antenna, or the height if none.
  int skylineTop() const;
  /// True for a mountain scene.
  bool mountains() const { return scene_->mountains; }
  /// First row of the mountain at column x, where the sky ends (seabedY() without mountains).
  int ridgeRow(int x) const;
  /// What lies at column x, row y: a zone of the mountain, or Zone::None in the sky.
  Zone zoneAt(int x, int y) const;
  /// True if a sprite of the entity's size with its top at row `top` and its left edge at column
  /// `x` stands in the entity's zone with its feet in the zone and its head inside the mountain.
  bool fits(const Entity& e, double x, double top) const;
  /// Puts a zone sprite where it belongs: a random place of its zone, or at an edge to walk in.
  /// @param keepX keep the column the entity has
  /// @param enter start off screen on the side it comes from (`right`: it moves right)
  void placeInZone(Entity& e, bool keepX, bool enter, bool right);
  /// Picks a new row for a ledge walker at its column, if one fits, and sets it as its target.
  void pickLane(Entity& e);
  /// Draw order for a thing on the mountain whose feet are at this row: lower is nearer.
  int depthFor(double feetRow) const;
  /// How much of the avalanche's swath still covers the cell, from 0 to 1.
  double scarAt(int x, int y) const;
  /// How loud the avalanche is, from 0 (none) to 1 (it is thundering down); sound follows it.
  double avalancheLevel() const { return slideLevel_; }
  /// How strong the poison cloud of the leaking submarine is, from 0 (none) to 1; sound follows it.
  double toxinLevel() const { return toxinLevel_; }
  /// Drives the leaking submarine (the `leak` behavior): it drives through the sea and leaves a
  /// trail of poison behind it that spreads while it passes and lingers after it has gone.
  void leakStep(Entity& e, double dt);
  /// How loud the rush of the flood is, from 0 (none) to 1; sound follows it.
  double floodLevel() const { return floodLevel_; }
  /// How muddy column x is after a flood, 0 to 1.
  double mudAt(int x) const { return x >= 0 && static_cast<size_t>(x) < mud_.size() ? mud_[static_cast<size_t>(x)] : 0.0; }
  /// Drives the flood (the `flood` behavior): the river bursts its banks and a wall of water runs
  /// across the land, tearing off everything it reaches, then recedes and leaves mud behind.
  void floodStep(Entity& e, double dt);
  /// Moves an avalanche entity one step down the mountain, scarring what it passes and wiping out
  /// what it buries. Called by the `avalanche` behavior.
  void slideStep(Entity& e, double dt);
  /// How the time of day advances. Dawn, Day, Dusk and Night freeze it at a fixed moment; Realtime
  /// leaves it to the frontend, which sets it from the host's clock (see setPhase()), because the
  /// core has no clock of its own.
  enum class TimeMode { Cycle, Dawn, Day, Dusk, Night, Realtime };
  /// Number of TimeMode values.
  static constexpr int kTimeModes = 6;
  /// Runs the day and night cycle, or freezes the time of day. Without a cycle in
  /// the scene this changes nothing.
  void setTimeMode(TimeMode mode);
  /// Current time mode.
  TimeMode timeMode() const { return timeMode_; }
  /// Summons one of the scene's rare catastrophes now. Returns false if the scene has none or
  /// one is already under way.
  /// @param pick which one: a weighted pick (like the scheduled ones), the scene's most likely
  ///        catastrophe, or its least likely one (the second), so that either can be seen without
  ///        waiting for the chance
  bool triggerRare(RarePick pick = RarePick::Weighted);
  /// Wipes out entities: all life (kills == "life") or the sprites named in kills. Each victim
  /// shows `effect`, and is brought back some seconds later. The attacker itself is spared.
  /// @param from,to only victims whose middle column lies in this range are taken (default: all)
  /// @param groundedOnly spare what flies: only things that walk on the ground are taken
  /// @return how many were taken
  size_t wipeOut(const std::string& kills, const std::string& effect, const Entity* except,
                 double from = -1e9, double to = 1e9, bool groundedOnly = false);
  /// A devouring attacker (see Entity::strikeReach) lashes a tongue or tentacle out at the nearest
  /// victim in reach: it reaches the victim, which is crushed, and draws back. Up to
  /// Entity::strikeLashes at once, each at another victim.
  /// @return true if one was started
  bool lash(const Entity& attacker);
  /// Number of tongues and tentacles currently lashing out.
  size_t tongueCount() const { return tongues_.size(); }
  /// How many things devouring catastrophes (the sandworm, the kraken) have crushed so far; sound follows each.
  unsigned crushCount() const { return crushes_; }
  /// Counts things crushed by a devouring catastrophe. Called by the `catastrophe` behavior.
  void noteCrushed(size_t n) { crushes_ += static_cast<unsigned>(n); }
  /// How many animals an avalanche has buried so far; each lets out a muffled squeal.
  unsigned squealCount() const { return squeals_; }
  /// How many rifle shots the poachers have fired so far; sound follows each.
  unsigned shotCount() const { return shots_; }
  /// How close the poachers' helicopter is to the middle of the scene, 0 to 1; the rotor sound follows it.
  double rotorLevel() const { return nearness("chopper"); }
  /// Drives the poachers' helicopter (the `poach` behavior): it flies after the closest living
  /// thing, shoots at what is in reach and leaves when nothing is left or after a while.
  void poachStep(Entity& e, double dt);
  /// How many entities catastrophes have wiped out so far.
  size_t killedCount() const { return killed_; }
  /// How many wiped-out entities are still waiting to be brought back.
  size_t restockPending() const { return restock_.size(); }
  /// Starts a screen flash of the given length (lightning).
  void flash(double seconds);
  /// How many flashes have been started so far; sound follows each one as thunder.
  unsigned flashCount() const { return flashCount_; }
  /// True while the screen is lit up by a flash (two quick flashes, then dark again).
  bool flashing() const;
  /// Makes it rain for this many seconds, if the scene's action is a weather action.
  void startRain(double seconds);
  /// Sets the time of day as a fraction of a day: 0 is midnight, 0.25 sunrise,
  /// 0.5 noon and 0.75 sunset.
  void setPhase(double phase);
  /// Sets the time of day in hours, 0 to 24.
  void setHour(double hour) { setPhase(hour / 24.0); }
  /// Time of day as a fraction of a day, see setPhase().
  double phase() const { return phase_; }
  /// True if the scene has a day and night cycle.
  bool hasDayNight() const { return scene_->dayNight; }
  /// True if the entity is asleep right now: it sleeps (see Entity::sleepBelow), the scene has a day
  /// and night cycle and the sun is low.
  bool isAsleep(const Entity& e) const;
  /// Current light levels (everything at full day in scenes without a cycle).
  Light light() const;
  /// Time of day as hours and minutes, for example 14:30.
  std::string clockText() const;
  /// How close a "gloom" creature (a thundercloud) is to the middle of the scene, from 0 (far away
  /// or absent) to 1 (overhead). It starts to count a little before the creature comes into view.
  double stormProximity() const;
  /// Like stormProximity() for any creature whose tag or sprite name is `key` (a whale, a tornado).
  double nearness(const std::string& key) const;
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
  /// Where and how a creature starts, for members of a herd.
  struct Placement {
    int dir = 0;           ///< 1 = moves right, -1 = moves left, 0 = random
    bool hasX = false;     ///< true if x is given; otherwise it starts anywhere or just off screen
    double x = 0;          ///< starting column
    double speedMul = 1;   ///< multiplies the speed picked from the sprite header
  };

  /// Spawns an entity from a sprite according to its header (speed, depth,
  /// habitat, behaviors).
  /// @param initial true while populating: place it anywhere instead of just off screen
  /// @param event true if it is a scheduled visitor
  /// @param where start position and direction, for members of a herd (nullptr = random)
  void spawnCreature(const std::shared_ptr<const Sprite>& sprite, bool initial, bool event = false,
                     const Placement* where = nullptr);
  /// Spawns an entity at an exact position with an exact velocity (bubbles,
  /// debris, food...).
  /// @return the new entity (valid until the end of the step), or nullptr if the sprite is unknown
  Entity* spawnEffect(const std::string& spriteName, double x, double y, int z, double vx, double vy);
  /// Counts a catch. Called by predator behaviors.
  void noteEaten() { ++eaten_; }
  /// Counts an eaten food flake. Called by feeding behaviors.
  void noteFoodEaten() { ++foodEaten_; }

 private:
  /// A skyscraper of the city's skyline.
  struct Building {
    int x = 0, w = 8, h = 10;  ///< left column, width, and height in rows above the street
    int layer = 0;             ///< 0 = far (pale), 1 = middle, 2 = front
    int facade = 0;            ///< 0 grid of windows, 1 glass curtain wall, 2 ribbons, 3 small windows
    int roof = 0;              ///< 0 flat, 1 antenna, 2 stepped, 3 crown, 4 water tower, 5 helipad
    int antenna = 0;           ///< rows of antenna above the roof
    int tint = 0;              ///< index of its wall color
    uint32_t seed = 0;         ///< decides its window pattern
    /// Normal, on fire (a catastrophe), burnt down to a ruin, or wrapped in scaffolding while it is
    /// renovated (or rebuilt, if it burnt down).
    enum class State { Normal, Burning, Ruin, Scaffold };
    State state = State::Normal;
    bool ruined = false;       ///< it burnt down and is being rebuilt from the rubble
    double timer = 0;          ///< seconds in the state
  };
  /// Chooses where the side streets go, clear of the shops and trees on the sidewalk.
  void placeAvenues();
  /// Lets cars come down the side streets now and then.
  void stepCity(double dt);
  /// Builds the skyline and decides where the railway runs.
  void buildCity();
  /// Gives a skyscraper new looks and a new height (the first time, and after a renovation).
  void rollBuilding(Building& b);
  /// Draws the skyline and the side streets (behind everything that moves in the city, except what
  /// drives up and down the side streets).
  void renderCityBack(Frame& out, Style style, const Light& light) const;
  /// Draws the skyscrapers.
  void renderSkyline(Frame& out, Style style, const Light& light) const;
  /// Paints a side street over what is behind it: a road that narrows a little as it runs back
  /// between two sidewalk edges, hazier the further it goes.
  void paintAvenue(Frame& out, const Avenue& a, Style style, const Light& light) const;
  /// Draws the viaduct, the street rows and the street lamps (in front of the side streets and what
  /// drives on them, behind the shops and the traffic of the main street).
  void renderStreet(Frame& out, Style style, const Light& light) const;
  /// Spawns the scene population and decorations.
  void populate();
  /// Builds the mountain range: the skyline, the tree line and the snow line of every column.
  void buildTerrain();
  /// Takes an entity out of the world after a catastrophe and brings it back some seconds later.
  void claim(Entity& o, const std::string& effect);
  /// Places one decoration.
  void spawnDecor(const SceneDef::Decor& d);
  /// Grows one procedural seaweed plant.
  void spawnSeaweed();
  /// Sends one weighted-random scene event.
  void spawnEvent();
  /// Sends one rare catastrophe, see RarePick.
  void spawnRare(RarePick pick = RarePick::Weighted);
  /// Number of cells in a habitat, used for per-cells populations.
  double areaFor(const std::string& habitat) const;
  /// Picks a starting row for a sprite of the given height in its habitat.
  double habitatY(const Props& p, int spriteHeight);
  /// Half the width of a crater in columns.
  int craterHalfWidth() const;
  /// Starts a herd: several members that move together in one direction.
  void spawnHerd(const std::shared_ptr<const Sprite>& sprite);
  /// Lets rain drops fall, or wind carry sand across the scene, for this step.
  void spawnWeather(double dt);
  /// Moves the tongues, crushes what they reach and removes the ones that are done.
  void stepTongues(double dt);
  /// Draws the tongues lashing out of devouring attackers.
  void renderTongues(Frame& out, Style style, const Light& light) const;
  /// The entity with this id, or nullptr.
  Entity* findEntity(uint64_t id);
  /// Moves entities spawned during the step into the world.
  void flushIncoming();
  /// Draws sky, medium, surface line and ground.
  void renderBackground(Frame& out, Style style, const Light& light) const;
  /// Draws the mountain range over the sky: meadow, forest, rock, snow, the stream and the swath
  /// of an avalanche.
  void renderTerrain(Frame& out, Style style, const Light& light) const;
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
  /// Mountains: first terrain row, tree line row and snow line row of every column, and which
  /// side of its peak the column is on (-1 left, 1 right: the way the rock is streaked).
  std::vector<int> ridge_, treeRow_, snowRow_;
  std::vector<signed char> side_;
  /// Lowest skyline row, where the sun sets behind the foothills.
  int horizonY_ = 0;
  /// Rows (feet) a zone spans: index by Zone.
  int zoneLo_[10] = {}, zoneHi_[10] = {};
  /// The skyline of a city scene, back layer first, and the row of the railway (-1 = none).
  std::vector<Building> city_;
  int railY_ = -1;
  /// The side streets, and the seconds until the next car comes down each of them.
  std::vector<Avenue> avenues_;
  std::vector<double> avenueTimer_;
  /// Other side streets, seen between the buildings of the front row (cars do not drive there).
  std::vector<Avenue> glimpses_;
  /// The fire under way: which building, and how fiercely it burns.
  /// The buildings of the fire under way (the first is where it started), whether it gets out of hand,
  /// how many more buildings it will spread to, and when it spreads next.
  std::vector<int> blazeBuildings_;
  int blazeOrigin_ = -1;
  bool blazeFails_ = false;
  int forcedOutcome_ = -1;  ///< -1 left to chance, 0 beaten, 1 out of hand (tests)
  int spreadLeft_ = 0;
  double blazeClock_ = 0, nextSpread_ = 0, cordonHalf_ = 18.0;
  /// Moves the cordon to cover every building of the fire.
  void fitCordon();
  /// The site of the fire is cordoned off from the arrival of the fire engine until the building is
  /// renovated: its middle column, and seconds of water still being sprayed.
  bool cordon_ = false;
  double cordonX_ = 0, sprayTime_ = 0;
  /// Puts barriers at both ends of the cordoned site, and takes them away again.
  void raiseBarriers();
  void liftBarriers();
  double fireLevel_ = 0;
  double smokePuffs_ = 0;
  /// Fading trace of the avalanche over every cell, row by row.
  std::vector<float> scar_;
  /// The avalanche under way: where it starts, where it is heading, how far down it has come.
  struct Slide {
    bool active = false;
    double x0 = 0, y0 = 0, dir = 1, gradient = 1, wobble = 0;
    double head = 0, lastHead = 0;
    double endedAt = -1;  ///< age of the avalanche entity when its head reached the bottom
    double puffs = 0;
  } slide_;
  /// A tongue lashing out: it reaches for the victim, crushes it on arrival and draws back.
  struct Tongue {
    uint64_t attacker = 0, victim = 0;  ///< entity ids
    double age = 0;
    double reach = 0.28, dur = 0.6;     ///< seconds to reach the victim, and to be done altogether
    bool hit = false;                   ///< the victim has been crushed
    double ox = 0;                      ///< where it starts, in columns from the attacker's left edge
    Rgb color{232, 96, 128};
    bool shot = false;                  ///< a rifle shot rather than a tongue
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  ///< the base and the tip, in cells
    std::string effect;                 ///< shown on the victim when it is crushed
    std::string wipes;
  };
  /// Tongues lashing out now, and when the next one may start.
  std::vector<Tongue> tongues_;
  double nextLash_ = 0;
  /// The id given to the next entity that joins the world.
  uint64_t nextId_ = 0;
  /// Counters behind crushCount() and squealCount().
  unsigned crushes_ = 0, squeals_ = 0, shots_ = 0;
  /// How loud the avalanche is right now.
  double slideLevel_ = 0;
  /// The poison trail of the submarine: a puff is left behind it every column or so, and every
  /// puff spreads (round, as cells go) until the trail fills the height of the sea. It lingers
  /// once the submarine has gone and then thins out; it kills what it covers.
  struct Toxin {
    bool active = false;      ///< a trail exists
    bool sourceGone = false;  ///< the submarine has left the screen
    double age = 0;           ///< seconds since the first puff
    double gone = 0;          ///< seconds since the submarine left
    double reach = 0;         ///< radius at which a puff covers the sea from top to bottom
    double strength = 0;      ///< 0 to 1: thick while it lingers, thin again as it fades
    /// One puff: where it was left, and when (in `age`).
    struct Puff {
      double x = 0, y = 0, born = 0;
    };
    std::vector<Puff> puffs;  ///< in the order they were left
  } toxin_;
  /// How far the puff has spread by now.
  double puffRadius(const Toxin::Puff& p) const;
  /// Strength of the cloud for the sound.
  double toxinLevel_ = 0;
  /// The flood: a wall of water runs in from one side, stands over the land for a while and then
  /// recedes.
  struct Flood {
    bool active = false;   ///< a flood is under way
    bool started = false;  ///< it has begun (the entity that stands for it may leave after it)
    int dir = 1;           ///< side it comes from: 1 from the left, -1 from the right
    double age = 0;        ///< seconds since it began
    double depthMax = 0;   ///< depth of the water at its highest, in rows
    double recede = 0;     ///< how far the water has withdrawn, 0 to 1
  } flood_;
  /// How loud the flood is right now.
  double floodLevel_ = 0;
  /// How muddy each column is (1 right after the water has been over it, fading in time).
  std::vector<float> mud_;
  /// Depth of the flood water at column x, in rows from the bottom of the screen.
  double floodDepth(int x) const;
  /// Advances the flood, tears off what it reaches and lets the mud dry.
  void stepFlood(double dt);
  /// Draws the flood water over the land.
  void renderFlood(Frame& out, Style style, const Light& light) const;
  /// Spreads and fades the poison cloud, and kills what it covers.
  void stepToxin(double dt);
  /// Tints the sea where the poison cloud lies.
  void renderToxin(Frame& out, Style style, const Light& light) const;
  /// Seconds until the next rare event; negative means none is scheduled yet.
  double rareTimer_ = 0;
  /// A wiped-out entity waiting to be brought back.
  struct Restock {
    double left = 0;                  ///< seconds until it comes back
    std::function<void(World&)> fn;   ///< brings it back
  };
  /// Everything that is waiting to come back.
  std::vector<Restock> restock_;
  /// Length of the current screen flash and the time since it began.
  double flashLen_ = 0, flashElapsed_ = 0;
  /// Flashes started so far.
  unsigned flashCount_ = 0;
  /// How many entities catastrophes have wiped out.
  size_t killed_ = 0;
  /// Seconds of rain or sandstorm left; 0 when it is calm.
  double rainLeft_ = 0;
  /// Direction a sandstorm blows: 1 = towards the right, -1 = towards the left.
  int stormDir_ = 1;
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
  /// Meteorite strikes so far.
  unsigned impacts_ = 0;
  std::vector<double> crater_;  ///< per column, in rows; shrinks as dust settles
  /// Entities in the world.
  std::vector<std::unique_ptr<Entity>> entities_;
  /// Entities spawned during the current step.
  std::vector<std::unique_ptr<Entity>> incoming_;
};

}  // namespace dio
