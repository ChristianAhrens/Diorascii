/// @file core/ambience.h
/// @brief Procedurally synthesized ambient sound for a scene.

#pragma once
#include <cstdint>

#include "core/rng.h"

namespace dio {

class World;

/// What a scene sounds like.
enum class AmbientKind { Silent, Forest };

/// What the sound should reflect right now. All levels run from 0 to 1.
struct AmbientParams {
  AmbientKind kind = AmbientKind::Silent;
  float night = 0;   ///< how dark it is
  float rain = 0;    ///< how hard it rains
  float volume = 1;  ///< master volume; 0 mutes
  /// Counts lightning flashes (World::flashCount()). Every increase makes thunder roll in
  /// a moment later, as sound travels slower than light.
  unsigned flashes = 0;
  /// How near a thundercloud is, from 0 to 1 (World::stormProximity()): distant rumbling and
  /// far-off claps start as it moves in, before the first strike.
  float storm = 0;
};

/// Derives the sound parameters from the state of a world (volume stays at 1).
AmbientParams ambientFor(const World& world);

/// Generates ambient sound: wind, rain, thunder and crickets at night in the forest. Pure
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
  /// Thunder envelopes at the current time, summed over all claps: a sharp crack and a rumble.
  void thunderLevels(float& crack, float& rumble, float& snap) const;

  int rate_;
  float noiseScale_;  ///< keeps the noise density per Hz the same at every sample rate
  Rng rng_;
  double t_ = 0;  ///< seconds rendered so far
  float presence_ = 0, rain_ = 0, night_ = 0, volume_ = 0;  ///< smoothed parameters
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
  bool flashesSynced_ = false;  ///< the first counter value seen is the starting point, not lightning     ///< low-pass state that splits the rain hiss from its rumble
};

}  // namespace dio
