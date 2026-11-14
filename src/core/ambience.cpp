/// @file core/ambience.cpp
/// @brief Procedurally synthesized ambient sound.

#include "core/ambience.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "core/spatializer.h"
#include "core/world.h"

namespace dio {

namespace {
constexpr double kTwoPi = 6.283185307179586;

/// White noise, -1 to 1.
float noise(Rng& r) { return static_cast<float>(r.uniform() * 2.0 - 1.0); }

/// Soft limiter that keeps the mix within -1 to 1.
float softClip(float x) { return std::tanh(x); }

/// The level of a mono sound that was once added to both ears: at the middle of the stage in stereo the
/// panning gains are 1/sqrt(2) each, so such a sound is scaled by sqrt(2) to stay as loud as it has always been.
constexpr float kStereoRef = 1.41421356f;

/// The direction of a stereo pan position (0 hard left, 1 hard right) in the synth's own constant-power
/// law: in a stereo layout the spatializer reproduces exactly that pan.
Source panSource(float pan) {
  Source s;
  s.azimuth = std::asin(std::clamp(2.0f * pan - 1.0f, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
  return s;
}

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
  else if (name == "desert") p.kind = AmbientKind::Desert;
  else if (name == "moon") p.kind = AmbientKind::Moon;
  else if (name == "mountains") p.kind = AmbientKind::Mountains;
  else if (name == "city") p.kind = AmbientKind::City;
  const Light l = world.light();
  p.night = world.scene().airless ? 0.0f : static_cast<float>(std::clamp(1.0 - l.sun * 2.0, 0.0, 1.0));  // the moon sounds the same all day
  p.rain = world.raining() ? 1.0f : 0.0f;
  p.flashes = world.flashCount();
  p.storm = static_cast<float>(world.stormProximity());
  p.nibbles = static_cast<unsigned>(world.foodEatenCount());
  p.whale = static_cast<float>(world.nearness("whale"));
  p.tornado = static_cast<float>(world.nearness("tornado"));
  p.impacts = world.impactCount();
  p.avalanche = static_cast<float>(world.avalancheLevel());
  p.worm = static_cast<float>(world.nearness("sandworm"));
  p.siren = static_cast<float>(world.nearness("siren"));
  p.wail = static_cast<float>(world.nearness("wail"));
  p.train = static_cast<float>(world.nearness("train"));
  const World::TrainSound ts = world.trainSound();
  p.trainMotion = static_cast<float>(ts.motion);
  p.trainBrake = static_cast<float>(ts.brake);
  p.trainHiss = static_cast<float>(ts.hiss);
  p.plane = static_cast<float>(world.nearness("plane"));
  p.fire = static_cast<float>(world.fireLevel());
  p.crushes = world.crushCount();
  p.squeals = world.squealCount();
  p.menace = static_cast<float>(std::max({world.toxinLevel(), world.floodLevel(), world.rotorLevel(), world.yetiLevel(), world.titanLevel(), world.asteroidLevel()}));
  p.shots = world.shotCount();
  p.flames = static_cast<float>(world.flameLevel());
  p.winged = static_cast<float>(world.nearness("winged_titan"));
  for (int k = 0; k < kEmitterKeys; ++k) p.emitters[k] = world.emitter(static_cast<EmitterKey>(k));
  return p;
}

Ambience::Ambience(int sampleRate, uint64_t seed, const Layout& layout)
    : layout_(layout), rate_(sampleRate), noiseScale_(std::sqrt(static_cast<float>(sampleRate) / 48000.0f)), rng_(seed) {
  for (int c = 0; c < kMaxChannels; ++c) {
    bedRng_[c] = Rng(seed ^ (0xD1B54A32D192ED03ull * static_cast<uint64_t>(c + 1)));
    // The moon pad's phase offset of chain c: 0 and 0.9 for the stereo pair, as it has always been, and
    // then steps of the golden angle, which keep any two chains as far apart in phase as 16 can be.
    const double offset = c < 2 ? 0.9 * c : std::fmod(0.9 + 2.399963229728653 * (c - 1), kTwoPi);
    padCos_[c] = std::cos(offset);
    padSin_[c] = std::sin(offset);
  }
  std::fill(feed_, feed_ + kMaxChannels, static_cast<int8_t>(kSilent));
  std::fill(speakerOf_, speakerOf_ + kMaxChannels, -1);
  for (Place& pl : place_) pl.v = SpatialVoice(rate_);
  // How far the chains of each scene wander in level, slowly, so that wind and storms drift around the room.
  driftDepth_[static_cast<int>(AmbientKind::Forest)] = 0.3f;
  driftDepth_[static_cast<int>(AmbientKind::Aquarium)] = 0.15f;
  driftDepth_[static_cast<int>(AmbientKind::Desert)] = 0.3f;
  driftDepth_[static_cast<int>(AmbientKind::Moon)] = 0.1f;
  driftDepth_[static_cast<int>(AmbientKind::Mountains)] = 0.3f;
  driftDepth_[static_cast<int>(AmbientKind::City)] = 0.1f;
  route();
  std::copy(routeTarget_, routeTarget_ + kMaxChannels, routeGain_);  // the first layout needs no fade-in
  bassK_ = coeff(1.0f / spatial::kLfeCutoffHz / 6.28f);
  lowHum_ = coeff(1.0f / 110.0f / 6.28f);
  lowMuffle_ = coeff(1.0f / 650.0f / 6.28f);
  lowDeep_ = coeff(1.0f / 55.0f / 6.28f);
  lowSandCut_ = coeff(1.0f / 1200.0f / 6.28f);
  lowSandTop_ = coeff(1.0f / 4000.0f / 6.28f);
  lowTornado_ = coeff(1.0f / 170.0f / 6.28f);
  lowMoon_ = coeff(1.0f / 120.0f / 6.28f);
  lowRoar_ = coeff(1.0f / 420.0f / 6.28f);
  lowHiss_ = coeff(1.0f / 1100.0f / 6.28f);
  lowFlood_ = coeff(1.0f / 420.0f / 6.28f);
  lowFloodTop_ = coeff(1.0f / 2300.0f / 6.28f);
  lowChop_ = coeff(1.0f / 650.0f / 6.28f);
  lowThump_ = coeff(1.0f / 190.0f / 6.28f);
  lowFlameTop_ = coeff(1.0f / 2800.0f / 6.28f);
  lowFlameBottom_ = coeff(1.0f / 320.0f / 6.28f);
  lowCrunch_ = coeff(1.0f / 480.0f / 6.28f);
  lowGulp_ = coeff(1.0f / 520.0f / 6.28f);
  lowCar_ = coeff(1.0f / 230.0f / 6.28f);
  lowCarMid_ = coeff(1.0f / 1500.0f / 6.28f);
  lowCarTop_ = coeff(1.0f / 450.0f / 6.28f);
  lowEngine_ = coeff(1.0f / 480.0f / 6.28f);
  lowHorn_ = coeff(1.0f / 2600.0f / 6.28f);
  lowTrain_ = coeff(1.0f / 130.0f / 6.28f);
  lowTrainBand_ = coeff(1.0f / 1300.0f / 6.28f);
  lowKlack_ = coeff(1.0f / 200.0f / 6.28f);
  lowFire_ = coeff(1.0f / 420.0f / 6.28f);
  lowJet_ = coeff(1.0f / 700.0f / 6.28f);
  lowSqueal_ = coeff(1.0f / 1800.0f / 6.28f);
  lowSlide_ = coeff(1.0f / 95.0f / 6.28f);
  lowSlideMid_ = coeff(1.0f / 900.0f / 6.28f);
  lowStreamTop_ = coeff(1.0f / 1300.0f / 6.28f);
  lowStreamBottom_ = coeff(1.0f / 380.0f / 6.28f);
  lowIce_ = coeff(1.0f / 2500.0f / 6.28f);
}

void Ambience::route() {
  // The directional speaker nearest the left and right of the room play chains 0 and 1, which carry
  // the voices as well as the bed; every other directional speaker gets a bed chain of its own, in
  // output order; the LFE channel plays the bass of all of them.
  int8_t next[kMaxChannels];
  int* speakerOf = speakerOf_;  // the output channel of each chain
  std::fill(next, next + kMaxChannels, static_cast<int8_t>(kSilent));
  std::fill(speakerOf, speakerOf + kMaxChannels, -1);
  int left, right, used = 2;
  if (layout_.stereoPair(left, right)) {
    if (left == right) {
      next[left] = kBoth;
    } else {
      next[left] = 0;
      next[right] = 1;
    }
    speakerOf[0] = left;
    speakerOf[1] = right;
  }
  for (int c = 0; c < layout_.channels(); ++c) {
    if (next[c] != kSilent) continue;
    if (c == layout_.lfeIndex()) next[c] = kBass;
    else if (layout_.speaker(c).isDirectional() && used < kMaxChannels) {
      speakerOf[used] = c;
      next[c] = static_cast<int8_t>(used++);
    }
  }
  chains_ = used;

  // The level of every chain in every scene: the rear a little quieter, the centre not to form a
  // phantom image, the heights by how much of the scene's bed is overhead (none for the aquarium).
  static constexpr float kHeightShare[kAmbientKinds] = {0.0f, 0.6f, 0.0f, 0.7f, 0.4f, 0.7f, 0.0f};
  for (int k = 0; k < kAmbientKinds; ++k) {
    liveMask_[k] = 0;
    for (int ch = 0; ch < kMaxChannels; ++ch) {
      float t = 0;
      if (ch < 2) {
        t = 1.0f;  // the stereo pair is the synth as it always was
      } else if (ch < chains_) {
        const Speaker& sp = layout_.speaker(speakerOf[ch]);
        if (sp.elevation > 1.0f) t = kHeightShare[k];
        else if (std::fabs(sp.azimuth) > 100.0f) t = 0.85f;
        else if (std::fabs(sp.azimuth) < 5.0f) t = 0.7f;
        else t = 1.0f;
      }
      trim_[k][ch] = t;
      if (t > 0) liveMask_[k] |= 1u << ch;
    }
  }
  updateDrift();
  for (Place& pl : place_) pl.v.setTarget(layout_, forLayout(pl.src), false);  // placed sounds glide to their place in the new layout

  for (int c = 0; c < kMaxChannels; ++c) {
    routeTarget_[c] = next[c] == kSilent ? 0.0f : 1.0f;
    if (next[c] != feed_[c]) routeGain_[c] = 0;  // a channel with a new role fades in
    feed_[c] = next[c];
  }
}

void Ambience::updateDrift() {
  for (int k = 0; k < kAmbientKinds; ++k) {
    for (int ch = 0; ch < kMaxChannels; ++ch) {
      // The stereo pair does not drift. The others swell and ebb at slightly different, very slow
      // rates and phases, so the wind seems to move round the room.
      const double wave = ch < 2 ? 0.0 : std::sin(kTwoPi * (0.045 + 0.0137 * ch) * t_ + 1.9 * ch);
      bedGain_[k][ch] = trim_[k][ch] * (1.0f + driftDepth_[k] * static_cast<float>(wave));
    }
  }
}

Source Ambience::stageSource(float x, float y, float spread, float lfeSend) {
  Source s;
  const float ax = std::fabs(x);
  // The screen is a window of +-45 degrees; beyond it the sound moves round to the side (110 degrees at the limit).
  const float az = ax <= 1.0f ? ax * 45.0f : 45.0f + (std::min(ax, kStageReach) - 1.0f) * (110.0f - 45.0f) / (kStageReach - 1.0f);
  s.azimuth = x < 0 ? -az : az;
  s.elevation = std::clamp(y, 0.0f, 1.0f) * 60.0f;
  s.spread = spread;
  s.lfeSend = lfeSend;
  return s;
}

void Ambience::aim(int id, const Source& src) {
  Place& pl = place_[id];
  pl.src = src;
  pl.v.setTarget(layout_, forLayout(src), !pl.on);
  pl.on = true;
}

void Ambience::stepPlaces() {
  const int n = layout_.channels();
  for (Place& pl : place_)
    if (pl.on) pl.v.step(n);
}

float Ambience::weight(int id, int chain) const {
  const Place& pl = place_[id];
  if (!pl.on || speakerOf_[chain] < 0) return 0.0f;
  return pl.v.gain(speakerOf_[chain]) * kStereoRef;
}

void Ambience::flushRoom(float bus[kMaxChannels], float bassScale) {
  for (int k = 0; k < chains_; ++k)
    if (speakerOf_[k] >= 0) bus[k] += room_[speakerOf_[k]];
  if (layout_.lfeIndex() >= 0) bassExtra_ += room_[layout_.lfeIndex()] * bassScale;
  std::fill(room_, room_ + layout_.channels(), 0.0f);
}

void Ambience::aimCity(const AmbientParams& p) {
  // A place is aimed when its sound is wanted: at the emitter if the world has one (it keeps its last
  // place while the emitter is gone and the sound dies away), else straight ahead.
  auto emitter = [&](int id, EmitterKey key, bool wanted, float spread, float lfe) {
    const Emitter& e = p.emitters[static_cast<int>(key)];
    if (wanted && (e.level > 0 || !place_[id].on)) aim(id, stageSource(e.x, e.y, spread, lfe));
  };
  emitter(kSiren, EmitterKey::Siren, p.siren > 0, 0.0f, 0.0f);
  emitter(kWail, EmitterKey::Wail, p.wail > 0, 0.0f, 0.0f);
  emitter(kTrain, EmitterKey::Train, p.train > 0, 0.1f, 0.5f);
  emitter(kPlane, EmitterKey::Plane, p.plane > 0, 0.3f, 0.0f);
  emitter(kFire, EmitterKey::Fire, p.fire > 0, 0.2f + 0.5f * p.fire, 0.0f);
  emitter(kFlames, EmitterKey::Flames, p.flames > 0 && p.menace > 0, 0.4f, 0.0f);
  emitter(kTitan, EmitterKey::Titan, p.menace > 0, 0.1f, 1.0f);
  emitter(kWinged, EmitterKey::Winged, p.menace > 0 && p.winged > 0, 0.4f, 0.0f);
}

void Ambience::event(const SoundEvent& e) {
  if (pendingCount_ == kMaxPendingEvents) {  // keep the newest
    std::copy(pending_ + 1, pending_ + kMaxPendingEvents, pending_);
    --pendingCount_;
  }
  pending_[pendingCount_++] = e;
}

void Ambience::setLayout(const Layout& layout) {
  layout_ = layout;
  route();
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

void Ambience::moonSample(const AmbientParams& p, float out[kMaxChannels]) {
  (void)p;
  const double dt = 1.0 / rate_;
  // No air, so nothing to hear but what could be felt through the ground: a faint, deep drone of
  // stacked fifths that drifts slowly, with a hint of shimmer above it.
  const double drift = 0.7 + 0.3 * std::sin(kTwoPi * 0.043 * t_);
  const double shimmer = 0.5 + 0.5 * std::sin(kTwoPi * 0.11 * t_ + 0.8);
  float pad[kMaxChannels];
  for (int ch = 0; ch < 2; ++ch) {
    const double o = ch ? 0.9 : 0.0;
    pad[ch] = static_cast<float>(
        drift * (0.5 * std::sin(kTwoPi * 55.0 * t_ + o) + 0.35 * std::sin(kTwoPi * 82.5 * t_ + 1.0 + o)) +
        0.18 * std::sin(kTwoPi * 220.0 * t_ + 2.0 + o) * drift +
        0.07 * shimmer * std::sin(kTwoPi * 330.7 * t_ + o) + 0.03 * (1 - shimmer) * std::sin(kTwoPi * 441.0 * t_ + o)) * 0.06f;
  }
  if (chains_ > 2) {
    // The other speakers play the same chord with their own phase offsets. The sine and cosine of
    // each partial are computed once, and every chain rotates them: sin(a + o) = sin a cos o + cos a sin o.
    const double arg[5] = {kTwoPi * 55.0 * t_, kTwoPi * 82.5 * t_ + 1.0, kTwoPi * 220.0 * t_ + 2.0, kTwoPi * 330.7 * t_, kTwoPi * 441.0 * t_};
    double sn[5], cs[5];
    for (int k = 0; k < 5; ++k) {
      sn[k] = std::sin(arg[k]);
      cs[k] = std::cos(arg[k]);
    }
    for (int ch = 2; ch < chains_; ++ch) {
      if (!live(AmbientKind::Moon, ch)) continue;
      double s[5];
      for (int k = 0; k < 5; ++k) s[k] = sn[k] * padCos_[ch] + cs[k] * padSin_[ch];
      pad[ch] = static_cast<float>(drift * (0.5 * s[0] + 0.35 * s[1]) + 0.18 * s[2] * drift + 0.07 * shimmer * s[3] +
                                   0.03 * (1 - shimmer) * s[4]) * 0.06f;
    }
  }

  // A meteorite strike: a thud that thumps up out of the ground and falls away, a dull rumble,
  // and then debris pattering down slowly in the low gravity.
  float thud = 0, rumble = 0, patter = 0, click = 0;
  for (const Impact& im : impacts_) {
    const double tau = t_ - im.start;
    if (im.gain <= 0 || tau < 0 || tau > 9) continue;
    const double sweep = 34.0 * tau + 70.0 * (1.0 - std::exp(-9.0 * tau)) / 9.0;  // 104 Hz sinking to 34 Hz
    thud += static_cast<float>(std::sin(kTwoPi * sweep) * (1.0 - std::exp(-tau * 400.0)) * std::exp(-tau * 3.2)) * im.gain;
    rumble += static_cast<float>(im.gain * std::exp(-tau * 1.3) * (1.0 - std::exp(-tau * 30.0)));
    if (tau > 0.5) {
      const double density = 60.0 * std::exp(-(tau - 0.5) * 0.8);  // clicks per second
      if (rng_.uniform() < density * dt) click += static_cast<float>(rng_.range(0.4, 1.0) * (rng_.chance(0.5) ? 1 : -1)) * im.gain;
    }
  }
  patter = click;

  // The alien: an eerie rising warble.
  float zap = 0;
  const double zt = t_ - zapStart_;
  if (zapStart_ > 0 && zt >= 0 && zt < 0.9) {
    const double f = 350.0 + 1000.0 * (zt / 0.9) + 120.0 * std::sin(kTwoPi * 17.0 * zt);
    zapPhase_ += kTwoPi * f * dt;
    const double env = std::pow(std::sin(3.14159265 * zt / 0.9), 1.5);
    zap = static_cast<float>((std::sin(zapPhase_) + 0.3 * std::sin(1.5 * zapPhase_)) * env) * 0.12f;
  }

  const float debrisF = 2.0f * std::sin(3.14159265f * std::min(900.0f, static_cast<float>(rate_) / 7.0f) / static_cast<float>(rate_));
  // The drone and the impacts' rumble are the bed, one chain per speaker; the thud, the debris
  // and the warble are voices, which only the stereo pair carries until they are placed.
  for (int ch = 0; ch < chains_; ++ch) {
    if (!live(AmbientKind::Moon, ch)) continue;
    const float n = noise(chainRng(ch)) * noiseScale_;
    moonLow_[ch][0] += (n - moonLow_[ch][0]) * lowMoon_;
    moonLow_[ch][1] += (moonLow_[ch][0] - moonLow_[ch][1]) * lowMoon_;
    if (ch < 2) {
      // Debris: each click rings a short, dull resonance.
      debrisLow_[ch] += debrisF * debrisBand_[ch];
      const float high = patter * 6.0f - debrisLow_[ch] - 0.6f * debrisBand_[ch];
      debrisBand_[ch] += debrisF * high;
      out[ch] = pad[ch] + thud * 0.55f + moonLow_[ch][1] * 14.0f * rumble * 0.35f + debrisBand_[ch] * 0.3f + zap;
    } else {
      out[ch] = pad[ch] + moonLow_[ch][1] * 14.0f * rumble * 0.35f;
    }
  }
}

void Ambience::mountainSample(const AmbientParams& p, float out[kMaxChannels]) {
  (void)p;
  const double dt = 1.0 / rate_;
  // Thin alpine wind. In a snowstorm it is much stronger and higher, with a whistle that glides
  // with the gusts and the fine hiss of ice crystals on top.
  const float cutoffHz = (300.0f + 360.0f * gust_) * (1.0f + 1.3f * rain_);
  const float lowWind = 1.0f - std::exp(-6.2831853f * cutoffHz / static_cast<float>(rate_));
  const float windGain = (0.35f + 0.8f * gust_) * (1.0f + 5.0f * rain_) * 0.075f;
  whistlePhase_ += kTwoPi * (620.0 + 340.0 * gust_ + 60.0 * std::sin(0.4 * t_)) * dt;
  const float whistle = rain_ * rain_ * gust_ * 0.02f * static_cast<float>(std::sin(whistlePhase_));
  const float hiss = rain_ * (0.1f + 0.2f * gust_);
  // The stream: noise between two low-passes, quietly, that bubbles and gurgles: it is broken up by
  // a few unrelated fast ripples so that it does not sound like steady brushing.
  const float babble = std::clamp(0.35f + 0.65f * static_cast<float>(
      std::sin(kTwoPi * 6.1 * t_ + 2.0 * std::sin(kTwoPi * 0.7 * t_)) *
      std::sin(kTwoPi * 9.7 * t_ + 1.3 * std::sin(kTwoPi * 1.3 * t_ + 1.0))), 0.0f, 1.0f);
  // An eagle's cry now and then, by day only and not in a snowstorm: a few shrill, falling notes.
  const float day = (1.0f - night_) * (1.0f - std::min(1.0f, 2.0f * rain_));
  if (nextCry_ == 0) nextCry_ = t_ + rng_.range(5.0, 15.0);
  if (t_ >= nextCry_) {
    cryStart_ = t_;
    cryPan_ = static_cast<float>(rng_.range(0.2, 0.8));
    cryPitch_ = static_cast<float>(rng_.range(0.9, 1.12));
    nextCry_ = t_ + rng_.range(15.0, 35.0);
  }
  float cry = 0;
  const double ct = t_ - cryStart_;
  if (ct >= 0 && ct < 1.3) {
    const double syl = std::fmod(ct, 0.42), n = std::floor(ct / 0.42);  // three notes, 0.42 s apart
    if (n < 3 && syl < 0.3) {
      const double x = syl / 0.3;
      const double f = cryPitch_ * (3400.0 - 1300.0 * x) * (1.0 - 0.05 * n) + 60.0 * std::sin(kTwoPi * 38.0 * syl);
      cryPhase_ += kTwoPi * f * dt;
      const double env = std::sin(3.14159265 * x) * (1.0 - 0.18 * n);
      cry = static_cast<float>((std::sin(cryPhase_) + 0.5 * std::sin(2 * cryPhase_ + 0.4) + 0.3 * noise(rng_)) * env) * 0.09f * day;
    }
  }
  // A woodpecker drums on a tree now and then, as often as the eagle cries: a quick run of dry,
  // woody knocks that dies away a little. Same rules: by day, not in a snowstorm.
  if (nextPeck_ == 0) nextPeck_ = t_ + rng_.range(8.0, 20.0);
  if (t_ >= nextPeck_) {
    peckStart_ = t_;
    peckKnocks_ = rng_.rangeInt(7, 13);
    peckPan_ = static_cast<float>(rng_.range(0.15, 0.85));
    peckPitch_ = static_cast<float>(rng_.range(0.85, 1.2));
    nextPeck_ = t_ + rng_.range(15.0, 35.0);
  }
  float peck = 0;
  const double pt = t_ - peckStart_;
  constexpr double kKnockHz = 16.0;
  if (pt >= 0 && pt < peckKnocks_ / kKnockHz) {
    const int k = static_cast<int>(pt * kKnockHz);
    const double lt = pt - k / kKnockHz;  // time since this knock began
    const double amp = 1.0 - 0.5 * k / peckKnocks_;
    peck = static_cast<float>((std::sin(kTwoPi * 1150.0 * peckPitch_ * lt) * std::exp(-lt * 150.0) +
                               0.5 * std::sin(kTwoPi * 2300.0 * peckPitch_ * lt) * std::exp(-lt * 260.0) +
                               0.3 * noise(rng_) * std::exp(-lt * 500.0)) * amp) * 0.12f * day;
  }
  // An avalanche: when the slab breaks and while it tumbles, snow, rock and trees knock against
  // each other: a burst of crackling at the start, then clicks and now and then a dull thump with
  // a crack when something big goes down.
  if (p.avalanche > 0.7f && !slideBroke_) { slideBroke_ = true; breakEnv_ = 1.0f; thumpEnv_ = 1.0f; thumpFreq_ = 60.0f; }
  if (p.avalanche < 0.3f) slideBroke_ = false;
  breakEnv_ -= breakEnv_ * static_cast<float>(dt / 1.1);
  if (p.avalanche > 0.5f && rng_.chance(1.3 * p.avalanche * dt)) {
    thumpEnv_ = static_cast<float>(rng_.range(0.4, 0.9));
    thumpFreq_ = static_cast<float>(rng_.range(55, 120));
  }
  thumpEnv_ -= thumpEnv_ * static_cast<float>(dt * 7.0);
  thumpPhase_ += kTwoPi * thumpFreq_ * dt;
  const float thump = static_cast<float>(std::sin(thumpPhase_)) * thumpEnv_ * 0.35f;
  const double clickRate = 220.0 * breakEnv_ + 18.0 * p.avalanche * p.avalanche + 260.0 * thumpEnv_;
  float click = 0;
  if (clickRate > 0 && rng_.uniform() < clickRate * dt) click = static_cast<float>(rng_.range(0.4, 1.0)) * (rng_.chance(0.5) ? 1.0f : -1.0f);
  const float crackF = 2.0f * std::sin(3.14159265f * std::min(1800.0f, static_cast<float>(rate_) / 7.0f) / static_cast<float>(rate_));
  // An avalanche: a deep rumble that swells as it gets going, and the roar and crackle of tumbling
  // snow and rock on top of it.
  const float slide = avalanche_;
  const float roll = std::clamp(0.7f + 0.3f * roll_, 0.2f, 1.0f);
  // An animal buried by the snow: a short squeal that sinks in pitch, heard through the snow
  // (low-passed hard, so it is muted).
  float squeal = 0;
  for (Squeal& s : squealVoices_) {
    const double tau = t_ - s.start;
    if (!s.used || tau < 0 || tau > 0.6) continue;
    const double hz = s.pitch * (1150.0 - 450.0 * tau / 0.6) * (1.0 + 0.03 * std::sin(kTwoPi * 11.0 * tau));
    s.phase += kTwoPi * hz * dt;
    const double env = std::min(1.0, tau / 0.03) * std::exp(-tau / 0.25);
    squeal += static_cast<float>(env * (std::sin(s.phase) + 0.5 * std::sin(2 * s.phase) + 0.3 * std::sin(3 * s.phase)));
  }
  squealLow_[0] += (squeal - squealLow_[0]) * lowSqueal_;
  squealLow_[1] += (squealLow_[0] - squealLow_[1]) * lowSqueal_;

  // Wind, ice, stream and the avalanche's rumble and roar are the bed, one chain per speaker; the
  // whistle, the cries, the knocks, the clicks, the thump and the squeals are voices, which only
  // the stereo pair carries until they are placed.
  for (int ch = 0; ch < chains_; ++ch) {
    if (!live(AmbientKind::Mountains, ch)) continue;
    Rng& r = chainRng(ch);
    const float n = noise(r) * noiseScale_;
    mountLow_[ch][0] += (n - mountLow_[ch][0]) * lowWind;
    mountLow_[ch][1] += (mountLow_[ch][0] - mountLow_[ch][1]) * lowWind;
    const float n2 = noise(r) * noiseScale_;
    iceLow_[ch] += (n2 - iceLow_[ch]) * lowIce_;
    const float n3 = noise(r) * noiseScale_;
    streamLow_[ch][0] += (n3 - streamLow_[ch][0]) * lowStreamTop_;
    streamLow_[ch][1] += (n3 - streamLow_[ch][1]) * lowStreamBottom_;
    const float n4 = noise(r) * noiseScale_;
    slideLow_[ch][0] += (n4 - slideLow_[ch][0]) * lowSlide_;
    slideLow_[ch][1] += (slideLow_[ch][0] - slideLow_[ch][1]) * lowSlide_;
    const float n5 = noise(r) * noiseScale_;
    slideMid_[ch] += (n5 - slideMid_[ch]) * lowSlideMid_;
    if (ch < 2) {
      crackLowAv_[ch] += crackF * crackBandAv_[ch];
      const float highClick = click * 8.0f - crackLowAv_[ch] - 0.5f * crackBandAv_[ch];
      crackBandAv_[ch] += crackF * highClick;
      const float panCry = ch ? std::sin(cryPan_ * 1.5708f) : std::cos(cryPan_ * 1.5708f);
      out[ch] = mountLow_[ch][1] * 9.0f * windGain + whistle * (ch ? 0.9f : 1.0f) + (n2 - iceLow_[ch]) * 0.6f * hiss +
                (streamLow_[ch][0] - streamLow_[ch][1]) * 0.085f * babble * (1.0f - 0.5f * rain_) +
                cry * panCry * 1.4f +
                peck * (ch ? std::sin(peckPan_ * 1.5708f) : std::cos(peckPan_ * 1.5708f)) * 1.4f + crackBandAv_[ch] * 0.35f + thump + slideLow_[ch][1] * 14.0f * slide * slide * roll +
                slideMid_[ch] * 0.7f * slide * slide * slide * roll + squealLow_[1] * 0.6f;
    } else {
      out[ch] = mountLow_[ch][1] * 9.0f * windGain + (n2 - iceLow_[ch]) * 0.6f * hiss +
                (streamLow_[ch][0] - streamLow_[ch][1]) * 0.085f * babble * (1.0f - 0.5f * rain_) +
                slideLow_[ch][1] * 14.0f * slide * slide * roll + slideMid_[ch] * 0.7f * slide * slide * slide * roll;
    }
  }
}

void Ambience::desertSample(const AmbientParams& p, float out[kMaxChannels]) {
  (void)p;
  const double dt = 1.0 / rate_;
  // Dry wind: low-passed noise like the forest's, but steadier and a little higher. In a sandstorm
  // it howls stronger, with the hiss of blown sand on top.
  const float cutoffHz = (260.0f + 300.0f * gust_) * (1.0f + 0.7f * rain_);
  const float lowWind = 1.0f - std::exp(-6.2831853f * cutoffHz / static_cast<float>(rate_));
  // Calm weather is barely a breath; a sandstorm or a tornado brings the full wind.
  const float calm = 0.22f + 0.78f * std::min(1.0f, rain_ + tornado_);
  const float windGain = (0.45f + 0.8f * gust_) * (1.0f + 3.0f * rain_ + 0.8f * tornado_) *
                         (1.0f - 0.3f * night_) * calm * 0.085f;
  const float sandLevel = std::max(rain_, tornado_ * 0.6f) * (0.55f + 0.45f * gust_);
  // A tornado: a deep roar that turns, loud and quiet about three times a second.
  tornadoPhase_ += kTwoPi * 2.8 * dt;
  const float spin = 0.65f + 0.35f * static_cast<float>(std::sin(tornadoPhase_));
  const float roar = tornado_ * std::sqrt(tornado_) * spin * 2.0f;
  // Crickets at night, fewer than in the forest.
  const float chirps = night_ * 0.014f * (cricket(t_, 2.1, 33, 4300) + 0.7f * cricket(t_ + 0.5, 1.7, 28, 4800));
  // The sandworm below, as it nears: a growl in the ground.
  const float growl = storm_ * storm_ * 0.5f * std::clamp(0.55f + 0.45f * roll_, 0.1f, 1.0f);
  // The worm bellows now and then while it crosses: a very deep, trumpeting roar. The pitch
  // swoops up and sinks again, the tone is rough (a fast tremble) and low-passed to stay deep.
  if (wormNear_ > 0.05f) {
    if (nextRoar_ == 0) nextRoar_ = t_ + rng_.range(0.3, 1.2);
    if (t_ >= nextRoar_) {
      roarStart_ = t_;
      roarDur_ = rng_.range(2.0, 2.8);
      nextRoar_ = t_ + roarDur_ + rng_.range(1.2, 3.5);
    }
  } else {
    nextRoar_ = 0;
  }
  float bellow = 0;
  const double rt = t_ - roarStart_;
  if (rt >= 0 && rt < roarDur_) {
    const double u = rt / roarDur_;
    const double env = std::min(1.0, rt / 0.3) * std::min(1.0, (roarDur_ - rt) / 0.8);
    const double hz = 34.0 + 46.0 * std::sin(3.14159265 * std::pow(u, 0.7)) + 3.0 * std::sin(kTwoPi * 5.5 * rt);
    roarPhase_ += kTwoPi * hz * dt;
    const double tone = std::tanh(2.4 * std::sin(roarPhase_) + 1.3 * std::sin(2 * roarPhase_ + 0.6) +
                                  0.7 * std::sin(3 * roarPhase_ + 1.1));
    bellow = static_cast<float>(tone * env * (1.0 + 0.4 * std::sin(kTwoPi * 23.0 * t_)));
  }
  roarLow_[0] += (bellow - roarLow_[0]) * lowRoar_;
  roarLow_[1] += (roarLow_[0] - roarLow_[1]) * lowRoar_;
  const float roarOut = roarLow_[1] * 0.45f * wormNear_;
  // Crushing: chewing. Each thing it swallows is ground between a few soft bites, every one a
  // short swell of low-passed noise (no sharp start, so it never sounds like a shot), over a faint
  // low grumble.
  float crunchEnv = 0, grumble = 0;
  for (const Crunch& c : crunches_) {
    const double tau = t_ - c.start;
    if (c.gain <= 0 || tau < 0 || tau > 1.6) continue;
    for (int b = 0; b < c.bites; ++b) {
      const double bt = tau - c.bite[b];
      if (bt < 0 || bt > 0.09) continue;
      const double swell = std::sin(3.14159265 * bt / 0.09);
      crunchEnv += static_cast<float>(c.gain * c.force[b] * swell * swell);
    }
    grumble += static_cast<float>(c.gain * std::sin(kTwoPi * 62.0 * tau) * std::min(1.0, tau / 0.12) * std::exp(-tau / 0.7));
  }

  // Wind, sand, the tornado's roar and the growl are the bed, one chain per speaker; the crickets,
  // the bellow, the grumble and the chewing are voices, which only the stereo pair carries until
  // they are placed.
  for (int ch = 0; ch < chains_; ++ch) {
    if (!live(AmbientKind::Desert, ch)) continue;
    Rng& r = chainRng(ch);
    const float n = noise(r) * noiseScale_;
    desertLow_[ch][0] += (n - desertLow_[ch][0]) * lowWind;
    desertLow_[ch][1] += (desertLow_[ch][0] - desertLow_[ch][1]) * lowWind;
    const float n2 = noise(r) * noiseScale_;
    sandLow_[ch][0] += (n2 - sandLow_[ch][0]) * lowSandCut_;
    sandLow_[ch][1] += (n2 - sandLow_[ch][1]) * lowSandTop_;
    const float n3 = noise(r) * noiseScale_;
    tornadoLow_[ch][0] += (n3 - tornadoLow_[ch][0]) * lowTornado_;
    tornadoLow_[ch][1] += (tornadoLow_[ch][0] - tornadoLow_[ch][1]) * lowTornado_;
    float crunchOut = 0;
    if (ch < 2) {
      const float nc = noise(r) * noiseScale_;
      crunchLow_[ch][0] += (nc - crunchLow_[ch][0]) * lowCrunch_;
      crunchLow_[ch][1] += (crunchLow_[ch][0] - crunchLow_[ch][1]) * lowCrunch_;
      // the grinding is the noise below ~500 Hz, with a little more of the bite's grit above it
      crunchOut = (crunchLow_[ch][1] * 7.0f + (nc - crunchLow_[ch][0]) * 0.25f) * crunchEnv;
    }
    const float n4 = noise(r) * noiseScale_;
    deepLow_[ch][0] += (n4 - deepLow_[ch][0]) * lowDeep_;
    deepLow_[ch][1] += (deepLow_[ch][0] - deepLow_[ch][1]) * lowDeep_;
    if (ch < 2) {
      out[ch] = desertLow_[ch][1] * 9.0f * windGain + (sandLow_[ch][1] - sandLow_[ch][0]) * 0.5f * sandLevel +
                tornadoLow_[ch][1] * 8.0f * roar + deepLow_[ch][1] * 20.0f * growl +
                chirps * (ch ? 0.9f : 1.0f) + roarOut + grumble * 0.03f + crunchOut;
    } else {
      out[ch] = desertLow_[ch][1] * 9.0f * windGain + (sandLow_[ch][1] - sandLow_[ch][0]) * 0.5f * sandLevel +
                tornadoLow_[ch][1] * 8.0f * roar + deepLow_[ch][1] * 20.0f * growl;
    }
  }
}

void Ambience::citySample(const AmbientParams& p, float out[kMaxChannels]) {
  (void)p;
  const double dt = 1.0 / rate_;
  const float day = 1.0f - 0.55f * night_;
  // Cars passing by now and then: an engine swell that sinks in pitch as it goes, panned across.
  if (nextPass_ == 0) nextPass_ = t_ + rng_.range(0.3, 1.5);
  if (t_ >= nextPass_) {
    Pass& v = *std::min_element(std::begin(passes_), std::end(passes_), [](const Pass& a, const Pass& b) { return a.start < b.start; });
    v.start = t_;
    v.dur = rng_.range(1.4, 2.6);
    v.pitch = static_cast<float>(rng_.range(80, 125));
    v.pan = static_cast<float>(rng_.range(0.1, 0.9));
    v.gain = static_cast<float>(rng_.range(0.5, 1.0));
    nextPass_ = t_ + rng_.range(1.2, 4.5) * (1.0 + 2.0 * night_);
    Place& pl = place_[kEngine0 + static_cast<int>(&v - passes_)];  // the car is placed where its pan says
    pl.src = panSource(v.pan);
    pl.v.setTarget(layout_, pl.src, true);
  }
  // Each engine is one mono sound, low-passed on its own and placed; the stereo pan is the spatializer's.
  const int nch = layout_.channels();
  for (int i = 0; i < 4; ++i) {
    Pass& v = passes_[i];
    float tone = 0;
    const double tau = t_ - v.start;
    if (!(v.gain <= 0 || tau < 0 || tau > v.dur)) {
      const double u = tau / v.dur;
      const double env = std::sin(3.14159265 * u) * std::sin(3.14159265 * u);
      v.phase += kTwoPi * v.pitch * (1.3 - 0.55 * u) * dt;  // the pitch sinks as it drives away
      tone = static_cast<float>(std::tanh(1.8 * std::sin(v.phase) + 0.8 * std::sin(2 * v.phase + 0.4) + 0.5 * std::sin(3 * v.phase)) * env * v.gain);
    }
    engineLow_[i] += (tone - engineLow_[i]) * lowEngine_;
    if (tone != 0 || std::fabs(engineLow_[i]) > 1e-7f) place_[kEngine0 + i].v.add(engineLow_[i] * 0.16f * day, room_, nch);
  }
  // Horns: now and then one or two honks, rarer at night.
  if (nextHonk_ == 0) nextHonk_ = t_ + rng_.range(2.0, 6.0);
  if (t_ >= nextHonk_) {
    Honk& h = *std::min_element(std::begin(honks_), std::end(honks_), [](const Honk& a, const Honk& b) { return a.start < b.start; });
    h.start = t_;
    h.pitch = static_cast<float>(rng_.range(0.88, 1.18));
    h.pan = static_cast<float>(rng_.range(0.1, 0.9));
    h.gain = static_cast<float>(rng_.range(0.6, 1.0));
    h.long_ = rng_.chance(0.2) ? 1.0f : 0.0f;
    h.count = (h.long_ > 0 || rng_.chance(0.55)) ? 1 : 2;
    nextHonk_ = t_ + rng_.range(5.0, 16.0) * (1.0 + 2.5 * night_);
    place_[kHorn].src = panSource(h.pan);  // honks are seconds apart: one place serves them all
    place_[kHorn].v.setTarget(layout_, place_[kHorn].src, true);
  }
  float horn = 0;
  for (Honk& h : honks_) {
    const double tau = t_ - h.start;
    if (h.gain <= 0 || tau < 0 || tau > 1.2) continue;
    const double len = h.long_ > 0 ? 0.8 : 0.22, gap = 0.34;
    for (int k = 0; k < h.count; ++k) {
      const double lt = tau - k * gap;
      if (lt < 0 || lt > len) continue;
      const double env = std::min(1.0, lt / 0.01) * std::min(1.0, (len - lt) / 0.04);
      h.phaseA += kTwoPi * 420.0 * h.pitch * dt;
      h.phaseB += kTwoPi * 530.0 * h.pitch * dt;  // two notes a third apart, like a car horn
      const float s = static_cast<float>((std::tanh(3.0 * std::sin(h.phaseA)) + std::tanh(3.0 * std::sin(h.phaseB))) * 0.5 * env * h.gain);
      horn += s;
    }
  }
  hornLow_ += (horn - hornLow_) * lowHorn_;
  if (horn != 0 || std::fabs(hornLow_) > 1e-7f) place_[kHorn].v.add(hornLow_ * 0.045f, room_, nch);
  // The siren of an ambulance or a fire engine: two notes, the pitch gliding between them.
  const float sirenTarget = std::fmod(t_, 1.0) < 0.5 ? 960.0f : 770.0f;
  sirenFreq_ += (sirenTarget - sirenFreq_) * 0.0025f * 48000.0f / static_cast<float>(rate_);
  sirenPhase_ += kTwoPi * sirenFreq_ * dt;
  const float siren = static_cast<float>(std::tanh(1.6 * std::sin(sirenPhase_)) * 0.8 + 0.25 * std::sin(2 * sirenPhase_)) * sirenNear_ * std::sqrt(sirenNear_);
  // The fire engine's siren is a slow wail: the pitch rises and falls smoothly over about three and a
  // half seconds ("uuhh-wwiiww-hhuu"), with a little vibrato and a vowel-like roundness, where the
  // ambulance's is a quick alternation of two notes.
  const double sweep = 0.5 - 0.5 * std::cos(kTwoPi * t_ / 3.5);
  const double wailHz = (430.0 + 520.0 * sweep) * (1.0 + 0.012 * std::sin(kTwoPi * 5.5 * t_));
  wailPhase_ += kTwoPi * wailHz * dt;
  const float wail = static_cast<float>(std::tanh(1.3 * std::sin(wailPhase_)) * 0.75 + 0.3 * std::sin(2 * wailPhase_ + 0.7) +
                                        0.15 * std::sin(0.5 * wailPhase_)) *
                     wailNear_ * std::sqrt(wailNear_) * (0.8f + 0.2f * static_cast<float>(sweep));
  // A train: a deep rumble and the clatter of wheels over the rail joints, and a horn as it arrives.
  // The wheels beat out "klack-klack ... klack-klack": two quick knocks, then a pause, over and over
  // (the two axles of a bogie crossing a rail joint). Each knock is a woody thunk, not a click.
  // The wheels only knock while the train rolls, and slower as it slows down; standing at the
  // platform it is silent, apart from the squeal of the brakes and the hiss of the air as it stops.
  trainClock_ += dt * trainMove_ * (1.0 + 0.2 * trainNear_);
  if (trainClock_ >= kKlackPeriod) trainClock_ -= kKlackPeriod;
  float clack = 0, thunk = 0;
  for (int k = 0; k < 2; ++k) {
    const double tau = trainClock_ - k * 0.21;
    if (tau < 0 || tau > 0.4) continue;
    const double rise = 1.0 - std::exp(-tau / 0.004);
    const double gain = k ? 0.8 : 1.0;  // the second knock is a little softer
    clack += static_cast<float>(gain * rise * std::exp(-tau / 0.05));
    thunk += static_cast<float>(gain * rise * std::exp(-tau / 0.09) * std::sin(kTwoPi * (k ? 105.0 : 92.0) * tau));
  }
  const double squealHz = 1500.0 + 900.0 * trainMove_;  // the pitch falls as the train slows
  trainSquealPhase_ += kTwoPi * squealHz * dt;
  const float squeal = static_cast<float>(std::tanh(1.5 * std::sin(trainSquealPhase_)) * 0.6 + 0.4 * std::sin(trainSquealPhase_ * 1.51 + 0.4)) *
                       trainBrake_ * (0.85f + 0.15f * static_cast<float>(std::sin(kTwoPi * 7.0 * t_))) * trainNear_;  // a wavering squeal
  if (trainNear_ > 0.2f && !trainHorned_) { trainHorned_ = true; trainHornStart_ = t_; }
  if (trainNear_ < 0.03f) trainHorned_ = false;
  float trainHorn = 0;
  const double th = t_ - trainHornStart_;
  if (th >= 0 && th < 1.4) {
    const double env = std::min(1.0, th / 0.05) * std::min(1.0, (1.4 - th) / 0.25);
    trainHornPhase_ += kTwoPi * 311.0 * dt;
    trainHorn = static_cast<float>(env * (std::tanh(2.0 * std::sin(trainHornPhase_)) + 0.7 * std::tanh(2.0 * std::sin(trainHornPhase_ * 1.26)))) * 0.5f;
  }
  // A fire: a low roar and crackling.
  const bool crackle = rng_.uniform() < std::min(1.0, 90.0 * fireNear_ * dt * 4.0);
  const float spark = crackle ? static_cast<float>(rng_.range(0.4, 1.0)) * (rng_.chance(0.5) ? 1.0f : -1.0f) : 0.0f;
  fireCrack_ += (spark - fireCrack_) * 0.35f;

  // The sources the world has a place for sound from where they are: the sirens, the train's knocks and
  // horn and the fire's sparks are mono sounds, placed at their emitters (a place that is off is silent).
  // (Switched off once the sound has died away and is not wanted any more; a wanted one that is still
  // swelling in stays on, so that it is placed from its first moment.)
  if (sirenNear_ < 1e-3f && !(p.siren > 0)) place_[kSiren].on = false;
  if (wailNear_ < 1e-3f && !(p.wail > 0)) place_[kWail].on = false;
  if (trainNear_ < 1e-3f && !(p.train > 0)) place_[kTrain].on = false;
  if (planeNear_ < 1e-3f && !(p.plane > 0)) place_[kPlane].on = false;
  if (fireNear_ < 1e-3f && !(p.fire > 0)) place_[kFire].on = false;
  if (place_[kSiren].on) place_[kSiren].v.add(siren * 0.15f * kStereoRef, room_, nch);
  if (place_[kWail].on) place_[kWail].v.add(wail * 0.13f * kStereoRef, room_, nch);
  if (place_[kTrain].on) place_[kTrain].v.add((thunk * 0.12f * trainNear_ * trainMove_ + trainHorn * 0.05f * trainNear_ + squeal * 0.08f) * kStereoRef, room_, nch);
  if (place_[kFire].on) place_[kFire].v.add(fireCrack_ * 0.3f * fireNear_ * kStereoRef, room_, nch);

  // Traffic is the bed, one chain per speaker. The train's rumble and clatter, the fire's roar and the
  // jet are noise too, but they come from somewhere: each chain plays its own noise (so they stay wide
  // and unlike one another) weighted by how near the speaker is to the source. The first two chains
  // always run, so that the draws of the shared generator do not depend on where anything is.
  for (int ch = 0; ch < chains_; ++ch) {
    const bool bed = live(AmbientKind::City, ch);
    const float wTrain = weight(kTrain, ch), wFire = weight(kFire, ch), wPlane = weight(kPlane, ch);
    const bool train = ch < 2 || wTrain >= 1e-3f, fire = ch < 2 || wFire >= 1e-3f, plane = ch < 2 || wPlane >= 1e-3f;
    if (!bed && !train && !fire && !plane) continue;
    Rng& r = chainRng(ch);
    if (bed) {
      const float n = noise(r) * noiseScale_;
      carLow_[ch][0] += (n - carLow_[ch][0]) * lowCar_;
      carLow_[ch][1] += (carLow_[ch][0] - carLow_[ch][1]) * lowCar_;
      const float n2 = noise(r) * noiseScale_;
      carMid_[ch][0] += (n2 - carMid_[ch][0]) * lowCarMid_;
      carMid_[ch][1] += (n2 - carMid_[ch][1]) * lowCarTop_;
      out[ch] = carLow_[ch][1] * 9.0f * 0.075f * day + (carMid_[ch][0] - carMid_[ch][1]) * 0.45f * 0.04f * day;
    }
    float placed = 0;
    if (train) {
      const float n3 = noise(r) * noiseScale_;
      trainLow_[ch][0] += (n3 - trainLow_[ch][0]) * lowTrain_;
      trainLow_[ch][1] += (trainLow_[ch][0] - trainLow_[ch][1]) * lowTrain_;
      const float n4 = noise(r) * noiseScale_;
      trainBand_[ch][0] += (n4 - trainBand_[ch][0]) * lowTrainBand_;
      trainBand_[ch][1] += (n4 - trainBand_[ch][1]) * lowKlack_;
      trainHissLow_[ch] += (n4 - trainHissLow_[ch]) * 0.25f;  // what the low-pass leaves of the noise is the hiss
      placed += wTrain * ((trainLow_[ch][1] * 9.0f * 0.07f * trainNear_ * trainNear_ +
                           (trainBand_[ch][0] - trainBand_[ch][1]) * 1.6f * clack * trainNear_) * trainMove_ +
                          (n4 - trainHissLow_[ch]) * 0.9f * trainHiss_ * trainNear_);
    }
    if (fire) {
      const float n5 = noise(r) * noiseScale_;
      fireLow_[ch][0] += (n5 - fireLow_[ch][0]) * lowFire_;
      fireLow_[ch][1] += (fireLow_[ch][0] - fireLow_[ch][1]) * lowFire_;
      placed += wFire * fireLow_[ch][1] * 9.0f * 0.11f * fireNear_;
    }
    if (plane) {
      const float n6 = noise(r) * noiseScale_;
      planeLow_[ch][0] += (n6 - planeLow_[ch][0]) * lowJet_;
      planeLow_[ch][1] += (planeLow_[ch][0] - planeLow_[ch][1]) * lowJet_;
      placed += wPlane * planeLow_[ch][1] * 9.0f * 0.06f * planeNear_ * planeNear_;
    }
    voiceOut_[ch] += placed;
  }
  flushRoom(voiceOut_, mix_[static_cast<int>(AmbientKind::City)]);
}

void Ambience::aquariumSample(const AmbientParams& p, float out[kMaxChannels]) {
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
  // What its tentacles catch is gulped down: two soft glugs, each a tone that sinks in pitch and
  // wobbles a little, heard through the water (low-passed, so it is muffled).
  float gulp = 0;
  for (const Gulp& g : gulps_) {
    const double tau = t_ - g.start;
    if (!g.used || tau < 0 || tau > 0.5) continue;
    for (int k = 0; k < 2; ++k) {  // the second glug follows a moment later, lower
      const double lt = tau - k * 0.2;
      if (lt < 0 || lt > 0.17) continue;
      const double u = lt / 0.17;
      const double hz = g.pitch * (k ? 0.8 : 1.0) * (250.0 - 120.0 * u) * (1.0 + 0.06 * std::sin(kTwoPi * 24.0 * lt));
      gulpPhase_ += kTwoPi * hz * dt;
      const double env = std::sin(3.14159265 * u);
      gulp += static_cast<float>(env * env * (std::sin(gulpPhase_) + 0.45 * std::sin(2 * gulpPhase_ + 0.5)));
    }
  }
  gulpLow_[0] += (gulp - gulpLow_[0]) * lowGulp_;
  gulpLow_[1] += (gulpLow_[0] - gulpLow_[1]) * lowGulp_;

  // Hum, flow and growl are the bed, one chain per speaker; bubbles, song and gulps are voices,
  // which only the stereo pair carries until they are placed.
  for (int ch = 0; ch < chains_; ++ch) {
    if (!live(AmbientKind::Aquarium, ch)) continue;
    Rng& r = chainRng(ch);
    const float n = noise(r) * noiseScale_;
    waterLow_[ch][0] += (n - waterLow_[ch][0]) * lowHum_;
    waterLow_[ch][1] += (waterLow_[ch][0] - waterLow_[ch][1]) * lowHum_;
    const float n2 = noise(r) * noiseScale_;
    muffleLow_[ch] += (n2 - muffleLow_[ch]) * lowMuffle_;
    const float n3 = noise(r) * noiseScale_;
    deepLow_[ch][0] += (n3 - deepLow_[ch][0]) * lowDeep_;
    deepLow_[ch][1] += (deepLow_[ch][0] - deepLow_[ch][1]) * lowDeep_;
    if (ch < 2) {
      const float panSong = ch ? std::sin(song_.pan * 1.5708f) : std::cos(song_.pan * 1.5708f);
      out[ch] = waterLow_[ch][1] * 9.0f * 0.11f * swell + muffleLow_[ch] * 0.05f * swell +
                deepLow_[ch][1] * 20.0f * growl + bub[ch] + song * panSong * 1.4f + gulpLow_[1] * 0.45f;
    } else {
      out[ch] = waterLow_[ch][1] * 9.0f * 0.11f * swell + muffleLow_[ch] * 0.05f * swell + deepLow_[ch][1] * 20.0f * growl;
    }
  }
}

float Ambience::coeff(float seconds) const {
  return 1.0f - std::exp(-1.0f / (static_cast<float>(rate_) * seconds));
}

void Ambience::menaceSample(const AmbientParams& p, float out[kMaxChannels]) {
  std::fill(out, out + chains_, 0.0f);
  if (menace_ < 1e-3f) {  // nothing of the catastrophe sounds: its placed sources are off (unless it is just swelling in)
    if (!(p.menace > 0)) place_[kTitan].on = place_[kWinged].on = place_[kFlames].on = false;
    return;
  }
  if (flames_ < 1e-3f && !(p.flames > 0)) place_[kFlames].on = false;
  if (wingedNear_ < 1e-3f && !(p.winged > 0)) place_[kWinged].on = false;
  const int nch = layout_.channels();
  const double dt = 1.0 / rate_;
  const float level = menace_;
  // The rush, the hiss and the rotor's chop are noise, one chain per speaker; the whine, the
  // thumps, the roars and the bubbles are voices of the stereo pair.
  const double chop = p.kind == AmbientKind::Desert ? std::pow(0.5 + 0.5 * std::sin(kTwoPi * 11.5 * t_), 2.2) : 0.0;
  for (int ch = 0; ch < chains_; ++ch) {
    if (!live(p.kind, ch)) continue;
    const float n = noise(chainRng(ch)) * noiseScale_;
    menaceLow_[ch][0] += (n - menaceLow_[ch][0]) * lowThump_;
    menaceLow_[ch][1] += (menaceLow_[ch][0] - menaceLow_[ch][1]) * lowThump_;
    out[ch] = 0;
    switch (p.kind) {
      case AmbientKind::Aquarium: {  // poison gas: a hiss that wells up, and glugging bubbles
        menaceHigh_[ch] += (n - menaceHigh_[ch]) * lowHiss_;
        out[ch] = (n - menaceHigh_[ch]) * 0.3f * level * (0.7f + 0.3f * static_cast<float>(std::sin(kTwoPi * 0.7 * t_ + ch)));
        break;
      }
      case AmbientKind::Forest: {  // a flood: the deep roar of rushing water and the hiss of its surface
        menaceHigh_[ch] += (n - menaceHigh_[ch]) * lowFloodTop_;
        const float swell = 0.75f + 0.25f * static_cast<float>(std::sin(kTwoPi * 0.31 * t_ + 1.7 * ch));
        out[ch] = (menaceLow_[ch][1] * 3.2f + (n - menaceHigh_[ch]) * 0.06f + menaceHigh_[ch] * 0.28f) * 0.55f * level * swell;
        break;
      }
      case AmbientKind::Desert: {  // a helicopter: the chop of the rotor, over a whine
        menaceHigh_[ch] += (n - menaceHigh_[ch]) * lowChop_;
        float whine = 0;
        if (ch < 2) {  // (advanced once per stereo channel, as it always was)
          menacePhase_[1] += kTwoPi * 390.0 * dt * 0.5;
          whine = 0.012f * static_cast<float>(std::sin(menacePhase_[1]));
        }
        out[ch] = (menaceHigh_[ch] * static_cast<float>(chop) * 1.9f + menaceLow_[ch][1] * 1.5f * static_cast<float>(chop) +
                   whine) * 0.7f * level;
        break;
      }
      case AmbientKind::Moon: {  // the big meteor: a rumble in the ground that swells as it nears
        out[ch] = menaceLow_[ch][1] * 4.5f * level * level;
        break;
      }
      default: break;
    }
  }
  if (p.kind == AmbientKind::Aquarium && rng_.uniform() < 5.5 * level * dt) {  // bubbles of gas
    bubble(0, static_cast<float>(rng_.range(230, 560)), 0.12f, static_cast<float>(rng_.range(0.05, 0.11)) * level);
  }
  if (p.kind == AmbientKind::City && flames_ > 1e-3f) {
    // The streams of fire: one steady, rushing roar (broad noise over a deep body), only unevenly
    // swelling and with a few faint crackles. Nothing in it beats in time.
    flameWander_ += (noise(rng_) - flameWander_) * static_cast<float>(1.0 - std::exp(-kTwoPi * 2.0 * dt));
    const float swell = 0.88f + 0.12f * std::clamp(flameWander_ * 45.0f, -1.0f, 1.0f);
    // The streams are placed at the monsters' fight: every chain plays its own noise, weighted by how
    // near its speaker is to the streams (the first two always run, to keep the generator's draws).
    for (int ch = 0; ch < chains_; ++ch) {
      const float w = weight(kFlames, ch);
      if (ch >= 2 && w < 1e-3f) continue;
      Rng& r = chainRng(ch);
      const float n = noise(r) * noiseScale_;
      flameHi_[ch] += (n - flameHi_[ch]) * lowFlameTop_;
      flameLo_[ch] += (n - flameLo_[ch]) * lowFlameBottom_;
      const float band = flameHi_[ch] - flameLo_[ch];  // the rush
      const float body = flameLo_[ch];                 // the deep roar under it
      float crackle = 0;
      if (r.uniform() < 14.0 * dt) crackle = noise(r) * 0.18f;
      menaceBus_[ch] += w * (band * 3.2f + body * 1.8f + crackle) * swell * 0.7f * flames_;
    }
  }
  if (p.kind != AmbientKind::Mountains && p.kind != AmbientKind::City) return;
  // A creature: heavy steps and a roar now and then. The monsters of the city are bigger and slower.
  const bool big = p.kind == AmbientKind::City;
  const double period = big ? 0.95 : 0.55;
  const double ph = std::fmod(t_, period);
  const float thump = static_cast<float>(std::exp(-ph / (big ? 0.2 : 0.11)) * std::sin(kTwoPi * (big ? 44.0 : 62.0) * ph));
  const float stampRumble = (big ? 1.3f : 1.2f) * level * static_cast<float>(std::exp(-ph / 0.25));
  if (big) {
    // The stamps are placed at the lizard: the thump is one mono sound with a full bass send, the
    // rumble under it noise per chain, weighted by how near each speaker is.
    if (place_[kTitan].on) place_[kTitan].v.add(thump * 0.36f * level * kStereoRef, room_, nch);
    for (int ch = 0; ch < chains_; ++ch) {
      const float w = weight(kTitan, ch);
      if (w >= 1e-3f && live(p.kind, ch)) menaceBus_[ch] += w * menaceLow_[ch][1] * stampRumble;
    }
  } else {
    out[0] += thump * 0.38f * level + menaceLow_[0][1] * stampRumble;
    out[1] += thump * 0.38f * level + menaceLow_[1][1] * stampRumble;
  }
  if (nextMenaceRoar_ == 0) nextMenaceRoar_ = t_ + rng_.range(0.5, 1.5);
  if (t_ >= nextMenaceRoar_) {
    menaceRoarStart_ = t_;
    menaceRoarDur_ = rng_.range(1.3, 2.0);
    menaceRoarVoice_ = big && wingedNear_ > 0.5f ? (menaceRoarVoice_ + 1) % 2 : 0;  // the screech needs the winged one alive
    nextMenaceRoar_ = t_ + menaceRoarDur_ + rng_.range(2.0, 4.5);
  }
  const double rt = t_ - menaceRoarStart_;
  if (rt >= 0 && rt < menaceRoarDur_) {
    const double u = rt / menaceRoarDur_;
    const double env = std::min(1.0, rt / 0.15) * std::min(1.0, (menaceRoarDur_ - rt) / 0.5);
    // The yeti and the lizard bellow low; the winged monster screeches high.
    const double hz = menaceRoarVoice_ == 1 ? 520.0 + 380.0 * std::sin(3.14159265 * u) : (big ? 58.0 : 96.0) + 50.0 * std::sin(3.14159265 * std::pow(u, 0.8));
    menacePhase_[0] += kTwoPi * hz * dt;
    const double tone = std::tanh(2.2 * std::sin(menacePhase_[0]) + 1.1 * std::sin(2 * menacePhase_[0] + 0.5) + 0.5 * std::sin(3 * menacePhase_[0] + 1.0));
    const float roar = static_cast<float>(tone * env * (1.0 + 0.35 * std::sin(kTwoPi * 19.0 * t_))) * level * (menaceRoarVoice_ == 1 ? 0.09f * wingedNear_ : 0.14f);
    if (big) {  // the lizard bellows from where it stands, the winged monster screeches from where it flies
      const int id = menaceRoarVoice_ == 1 ? kWinged : kTitan;
      if (place_[id].on) place_[id].v.add(roar * kStereoRef, room_, nch);
    } else {
      out[0] += roar;
      out[1] += roar;
    }
  }
}

void Ambience::render(const AmbientParams& p, float* out, int frames) {
  const float kPresence = coeff(0.6f), kRain = coeff(1.5f), kNight = coeff(3.0f), kVolume = coeff(0.05f), kStorm = coeff(2.0f), kSlide = coeff(0.4f), kFast = coeff(12.0f);
  const float lowRoll = coeff(1.0f / 0.35f / 6.28f);
  const float rollScale = 1.0f / (0.577f * std::sqrt(lowRoll / 2.0f));
  const float lowRain = coeff(1.0f / 2500.0f / 6.28f);
  const float lowCrack = coeff(1.0f / 1800.0f / 6.28f), lowRumble = coeff(1.0f / 140.0f / 6.28f);
  const int channels = layout_.channels();
  const float bassNorm = 1.0f / std::sqrt(static_cast<float>(chains_));
  const float kRoute = coeff(spatial::kSmoothingSeconds);
  const double dt = 1.0 / rate_;
  const float snapHz = std::min(2400.0f, static_cast<float>(rate_) / 7.0f);  // keeps the filter stable
  const float snapF = 2.0f * std::sin(3.14159265f * snapHz / static_cast<float>(rate_));
  constexpr float kSnapDamp = 0.35f;  // 1/Q: rings for a few milliseconds
  const float lowWander = coeff(1.0f / 0.45f / 6.28f);
  const float wanderScale = 1.0f / (0.577f * std::sqrt(lowWander / 2.0f));  // back to unit spread

  if (p.kind == AmbientKind::City) aimCity(p);  // the placed sounds follow their emitters, once per block
  if (!flashesSynced_) { seenFlashes_ = p.flashes; flashesSynced_ = true; }
  // Lightning: each new flash brings an immediate clap.
  if (p.flashes != seenFlashes_) {
    // (The counter drops when the world is rebuilt; that is no lightning.)
    if (p.flashes > seenFlashes_ && p.kind == AmbientKind::Forest) {
      clap(rng_.range(0.03, 0.10), 1.0, true);  // the strike is overhead: sound follows at once
    }
    if (p.flashes > seenFlashes_ && p.kind == AmbientKind::Moon) zapStart_ = t_;  // the alien's flash
    seenFlashes_ = p.flashes;
  }

  for (int i = 0; i < frames; ++i, t_ += dt) {
    for (int k = 0; k < kAmbientKinds; ++k) approach(mix_[k], static_cast<int>(p.kind) == k ? 1.0f : 0.0f, kPresence);
    approach(rain_, p.rain, kRain);
    approach(night_, p.night, kNight);
    approach(storm_, p.storm, kStorm);
    approach(tornado_, p.kind == AmbientKind::Desert ? p.tornado : 0.0f, kStorm);
    approach(whale_, p.kind == AmbientKind::Aquarium ? p.whale : 0.0f, kStorm);
    approach(avalanche_, p.kind == AmbientKind::Mountains ? p.avalanche : 0.0f, kSlide);
    approach(wormNear_, p.kind == AmbientKind::Desert ? p.worm : 0.0f, kStorm);
    approach(sirenNear_, p.kind == AmbientKind::City ? p.siren : 0.0f, kRain);
    approach(wailNear_, p.kind == AmbientKind::City ? p.wail : 0.0f, kRain);
    approach(trainNear_, p.kind == AmbientKind::City ? p.train : 0.0f, kRain);
    approach(trainMove_, p.trainMotion, kRain);
    approach(trainBrake_, p.trainBrake, kRain);
    approach(trainHiss_, p.trainHiss, kRain);
    approach(planeNear_, p.kind == AmbientKind::City ? p.plane : 0.0f, kStorm);
    approach(fireNear_, p.kind == AmbientKind::City ? p.fire : 0.0f, kRain);
    approach(menace_, p.menace, kStorm);
    approach(flames_, p.kind == AmbientKind::City ? p.flames : 0.0f, kFast);
    approach(wingedNear_, p.kind == AmbientKind::City ? (p.winged > 0.0f ? 1.0f : 0.0f) : 0.0f, kFast);
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
    // The forest's own sounds (wind, rain, thunder, crickets) are made only while the forest is
    // audible: its presence below 0.001 is silence, as for the other scenes. What the other scenes
    // share with it (the gust, and every draw from the random generator) runs regardless, so that
    // the noise and the events of the other scenes are exactly what they were when this chain
    // always ran.
    const bool forestOn = mix_[static_cast<int>(AmbientKind::Forest)] > 1e-3f;
    // Soft wind in the trees is low-passed noise that swells with the gusts. A gust lifts the
    // pitch a little; in a storm the wind is deeper and stronger.
    float lowWind = 0, windGain = 0;
    if (forestOn) {
      const float cutoffHz = (230.0f + 230.0f * gust) * (1.0f - 0.35f * rain_);
      lowWind = 1.0f - std::exp(-6.2831853f * cutoffHz / static_cast<float>(rate_));
      windGain = (0.3f + 0.9f * gust * gust) * (1.0f + 2.0f * rain_) * 0.10f;
    }
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
    const float chirps = !forestOn ? 0.0f : night_ * 0.035f *
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
    // Things the sandworm swallows and animals an avalanche buries (the first values seen are starting points).
    if (!crushesSynced_) { seenCrushes_ = p.crushes; crushesSynced_ = true; }
    if (p.crushes != seenCrushes_) {
      if (p.crushes > seenCrushes_ && p.kind == AmbientKind::Desert) {
        for (unsigned n = 0; n < std::min(p.crushes - seenCrushes_, 3u); ++n) {
          Crunch& c = *std::min_element(std::begin(crunches_), std::end(crunches_),
                                        [](const Crunch& a, const Crunch& b) { return a.start < b.start; });
          c.start = t_ + n * rng_.range(0.05, 0.2);
          c.gain = static_cast<float>(rng_.range(0.8, 1.0));
          c.bites = rng_.rangeInt(6, Crunch::kBites);
          float at = static_cast<float>(rng_.range(0.0, 0.05));
          for (int b = 0; b < c.bites; ++b) {  // irregular chewing that eases off
            c.bite[b] = at;
            c.force[b] = static_cast<float>(rng_.range(0.55, 1.0)) * (1.0f - 0.5f * b / c.bites);
            at += static_cast<float>(rng_.range(0.07, 0.13));
          }
        }
      }
      if (p.crushes > seenCrushes_ && p.kind == AmbientKind::Aquarium) {
        for (unsigned n = 0; n < std::min(p.crushes - seenCrushes_, 3u); ++n) {
          Gulp& g = *std::min_element(std::begin(gulps_), std::end(gulps_),
                                      [](const Gulp& a, const Gulp& b) { return a.start < b.start; });
          g.start = t_ + n * rng_.range(0.08, 0.3);
          g.pitch = static_cast<float>(rng_.range(0.85, 1.2));
          g.used = true;
        }
      }
      seenCrushes_ = p.crushes;
    }
    if (!squealsSynced_) { seenSqueals_ = p.squeals; squealsSynced_ = true; }
    if (p.squeals != seenSqueals_) {
      if (p.squeals > seenSqueals_ && p.kind == AmbientKind::Mountains) {
        for (unsigned n = 0; n < std::min(p.squeals - seenSqueals_, 3u); ++n) {
          Squeal& s = *std::min_element(std::begin(squealVoices_), std::end(squealVoices_),
                                        [](const Squeal& a, const Squeal& b) { return a.start < b.start; });
          s.start = t_ + n * rng_.range(0.05, 0.25);
          s.pitch = static_cast<float>(rng_.range(0.8, 1.3));
          s.used = true;
        }
      }
      seenSqueals_ = p.squeals;
    }
    // Meteorite strikes and the alien's flash on the moon (first values seen are starting points).
    if (!impactsSynced_) { seenImpacts_ = p.impacts; impactsSynced_ = true; }
    if (p.impacts != seenImpacts_) {
      if (p.impacts > seenImpacts_ && p.kind == AmbientKind::Moon) {
        Impact& im = *std::min_element(std::begin(impacts_), std::end(impacts_),
                                       [](const Impact& a, const Impact& b) { return a.start < b.start; });
        im.start = t_ + rng_.range(0.0, 0.05);
        im.gain = 1.0f;
      }
      seenImpacts_ = p.impacts;
    }
    // Rifle shots (the poachers' helicopter): each one cracks (the first value seen is the starting point).
    if (!shotsSynced_) { seenShots_ = p.shots; shotsSynced_ = true; }
    if (p.shots != seenShots_) {
      if (p.shots > seenShots_ && p.kind == AmbientKind::Desert) {
        Shot& s = *std::min_element(std::begin(shotVoices_), std::end(shotVoices_),
                                    [](const Shot& a, const Shot& b) { return a.start < b.start; });
        s.start = t_ + rng_.range(0.0, 0.06);
        s.pan = static_cast<float>(rng_.range(0.25, 0.75));
      }
      seenShots_ = p.shots;
    }
    if ((driftTick_++ & 31u) == 0) updateDrift();
    std::fill(voiceOut_, voiceOut_ + chains_, 0.0f);
    std::fill(menaceBus_, menaceBus_ + chains_, 0.0f);
    bassExtra_ = 0;
    stepPlaces();
    float menaceOut[kMaxChannels];
    menaceSample(p, menaceOut);
    flushRoom(menaceBus_, 1.0f);
    for (const Shot& s : shotVoices_) {  // a sharp crack that dies away at once, over a short thump
      const double tau = t_ - s.start;
      if (tau < 0 || tau > 0.4) continue;
      const float crack = noise(rng_) * static_cast<float>(std::exp(-tau / 0.012)) * 0.34f;
      const float thump = static_cast<float>(std::sin(kTwoPi * 150.0 * tau) * std::exp(-tau / 0.05)) * 0.22f;
      menaceOut[0] += (crack + thump) * (1.2f - s.pan);
      menaceOut[1] += (crack + thump) * (0.2f + s.pan);
    }
    float aq[kMaxChannels], desert[kMaxChannels], moon[kMaxChannels], mountains[kMaxChannels], city[kMaxChannels];
    std::fill(aq, aq + chains_, 0.0f);
    std::fill(desert, desert + chains_, 0.0f);
    std::fill(moon, moon + chains_, 0.0f);
    std::fill(mountains, mountains + chains_, 0.0f);
    std::fill(city, city + chains_, 0.0f);
    if (mix_[static_cast<int>(AmbientKind::Aquarium)] > 1e-3f) aquariumSample(p, aq);
    if (mix_[static_cast<int>(AmbientKind::Desert)] > 1e-3f) desertSample(p, desert);
    if (mix_[static_cast<int>(AmbientKind::Moon)] > 1e-3f) moonSample(p, moon);
    if (mix_[static_cast<int>(AmbientKind::Mountains)] > 1e-3f) mountainSample(p, mountains);
    if (mix_[static_cast<int>(AmbientKind::City)] > 1e-3f) citySample(p, city);

    const int kind = static_cast<int>(p.kind);
    float chain[kMaxChannels];  // what each bed chain plays; chains 0 and 1 are the stereo pair
    float bassIn = 0;
    for (int ch = 0; ch < chains_; ++ch) {
      float forest = 0;
      Rng& r = chainRng(ch);
      if (forestOn && live(AmbientKind::Forest, ch)) {
        // Wind: noise through two low-pass stages (no hiss, no crackle).
        const float wn = noise(r) * noiseScale_;
        windLow_[ch][0] += (wn - windLow_[ch][0]) * lowWind;
        windLow_[ch][1] += (windLow_[ch][0] - windLow_[ch][1]) * lowWind;
        const float wind = windLow_[ch][1] * 9.0f * windGain;

        // Rain: the hiss is noise with its low end removed, plus a quiet rumble beneath it.
        const float n = noise(r) * noiseScale_;
        rainLow_[ch] += (n - rainLow_[ch]) * lowRain;
        const float rain = ((n - rainLow_[ch]) * 0.30f + rainLow_[ch] * 0.9f) * rain_;

        // Thunder: a brief crack of mid-high noise, then deep rumbling noise.
        float thunder = 0;
        if (snapEnv > 1e-3f && ch < 2) {  // the lightning's snap is a voice of the stereo pair
          snapLow_[ch] += snapF * snapBand_[ch];
          const float high = click * 9.0f - snapLow_[ch] - kSnapDamp * snapBand_[ch];
          snapBand_[ch] += snapF * high;
          thunder += snapBand_[ch] * snapEnv;
        }
        if (crackEnv > 1e-4f || rumbleEnv > 1e-4f) {
          const float tn = noise(r) * noiseScale_;
          crackLow_[ch] += (tn - crackLow_[ch]) * lowCrack;
          rumbleLow_[ch][0] += (tn - rumbleLow_[ch][0]) * lowRumble;
          rumbleLow_[ch][1] += (rumbleLow_[ch][0] - rumbleLow_[ch][1]) * lowRumble;
          thunder += crackLow_[ch] * crackEnv * 0.85f + rumbleLow_[ch][1] * rumbleEnv * 17.0f;
        }
        forest = wind + rain + thunder + (ch < 2 ? chirps * (ch ? 0.9f : 1.0f) : 0.0f);  // crickets are voices too
      } else if (ch < 2) {
        // Silent, but the generator advances as if the chain had run: one draw each for the wind
        // and the rain, and one for the thunder's noise while a clap is still ringing.
        noise(rng_);
        noise(rng_);
        if (crackEnv > 1e-4f || rumbleEnv > 1e-4f) noise(rng_);
      }

      // Every scene's level in this chain: its presence times the chain's own level (1 for the stereo pair).
      const float sample = menaceOut[ch] * trim_[kind][ch] + menaceBus_[ch] +
                           forest * (mix_[static_cast<int>(AmbientKind::Forest)] * bedGain_[static_cast<int>(AmbientKind::Forest)][ch]) +
                           aq[ch] * (mix_[static_cast<int>(AmbientKind::Aquarium)] * bedGain_[static_cast<int>(AmbientKind::Aquarium)][ch]) +
                           desert[ch] * (mix_[static_cast<int>(AmbientKind::Desert)] * bedGain_[static_cast<int>(AmbientKind::Desert)][ch]) +
                           moon[ch] * (mix_[static_cast<int>(AmbientKind::Moon)] * bedGain_[static_cast<int>(AmbientKind::Moon)][ch]) +
                           mountains[ch] * (mix_[static_cast<int>(AmbientKind::Mountains)] * bedGain_[static_cast<int>(AmbientKind::Mountains)][ch]) +
                           city[ch] * (mix_[static_cast<int>(AmbientKind::City)] * bedGain_[static_cast<int>(AmbientKind::City)][ch]) +
                           voiceOut_[ch] * mix_[static_cast<int>(AmbientKind::City)];
      chain[ch] = softClip(sample) * volume_ * 0.8f;
      bassIn += chain[ch];
    }
    // The bass channel plays what is low in all the chains together: their sum, scaled to the level
    // of one chain (they are independent), through two low-pass stages.
    if (layout_.lfeIndex() >= 0) {
      bassIn = bassIn * bassNorm + bassExtra_;  // plus the bass sends of the placed sounds
      bassLow_[0] += (bassIn - bassLow_[0]) * bassK_;
      bassLow_[1] += (bassLow_[0] - bassLow_[1]) * bassK_;
    }
    float* frame = out + static_cast<size_t>(i) * channels;
    for (int c = 0; c < channels; ++c) {
      routeGain_[c] += (routeTarget_[c] - routeGain_[c]) * kRoute;
      const int f = feed_[c];
      const float x = f == kSilent ? 0.0f : f == kBoth ? 0.5f * (chain[0] + chain[1]) : f == kBass ? std::clamp(bassLow_[1], -1.0f, 1.0f) : chain[f];
      frame[c] = x * routeGain_[c];
    }
  }
  pendingCount_ = 0;  // the events of this block have been heard (by the sounds that use them, in phase 6)
}

}  // namespace dio
