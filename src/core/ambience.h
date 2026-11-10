/// @file core/ambience.h
/// @brief Procedurally synthesized ambient sound for a scene.

#pragma once
#include <cstdint>

#include "core/rng.h"

namespace dio {

class World;

/// What a scene sounds like.
enum class AmbientKind { Silent, Forest, Aquarium, Desert, Moon, Mountains, City };
/// Number of AmbientKind values.
inline constexpr int kAmbientKinds = 7;

/// What the sound should reflect right now. All levels run from 0 to 1.
/// Every field must also be handed on by the audio backends (src/audio/audio.cpp and
/// audio_apple.mm copy them one by one); a field missing there is silent in the real program
/// although the synthesis tests pass.
struct AmbientParams {
  AmbientKind kind = AmbientKind::Silent;
  float night = 0;   ///< how dark it is
  float rain = 0;    ///< how hard it rains (or a sandstorm blows)
  float volume = 1;  ///< master volume; 0 mutes
  /// Counts lightning flashes (World::flashCount()). Every increase makes thunder roll in
  /// a moment later, as sound travels slower than light.
  unsigned flashes = 0;
  /// How near a thundercloud is, from 0 to 1 (World::stormProximity()): distant rumbling and
  /// far-off claps start as it moves in, before the first strike.
  float storm = 0;
  /// Counts fish nibbling at food (World::foodEatenCount()); each increase is a small blip.
  unsigned nibbles = 0;
  /// How near a whale is, from 0 to 1 (World::nearness("whale")): it sings as it passes.
  float whale = 0;
  /// How near a tornado is, from 0 to 1 (World::nearness("tornado")).
  float tornado = 0;
  /// Counts meteorite strikes (World::impactCount()); each one thuds through the ground.
  unsigned impacts = 0;
  /// How loud an avalanche is, from 0 to 1 (World::avalancheLevel()): it cracks, rumbles and roars.
  float avalanche = 0;
  /// How near a sandworm is, from 0 to 1 (World::nearness("sandworm")): it bellows as it crosses.
  float worm = 0;
  /// Counts things crushed by the sandworm or the kraken (World::crushCount()); in the desert each
  /// one is chewed, in the aquarium it is gulped down.
  unsigned crushes = 0;
  /// How near a siren is, from 0 to 1 (World::nearness("siren"): the ambulance, the fire engine).
  float siren = 0;
  /// How near a fire engine with its siren on is, from 0 to 1 (World::nearness("wail")): a slow, wailing
  /// siren, unlike the ambulance's quick two-tone one.
  float wail = 0;
  /// How near a train is, from 0 to 1 (World::nearness("train")): it rumbles, clatters and sounds its horn.
  float train = 0;
  /// How near a plane is, from 0 to 1 (World::nearness("plane")): a faint, distant jet.
  float plane = 0;
  /// How fiercely a building burns, from 0 to 1 (World::fireLevel()): it crackles and roars.
  float fire = 0;
  /// Counts animals buried by an avalanche (World::squealCount()); each one is a muffled squeal.
  unsigned squeals = 0;
  /// How strong the scene's second catastrophe is, from 0 to 1 (the largest of World::toxinLevel(),
  /// floodLevel(), rotorLevel(), yetiLevel(), titanLevel() and asteroidLevel(); only the current scene's is not
  /// zero): the leaking submarine's hissing gas, the flood's rush, the poachers' helicopter, the
  /// yeti's roars and stamping, the monsters' roars and stamping, the rumble of the big meteor coming down.
  float menace = 0;
  /// Counts the poachers' rifle shots (World::shotCount()); each one cracks.
  unsigned shots = 0;
};

/// Derives the sound parameters from the state of a world (volume stays at 1).
AmbientParams ambientFor(const World& world);

/// Generates ambient sound: wind, rain, thunder and crickets at night in the forest, the deep
/// hum, bubbles and passing whales of the aquarium, the dry wind, sandstorms and tornadoes of
/// the desert, the wind, stream and avalanches of the mountains. Pure
/// computation without I/O, deterministic per seed, so it is testable without a sound device.
/// Parameter changes are smoothed, so switching scenes or muting never clicks.
class Ambience {
 public:
  /// @param sampleRate output rate in Hz
  /// @param seed seeds the noise
  explicit Ambience(int sampleRate, uint64_t seed = 1);
  /// Writes `frames` frames of interleaved stereo float samples, each within -1 to 1.
  void render(const AmbientParams& p, float* out, int frames);

 private:
  /// Moves `v` towards `target`; `k` is the per-sample smoothing coefficient.
  static void approach(float& v, float target, float k) { v += (target - v) * k; }
  /// One-pole smoothing coefficient for a time constant in seconds.
  float coeff(float seconds) const;

  /// One clap of thunder: when it starts, how loud it is, how far its rumbling swells reach.
  struct Thunder {
    double start = 0, gain = 0;
    bool strike = false;  ///< lightning overhead: adds a sharp, crackling snap
    double swellAt[3] = {}, swellGain[3] = {};
  };
  /// Schedules a clap `delay` seconds from now with the given loudness (0 to 1).
  void clap(double delay, double gain, bool strike = false);
  /// A rising bubble blip, `delay` seconds from now. `f0` is its starting pitch in Hz.
  void bubble(double delay, float f0, float dur, float amp);
  /// One sample of the aquarium's sound for both ears.
  void aquariumSample(const AmbientParams& p, float out[2]);
  /// One sample of the desert's sound for both ears.
  void desertSample(const AmbientParams& p, float out[2]);
  /// One sample of the moon's sound for both ears.
  void moonSample(const AmbientParams& p, float out[2]);
  /// One sample of the city's sound for both ears.
  void citySample(const AmbientParams& p, float out[2]);
  /// One sample of the mountains' sound for both ears.
  void mountainSample(const AmbientParams& p, float out[2]);
  /// One sample of the second catastrophe of the current scene, for both ears (silent without one).
  void menaceSample(const AmbientParams& p, float out[2]);
  /// Thunder envelopes at the current time, summed over all claps: a sharp crack and a rumble.
  void thunderLevels(float& crack, float& rumble, float& snap) const;

  int rate_;
  float noiseScale_;  ///< keeps the noise density per Hz the same at every sample rate
  Rng rng_;
  double t_ = 0;  ///< seconds rendered so far
  float mix_[kAmbientKinds] = {};  ///< how present each kind of scene is; they crossfade
  float rain_ = 0, night_ = 0, volume_ = 0;  ///< smoothed parameters
  float gust_ = 0;  ///< current gust strength, 0 to 1, shared by the wind-like sounds
  float windLow_[2][2] = {};  ///< two low-pass stages of the wind, per channel
  double gustPhaseA_ = 0, gustPhaseB_ = 0;  ///< phases of the two slow gust swells (they speed up with rain)
  float wander_ = 0;          ///< slow random drift that keeps gusts irregular
  float rainLow_[2] = {};
  float snapLow_[2] = {}, snapBand_[2] = {};  ///< resonator that the crack's clicks ring, per channel
  float crackLow_[2] = {}, rumbleLow_[2][2] = {};  ///< thunder filter state, per channel
  Thunder thunder_[3];                              ///< claps under way or waiting (start 0 = free)
  float storm_ = 0, roll_ = 0;  ///< smoothed storm proximity; slow random roll of the distant rumble
  double nextFarClap_ = 0;      ///< when the next far-off clap comes (0 = none planned)
  unsigned seenFlashes_ = 0;
  bool flashesSynced_ = false;  ///< the first counter value seen is the starting point, not lightning

  /// A bubble blip under way or waiting (start 0 = free).
  struct Bubble {
    double start = 0;
    float f0 = 0, dur = 0.1f, amp = 0, pan = 0.5f;
  };
  Bubble bubbles_[10];
  float lowHum_, lowMuffle_, lowDeep_;              ///< aquarium filter coefficients
  double nextBubble_ = 0;                           ///< when the next random bubble rises
  float waterLow_[2][2] = {}, muffleLow_[2] = {};   ///< the deep hum and the muffled flow, per ear
  float deepLow_[2][2] = {};                        ///< the growl of something huge, per ear
  float whale_ = 0;                                 ///< smoothed whale proximity
  double nextSong_ = 0;                             ///< when the whale sings next (0 = not planned)
  /// A whale phrase: a long, gliding moan with a faint overtone.
  struct Song {
    double start = 0, dur = 0;
    float f0 = 100, pan = 0.5f;
    double phase = 0;
  } song_;
  unsigned seenNibbles_ = 0;
  bool nibblesSynced_ = false;

  float desertLow_[2][2] = {};  ///< two low-pass stages of the desert wind, per ear
  float sandLow_[2][2] = {};    ///< band limits of the hiss of blown sand (low cut, high cut), per ear
  float tornadoLow_[2][2] = {}; ///< the roar of a tornado, per ear
  float tornado_ = 0;           ///< smoothed tornado proximity
  double tornadoPhase_ = 0;     ///< phase of the roar's rotation
  float lowSandCut_, lowSandTop_, lowTornado_;  ///< desert filter coefficients

  /// A meteorite strike heard through the ground: when it began and how hard it hit.
  struct Impact {
    double start = 0;
    float gain = 0;
  };
  Impact impacts_[3];
  double zapStart_ = 0;         ///< when the alien's warble began (0 = none yet)
  double zapPhase_ = 0;         ///< phase of the warble
  float moonLow_[2][2] = {};    ///< the dull rumble of an impact, per ear
  float debrisBand_[2] = {}, debrisLow_[2] = {};  ///< resonator that rings the pattering debris, per ear
  float lowMoon_;               ///< moon filter coefficient

  float mountLow_[2][2] = {};   ///< two low-pass stages of the thin mountain wind, per ear
  float iceLow_[2] = {};        ///< low end of the hiss of ice crystals, per ear
  float streamLow_[2][2] = {};  ///< band limits of the stream (high cut, low cut), per ear
  float slideLow_[2][2] = {};   ///< the deep rumble of an avalanche, per ear
  float slideMid_[2] = {};      ///< the roar and crackle of tumbling snow and rock, per ear
  float avalanche_ = 0;         ///< smoothed avalanche level
  double whistlePhase_ = 0;     ///< phase of the whistle of an icy wind
  float crackLowAv_[2] = {}, crackBandAv_[2] = {};  ///< resonator that the clicks of tumbling rock ring, per ear
  float breakEnv_ = 0;          ///< how hard the slab is still breaking up (crackling), 0 to 1
  bool slideBroke_ = false;     ///< the current avalanche has already broken loose
  float thumpEnv_ = 0, thumpFreq_ = 80;  ///< a tree or boulder knocked down: a dull thump
  double thumpPhase_ = 0;
  double nextCry_ = 0;          ///< when an eagle cries next (0 = not planned)
  double cryStart_ = -10;       ///< when the current eagle cry began
  float cryPan_ = 0.5f, cryPitch_ = 1;
  double cryPhase_ = 0;
  double nextPeck_ = 0;         ///< when a woodpecker drums next (0 = not planned)
  double peckStart_ = -10;      ///< when the current drumming began
  int peckKnocks_ = 0;          ///< how many knocks it has
  float peckPan_ = 0.5f, peckPitch_ = 1;
  float lowSlide_, lowSlideMid_, lowStreamTop_, lowStreamBottom_, lowIce_;  ///< mountain filter coefficients
  float wormNear_ = 0;                ///< smoothed sandworm proximity
  double nextRoar_ = 0;               ///< when the worm bellows next (0 = not planned)
  double roarStart_ = -10, roarDur_ = 2.4;  ///< when the current bellow began, and how long it lasts
  double roarPhase_ = 0;              ///< phase of the bellow's tone
  float roarLow_[2] = {};             ///< two low-pass stages that keep the bellow deep
  float lowRoar_;                     ///< filter coefficient of the bellow
  /// Chewing, as the worm swallows something: a series of soft, grinding bites. `bite` holds when
  /// each one starts (seconds after `start`) and how hard it is.
  struct Crunch {
    static constexpr int kBites = 9;
    double start = 0;
    float gain = 0;
    int bites = 0;
    float bite[kBites] = {}, force[kBites] = {};
  };
  Crunch crunches_[4];
  float crunchLow_[2][2] = {};        ///< two low-pass stages of the crunch, per ear
  float lowCrunch_;                   ///< filter coefficient of the crunch
  /// A muffled gulp of the kraken swallowing something: two glugs in a row.
  struct Gulp {
    double start = 0;
    float pitch = 1;
    bool used = false;
  };
  Gulp gulps_[4];
  double gulpPhase_ = 0;              ///< phase of the glug's tone
  float gulpLow_[2] = {};             ///< two low-pass stages that muffle the gulp
  float lowGulp_;                     ///< filter coefficient of the gulp
  unsigned seenCrushes_ = 0;
  bool crushesSynced_ = false;
  /// A muffled squeal of an animal in an avalanche.
  struct Squeal {
    double start = 0, phase = 0;
    float pitch = 1;
    bool used = false;
  };
  Squeal squealVoices_[3];
  float squealLow_[2] = {};           ///< two low-pass stages that muffle the squeal
  float lowSqueal_;                   ///< filter coefficient of the squeal
  unsigned seenSqueals_ = 0;
  bool squealsSynced_ = false;
  /// A car passing by: a swell of engine noise that sinks in pitch, panned across.
  struct Pass {
    double start = 0, dur = 1, phase = 0;
    float pitch = 90, pan = 0.5f, gain = 0;
  };
  Pass passes_[4];
  double nextPass_ = 0;             ///< when the next car passes (0 = not planned)
  float carLow_[2][2] = {};         ///< traffic rumble, two low-pass stages per ear
  float carMid_[2][2] = {};         ///< the hiss of tyres: band limits per ear
  float engineLow_[2] = {};         ///< low-pass over the passing engines, per ear
  float lowCar_, lowCarMid_, lowCarTop_, lowEngine_;  ///< city filter coefficients
  /// A car horn: one or two honks.
  struct Honk {
    double start = 0, phaseA = 0, phaseB = 0;
    float pitch = 1, pan = 0.5f, gain = 0, long_ = 0;  ///< long_: 0 = short honks, 1 = one long blast
    int count = 1;
  };
  Honk honks_[3];
  double nextHonk_ = 0;             ///< when the next horn sounds (0 = not planned)
  float hornLow_[2] = {};           ///< takes the edge off the horns, per ear
  float lowHorn_;                   ///< filter coefficient of the horns
  float sirenNear_ = 0, sirenFreq_ = 700;  ///< smoothed siren proximity, and the pitch it glides to
  double sirenPhase_ = 0;
  float wailNear_ = 0;              ///< smoothed proximity of the fire engine's wail
  double wailPhase_ = 0;            ///< phase of the wail's tone
  float trainNear_ = 0, planeNear_ = 0, fireNear_ = 0;  ///< smoothed proximity of train and plane, fire level
  double trainClock_ = 0;           ///< seconds into the current "klack-klack" of the wheels
  static constexpr double kKlackPeriod = 0.64;  ///< seconds from one "klack-klack" to the next
  double trainHornStart_ = -10, trainHornPhase_ = 0;  ///< the train's horn
  bool trainHorned_ = false;        ///< this train has sounded its horn already
  float trainLow_[2][2] = {}, trainBand_[2][2] = {};  ///< rumble and clatter filters, per ear
  float fireLow_[2][2] = {}, fireCrack_[2] = {}, planeLow_[2][2] = {};  ///< fire and jet filters
  float lowTrain_, lowTrainBand_, lowKlack_, lowFire_, lowJet_;  ///< their coefficients
  float menace_ = 0;                ///< smoothed strength of the second catastrophe
  double menacePhase_[2] = {};      ///< phases of its tones (the roar, the whine)
  float menaceLow_[2][2] = {};      ///< low-pass stages of its noise, per ear
  float menaceHigh_[2] = {};        ///< what the hiss and the rush take away, per ear
  float lowHiss_, lowThump_;        ///< coefficients of the hiss and of the low stages
  float lowFlood_, lowFloodTop_;    ///< coefficients of the flood's rush
  float lowChop_;                   ///< coefficient of the rotor's chop
  double nextMenaceRoar_ = 0;       ///< when the creature roars next (0 = not planned)
  double menaceRoarStart_ = -10, menaceRoarDur_ = 1.5;  ///< the current roar
  int menaceRoarVoice_ = 0;         ///< 0 the low growl, 1 the shrill cry
  /// A rifle shot: a crack with a thump.
  struct Shot {
    double start = -10;
    float pan = 0.5f;
  };
  Shot shotVoices_[4];
  unsigned seenShots_ = 0;
  bool shotsSynced_ = false;
  unsigned seenImpacts_ = 0;
  bool impactsSynced_ = false;
};

}  // namespace dio
