/// @file core/ambience.h
/// @brief Procedurally synthesized ambient sound for a scene.

#pragma once
#include <cstdint>

#include "core/rng.h"

namespace dio {

class World;

/// What a scene sounds like.
enum class AmbientKind { Silent, Forest, Aquarium, Desert, Moon, Mountains };
/// Number of AmbientKind values.
inline constexpr int kAmbientKinds = 6;

/// What the sound should reflect right now. All levels run from 0 to 1.
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
  /// One sample of the mountains' sound for both ears.
  void mountainSample(const AmbientParams& p, float out[2]);
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
  unsigned seenImpacts_ = 0;
  bool impactsSynced_ = false;
};

}  // namespace dio
