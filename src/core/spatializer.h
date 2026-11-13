/// @file core/spatializer.h
/// @brief Places a mono sound in a speaker layout: panning gains, spread, bass send, smoothing.

#pragma once
#include "core/layout.h"

namespace dio {

/// Where a sound is and how it is spread. Angles in degrees; non-finite values count as 0.
struct Source {
  float azimuth = 0;    ///< from straight ahead, positive to the right
  float elevation = 0;  ///< above the horizon
  float spread = 0;     ///< 0 a point, 1 fills the whole room evenly
  float lfeSend = 0;    ///< 0 to 1, how much of the sound (low-passed) also goes to the bass channel
};

/// Tuning of the panning law. Single constants so that they can be adjusted by ear.
namespace spatial {
/// Exponent of the distance roll-off between a source and a speaker (1 = 6 dB per doubling,
/// 2 = 12 dB). Larger values localize more sharply.
inline constexpr float kRolloff = 1.5f;
/// Blur (a distance on the unit sphere) that keeps the gain of a speaker finite when a source is
/// exactly at it, and widens the image a little.
inline constexpr float kBlur = 0.15f;
/// Time constant in seconds of the gain smoothing; about 20 ms never zippers or clicks.
inline constexpr float kSmoothingSeconds = 0.02f;
/// Corner frequency in Hz of the bass send's low-pass (two one-pole stages).
inline constexpr float kLfeCutoffHz = 120.0f;
}  // namespace spatial

/// Panning gains for one source in one layout.
class Spatializer {
 public:
  /// Computes the target gain of every channel for a source.
  ///
  /// For a stereo layout this is exactly the constant-power law `cos/sin(pan * pi/2)` the synth
  /// has always used, with `pan = 0.5 + 0.5 * sin(azimuth)`. Any other layout uses
  /// distance-based amplitude panning (DBAP) on the unit sphere, which needs no triangulation and
  /// works for any speaker set: a layout without height speakers simply puts a high source on the
  /// speakers nearest to it. `spread` blends the result with an even distribution. The gains of
  /// the directional speakers always satisfy sum(g^2) = 1 (constant power); LFE and silent
  /// channels get 0.
  /// @param layout the output layout
  /// @param source position and spread of the sound
  /// @param gains receives layout.channels() values (the rest of kMaxChannels is zeroed)
  static void gains(const Layout& layout, const Source& source, float gains[kMaxChannels]);
};

/// A sound that is placed in a layout and moves smoothly: it holds the current gains, glides to
/// new targets with a one-pole filter and adds its bass send. Allocation-free and lock-free, meant
/// for the audio thread. One instance per positioned voice.
class SpatialVoice {
 public:
  /// @param sampleRate output rate in Hz
  explicit SpatialVoice(int sampleRate);

  /// Sets where the sound should be. Normally called once per block. With `snap` the current gains
  /// jump to the target (a new voice starts at its place instead of gliding in from silence).
  void setTarget(const Layout& layout, const Source& source, bool snap = false);

  /// Adds one mono sample to the frame: `frame[c]` for `c < channels` gets the sample times the
  /// current gain of channel c, plus the low-passed sample times the bass share. Moves the gains one
  /// step towards their targets.
  void process(float x, float* frame, int channels);

  /// The current (smoothed) gain of channel `c`, for tests.
  float gain(int c) const { return gain_[c]; }
  /// The current (smoothed) bass gain of channel `c`, for tests.
  float bassGain(int c) const { return bassGain_[c]; }

 private:
  float k_;                              ///< per-sample smoothing coefficient of the gains
  float lowK_;                           ///< coefficient of the bass low-pass stages
  float gain_[kMaxChannels] = {};        ///< current gain of each channel
  float target_[kMaxChannels] = {};      ///< gain each channel glides towards
  float bassGain_[kMaxChannels] = {};    ///< current share of the bass signal in each channel
  float bassTarget_[kMaxChannels] = {};  ///< share of the bass signal each channel glides towards
  float low_[2] = {};                    ///< the two low-pass stages of the bass send
};

}  // namespace dio
