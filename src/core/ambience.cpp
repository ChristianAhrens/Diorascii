/// @file core/ambience.cpp
/// @brief Procedurally synthesized ambient sound.

#include "core/ambience.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "core/world.h"

namespace dio {

namespace {
constexpr double kTwoPi = 6.283185307179586;

/// White noise, -1 to 1.
float noise(Rng& r) { return static_cast<float>(r.uniform() * 2.0 - 1.0); }

/// Soft limiter that keeps the mix within -1 to 1.
float softClip(float x) { return std::tanh(x); }

/// A cricket's chirp: bursts of short pulses on a high carrier, in [0, 1].
float cricket(double t, double burstHz, double pulseHz, double carrierHz) {
  const double burst = std::fmod(t * burstHz, 1.0);
  if (burst > 0.35) return 0.0f;  // silent between bursts
  const double pulse = std::fmod(t * pulseHz, 1.0);
  const double env = pulse < 0.6 ? std::sin(pulse / 0.6 * kTwoPi / 2.0) : 0.0;
  return static_cast<float>(env * std::sin(kTwoPi * carrierHz * t));
}
}  // namespace

void Ambience::clap(double delay, double gain, bool strike) {
  Thunder& th = *std::min_element(std::begin(thunder_), std::end(thunder_),
                                  [](const Thunder& a, const Thunder& b) { return a.start < b.start; });
  th.start = t_ + delay;
  th.gain = gain;
  th.strike = strike;
  // The rumble of a strike overhead rolls in after the crack has cleared.
  const double lag = strike ? 0.32 : 0.0;
  th.swellAt[0] = lag;
  th.swellAt[1] = lag + rng_.range(0.6, 1.4);
  th.swellAt[2] = lag + rng_.range(1.6, 2.8);
  th.swellGain[0] = 1.0;
  th.swellGain[1] = rng_.range(0.4, 0.8);
  th.swellGain[2] = rng_.range(0.2, 0.5);
}

void Ambience::thunderLevels(float& crack, float& rumble, float& snap) const {
  crack = rumble = snap = 0;
  for (const Thunder& th : thunder_) {
    const double t = t_ - th.start;
    if (th.gain <= 0 || t < 0 || t > 9) continue;
    crack += static_cast<float>(th.gain * std::exp(-t * 22.0));  // the sharp first crack
    if (th.strike) {  // two snaps, matching the two quick flashes of the lightning
      snap += static_cast<float>(th.gain * std::exp(-t * 17.0));
      if (t > 0.22) snap += static_cast<float>(0.6 * th.gain * std::exp(-(t - 0.22) * 20.0));
    }
    for (int i = 0; i < 3; ++i) {  // rolling swells, each slowly fading
      const double s = t - th.swellAt[i];
      if (s > 0) rumble += static_cast<float>(th.swellGain[i] * th.gain * (1 - std::exp(-s * 12.0)) * std::exp(-s * 0.7));
    }
  }
}

AmbientParams ambientFor(const World& world) {
  AmbientParams p;
  const std::string& name = world.scene().name;
  if (name == "forest") p.kind = AmbientKind::Forest;
  else if (name == "aquarium") p.kind = AmbientKind::Aquarium;
  const Light l = world.light();
  p.night = static_cast<float>(std::clamp(1.0 - l.sun * 2.0, 0.0, 1.0));
  p.rain = world.raining() ? 1.0f : 0.0f;
  p.flashes = world.flashCount();
  p.storm = static_cast<float>(world.stormProximity());
  p.nibbles = static_cast<unsigned>(world.foodEatenCount());
  p.whale = static_cast<float>(world.nearness("whale"));
  return p;
}

Ambience::Ambience(int sampleRate, uint64_t seed)
    : rate_(sampleRate), noiseScale_(std::sqrt(static_cast<float>(sampleRate) / 48000.0f)), rng_(seed) {
  lowHum_ = coeff(1.0f / 110.0f / 6.28f);
  lowMuffle_ = coeff(1.0f / 650.0f / 6.28f);
  lowDeep_ = coeff(1.0f / 55.0f / 6.28f);
}

void Ambience::bubble(double delay, float f0, float dur, float amp) {
  Bubble& b = *std::min_element(std::begin(bubbles_), std::end(bubbles_),
                                [](const Bubble& x, const Bubble& y) { return x.start < y.start; });
  b.start = t_ + delay;
  b.f0 = f0;
  b.dur = dur;
  b.amp = amp;
  b.pan = static_cast<float>(rng_.range(0.2, 0.8));
}

void Ambience::aquariumSample(const AmbientParams& p, float out[2]) {
  (void)p;
  const double dt = 1.0 / rate_;
  // Slow swells of the water, from the same irregular gusts that move the wind; night is a bit calmer.
  const float swell = (0.55f + 0.9f * gust_) * (1.0f - 0.25f * night_);

  // Random bubbles rise now and then, sometimes in a little cluster.
  if (nextBubble_ == 0) nextBubble_ = t_ + 0.5;
  if (t_ >= nextBubble_) {
    const int n = rng_.chance(0.25) ? rng_.rangeInt(2, 3) : 1;
    for (int i = 0; i < n; ++i) {
      bubble(i * rng_.range(0.05, 0.22), static_cast<float>(rng_.range(350, 1100)),
             static_cast<float>(rng_.range(0.04, 0.10)), static_cast<float>(rng_.range(0.03, 0.07)));
    }
    nextBubble_ = t_ + rng_.range(0.25, 1.4) * (1.0 + 0.8 * night_);
  }
  float bub[2] = {0, 0};
  for (const Bubble& b : bubbles_) {
    const double tau = t_ - b.start;
    if (b.amp <= 0 || tau < 0 || tau > b.dur) continue;
    // A bubble sings a short rising note: it starts at f0 and climbs as it shrinks.
    const double phase = kTwoPi * b.f0 * (tau + 1.1 * tau * tau / b.dur);
    const float v = static_cast<float>(std::sin(phase) * (1.0 - std::exp(-tau * 900.0)) *
                                       std::exp(-tau / (b.dur * 0.38))) * b.amp;
    bub[0] += v * std::cos(b.pan * 1.5708f);
    bub[1] += v * std::sin(b.pan * 1.5708f);
  }

  // A whale passing: a long, gliding moan that repeats while it is near.
  float song = 0;
  const float whaleLevel = whale_ * std::sqrt(whale_);
  if (whale_ > 0.03f) {
    if (nextSong_ == 0) nextSong_ = t_ + rng_.range(1.0, 3.0);
    const bool singing = t_ - song_.start <= song_.dur && song_.dur > 0;
    if (!singing && t_ >= nextSong_) {
      song_ = Song{t_, rng_.range(3.0, 5.0), static_cast<float>(rng_.range(70, 110)),
                   static_cast<float>(rng_.range(0.3, 0.7)), 0};
      nextSong_ = t_ + song_.dur + rng_.range(3.0, 8.0) * (1.6 - whale_);
    }
  } else {
    nextSong_ = 0;
  }
  if (t_ - song_.start <= song_.dur && song_.dur > 0) {
    const double tau = t_ - song_.start, x = tau / song_.dur;
    const double f = song_.f0 * (1.0 + 0.55 * std::sin(3.14159265 * x) + 0.3 * x +
                                 0.02 * std::sin(kTwoPi * 5.5 * tau));
    song_.phase += kTwoPi * f * dt;
    const double env = std::sin(3.14159265 * x);
    song = static_cast<float>((std::sin(song_.phase) + 0.35 * std::sin(2 * song_.phase + 0.5) +
                               0.12 * std::sin(3 * song_.phase)) * env * env) * whaleLevel * 0.16f;
  }

  // The growl of something huge (the kraken) as it nears.
  const float growl = storm_ * storm_ * 0.5f * std::clamp(0.55f + 0.45f * roll_, 0.1f, 1.0f);

  for (int ch = 0; ch < 2; ++ch) {
    const float n = noise(rng_) * noiseScale_;
    waterLow_[ch][0] += (n - waterLow_[ch][0]) * lowHum_;
    waterLow_[ch][1] += (waterLow_[ch][0] - waterLow_[ch][1]) * lowHum_;
    const float n2 = noise(rng_) * noiseScale_;
    muffleLow_[ch] += (n2 - muffleLow_[ch]) * lowMuffle_;
    const float n3 = noise(rng_) * noiseScale_;
    deepLow_[ch][0] += (n3 - deepLow_[ch][0]) * lowDeep_;
    deepLow_[ch][1] += (deepLow_[ch][0] - deepLow_[ch][1]) * lowDeep_;
    const float panSong = ch ? std::sin(song_.pan * 1.5708f) : std::cos(song_.pan * 1.5708f);
    out[ch] = waterLow_[ch][1] * 9.0f * 0.11f * swell + muffleLow_[ch] * 0.05f * swell +
              deepLow_[ch][1] * 20.0f * growl + bub[ch] + song * panSong * 1.4f;
  }
}

float Ambience::coeff(float seconds) const {
  return 1.0f - std::exp(-1.0f / (static_cast<float>(rate_) * seconds));
}

void Ambience::render(const AmbientParams& p, float* out, int frames) {
  const float kPresence = coeff(0.6f), kRain = coeff(1.5f), kNight = coeff(3.0f), kVolume = coeff(0.05f), kStorm = coeff(2.0f);
  const float lowRoll = coeff(1.0f / 0.35f / 6.28f);
  const float rollScale = 1.0f / (0.577f * std::sqrt(lowRoll / 2.0f));
  const float lowRain = coeff(1.0f / 2500.0f / 6.28f);
  const float lowCrack = coeff(1.0f / 1800.0f / 6.28f), lowRumble = coeff(1.0f / 140.0f / 6.28f);
  const double dt = 1.0 / rate_;
  const float snapHz = std::min(2400.0f, static_cast<float>(rate_) / 7.0f);  // keeps the filter stable
  const float snapF = 2.0f * std::sin(3.14159265f * snapHz / static_cast<float>(rate_));
  constexpr float kSnapDamp = 0.35f;  // 1/Q: rings for a few milliseconds
  const float lowWander = coeff(1.0f / 0.45f / 6.28f);
  const float wanderScale = 1.0f / (0.577f * std::sqrt(lowWander / 2.0f));  // back to unit spread

  if (!flashesSynced_) { seenFlashes_ = p.flashes; flashesSynced_ = true; }
  // Lightning: each new flash brings an immediate clap.
  if (p.flashes != seenFlashes_) {
    // (The counter drops when the world is rebuilt; that is no lightning.)
    if (p.flashes > seenFlashes_ && p.kind == AmbientKind::Forest) {
      clap(rng_.range(0.03, 0.10), 1.0, true);  // the strike is overhead: sound follows at once
    }
    seenFlashes_ = p.flashes;
  }

  for (int i = 0; i < frames; ++i, t_ += dt) {
    for (int k = 0; k < kAmbientKinds; ++k) approach(mix_[k], static_cast<int>(p.kind) == k ? 1.0f : 0.0f, kPresence);
    approach(rain_, p.rain, kRain);
    approach(night_, p.night, kNight);
    approach(storm_, p.kind == AmbientKind::Forest || p.kind == AmbientKind::Aquarium ? p.storm : 0.0f, kStorm);
    approach(whale_, p.kind == AmbientKind::Aquarium ? p.whale : 0.0f, kStorm);
    approach(volume_, p.volume, kVolume);

    // Gusts: slow swells plus a random drift, so they never repeat evenly.
    // In calm weather they are slow and relaxed; rain and storm quicken them.
    const double pace = 0.2 + 0.8 * rain_;
    gustPhaseA_ += kTwoPi * 0.09 * pace * dt;
    gustPhaseB_ += kTwoPi * 0.23 * pace * dt;
    approach(wander_, noise(rng_) * wanderScale, lowWander * static_cast<float>(pace));
    const float gust = std::clamp(
        0.45f + 0.20f * static_cast<float>(std::sin(gustPhaseA_)) +
            0.12f * static_cast<float>(std::sin(gustPhaseB_ + 1.3)) + 0.25f * wander_,
        0.0f, 1.0f);
    gust_ = gust;
    // Soft wind in the trees is low-passed noise that swells with the gusts. A gust lifts the
    // pitch a little; in a storm the wind is deeper and stronger.
    const float cutoffHz = (230.0f + 230.0f * gust) * (1.0f - 0.35f * rain_);
    const float lowWind = 1.0f - std::exp(-6.2831853f * cutoffHz / static_cast<float>(rate_));
    const float windGain = (0.3f + 0.9f * gust * gust) * (1.0f + 2.0f * rain_) * 0.10f;
    float crackEnv, rumbleEnv, snapEnv;
    thunderLevels(crackEnv, rumbleEnv, snapEnv);
    // A storm on its way: a soft rolling rumble that grows as it nears, and far-off claps now
    // and then, well before the first strike.
    // The crack: lightning snaps as a quick series of sharp clicks (the same for both ears, so
    // it sits in the middle) that thin out within a fraction of a second. Each click rings a
    // short resonance, which makes it a bright crack and not a hiss.
    float click = 0;
    if (snapEnv > 1e-3f && rng_.uniform() < std::min(1.0, 700.0 * snapEnv * dt)) {
      click = static_cast<float>(rng_.range(0.5, 1.0)) * (rng_.chance(0.5) ? 1.0f : -1.0f);
    }
    approach(roll_, noise(rng_) * rollScale, lowRoll);
    if (storm_ > 0.04f && p.kind == AmbientKind::Forest) {
      rumbleEnv += storm_ * storm_ * 0.16f * std::clamp(0.55f + 0.45f * roll_, 0.1f, 1.0f);
      if (nextFarClap_ == 0) nextFarClap_ = t_ + rng_.range(2.0, 5.0);
      if (t_ >= nextFarClap_) {
        clap(0.0, 0.2 + 0.4 * storm_);
        nextFarClap_ = t_ + rng_.range(7.0, 16.0) * (1.4 - storm_);
      }
    } else {
      nextFarClap_ = 0;
    }
    const float chirps = night_ * 0.035f *
                         (cricket(t_, 2.3, 34, 4200) + 0.8f * cricket(t_ + 0.4, 1.9, 29, 4650) +
                          0.6f * cricket(t_ + 0.9, 2.9, 31, 3900));

    // Nibbles at food: each one a little blip (the very first value seen is the starting point).
    if (!nibblesSynced_) { seenNibbles_ = p.nibbles; nibblesSynced_ = true; }
    if (p.nibbles != seenNibbles_) {
      if (p.nibbles > seenNibbles_ && p.kind == AmbientKind::Aquarium) {
        for (unsigned n = 0; n < std::min(p.nibbles - seenNibbles_, 4u); ++n) {
          bubble(rng_.range(0.0, 0.25), static_cast<float>(rng_.range(900, 1500)), 0.05f,
                 static_cast<float>(rng_.range(0.15, 0.22)));
        }
      }
      seenNibbles_ = p.nibbles;
    }
    float aq[2] = {0, 0};
    if (mix_[static_cast<int>(AmbientKind::Aquarium)] > 1e-3f) aquariumSample(p, aq);

    for (int ch = 0; ch < 2; ++ch) {
      // Wind: noise through two low-pass stages (no hiss, no crackle).
      const float wn = noise(rng_) * noiseScale_;
      windLow_[ch][0] += (wn - windLow_[ch][0]) * lowWind;
      windLow_[ch][1] += (windLow_[ch][0] - windLow_[ch][1]) * lowWind;
      const float wind = windLow_[ch][1] * 9.0f * windGain;

      // Rain: the hiss is noise with its low end removed, plus a quiet rumble beneath it.
      const float n = noise(rng_) * noiseScale_;
      rainLow_[ch] += (n - rainLow_[ch]) * lowRain;
      const float rain = ((n - rainLow_[ch]) * 0.30f + rainLow_[ch] * 0.9f) * rain_;

      // Thunder: a brief crack of mid-high noise, then deep rumbling noise.
      float thunder = 0;
      if (snapEnv > 1e-3f) {
        snapLow_[ch] += snapF * snapBand_[ch];
        const float high = click * 9.0f - snapLow_[ch] - kSnapDamp * snapBand_[ch];
        snapBand_[ch] += snapF * high;
        thunder += snapBand_[ch] * snapEnv;
      }
      if (crackEnv > 1e-4f || rumbleEnv > 1e-4f) {
        const float tn = noise(rng_) * noiseScale_;
        crackLow_[ch] += (tn - crackLow_[ch]) * lowCrack;
        rumbleLow_[ch][0] += (tn - rumbleLow_[ch][0]) * lowRumble;
        rumbleLow_[ch][1] += (rumbleLow_[ch][0] - rumbleLow_[ch][1]) * lowRumble;
        thunder += crackLow_[ch] * crackEnv * 0.85f + rumbleLow_[ch][1] * rumbleEnv * 17.0f;
      }

      const float sample = (wind + rain + thunder + chirps * (ch ? 0.9f : 1.0f)) * mix_[static_cast<int>(AmbientKind::Forest)] +
                           aq[ch] * mix_[static_cast<int>(AmbientKind::Aquarium)];
      out[2 * i + ch] = softClip(sample) * volume_ * 0.8f;
    }
  }
}

}  // namespace dio
