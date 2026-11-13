/// @file core/spatializer.cpp
/// @brief Panning law (stereo sine/cosine, DBAP for everything else), spread and bass send.

#include "core/spatializer.h"

#include <algorithm>
#include <cmath>

namespace dio {
namespace {

/// Pi, for degree conversions.
constexpr float kPi = 3.14159265358979f;

/// `v` if it is a finite number, else `fallback`.
float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

/// Unit vector (x right, y ahead, z up) of a direction in degrees.
void unitVector(float azimuth, float elevation, float v[3]) {
  const float a = azimuth * kPi / 180.0f, e = elevation * kPi / 180.0f;
  v[0] = std::sin(a) * std::cos(e);
  v[1] = std::cos(a) * std::cos(e);
  v[2] = std::sin(e);
}

/// Scales the gains of the listed channels so that their squares sum to 1.
/// @return false if they were all (nearly) zero, in which case nothing is changed
bool normalize(float* g, const int* idx, int n) {
  float sum = 0;
  for (int i = 0; i < n; ++i) sum += g[idx[i]] * g[idx[i]];
  if (!(sum > 1e-20f)) return false;
  const float inv = 1.0f / std::sqrt(sum);
  for (int i = 0; i < n; ++i) g[idx[i]] *= inv;
  return true;
}

}  // namespace

void Spatializer::gains(const Layout& layout, const Source& source, float out[kMaxChannels]) {
  std::fill(out, out + kMaxChannels, 0.0f);
  int idx[kMaxChannels];  // the channels the panner may use
  int n = 0;
  for (int i = 0; i < layout.channels(); ++i)
    if (layout.speaker(i).isDirectional()) idx[n++] = i;
  if (n == 0) return;

  const float az = finiteOr(source.azimuth, 0), el = finiteOr(source.elevation, 0);
  const float spread = std::clamp(finiteOr(source.spread, 0), 0.0f, 1.0f);

  if (n == 1) {
    out[idx[0]] = 1.0f;
    return;
  }
  if (layout.isStereo()) {
    // The synth's own law, driven by the azimuth: +-90 degrees is hard left or right.
    const float pan = std::clamp(0.5f + 0.5f * std::sin(az * kPi / 180.0f), 0.0f, 1.0f);
    out[idx[0]] = std::max(0.0f, std::cos(pan * kPi * 0.5f));  // cos(pi/2) is -4e-8 in float
    out[idx[1]] = std::max(0.0f, std::sin(pan * kPi * 0.5f));
  } else {
    float s[3];
    unitVector(az, el, s);
    for (int k = 0; k < n; ++k) {
      const Speaker& sp = layout.speaker(idx[k]);
      float p[3];
      unitVector(sp.azimuth, sp.elevation, p);
      const float dx = s[0] - p[0], dy = s[1] - p[1], dz = s[2] - p[2];
      const float d2 = dx * dx + dy * dy + dz * dz + spatial::kBlur * spatial::kBlur;
      out[idx[k]] = sp.weight * std::pow(d2, -0.5f * spatial::kRolloff);
    }
  }
  if (!normalize(out, idx, n)) {
    for (int k = 0; k < n; ++k) out[idx[k]] = 1.0f;  // degenerate weights: fall back to an even spread
  }

  if (spread > 0) {
    const float even = 1.0f / std::sqrt(static_cast<float>(n));
    for (int k = 0; k < n; ++k) out[idx[k]] = (1.0f - spread) * out[idx[k]] + spread * even;
  }
  normalize(out, idx, n);
}

SpatialVoice::SpatialVoice(int sampleRate) {
  const float rate = static_cast<float>(std::max(sampleRate, 1));
  k_ = 1.0f - std::exp(-1.0f / (spatial::kSmoothingSeconds * rate));
  lowK_ = 1.0f - std::exp(-2.0f * kPi * spatial::kLfeCutoffHz / rate);
}

void SpatialVoice::setTarget(const Layout& layout, const Source& source, bool snap) {
  Spatializer::gains(layout, source, target_);

  // The bass send goes to the LFE channel. A layout without one folds it into the front
  // speakers (or into all of them if none stands in front), at constant power.
  std::fill(bassTarget_, bassTarget_ + kMaxChannels, 0.0f);
  const float send = std::clamp(finiteOr(source.lfeSend, 0), 0.0f, 1.0f);
  if (layout.lfeIndex() >= 0) {
    bassTarget_[layout.lfeIndex()] = send;
  } else {
    int front[kMaxChannels], all[kMaxChannels];
    int nFront = 0, nAll = 0;
    for (int i = 0; i < layout.channels(); ++i) {
      const Speaker& sp = layout.speaker(i);
      if (!sp.isDirectional()) continue;
      all[nAll++] = i;
      if (std::fabs(sp.azimuth) <= 45.0f && sp.elevation <= 1.0f) front[nFront++] = i;
    }
    const int* use = nFront > 0 ? front : all;
    const int n = nFront > 0 ? nFront : nAll;
    for (int k = 0; k < n; ++k) bassTarget_[use[k]] = send / std::sqrt(static_cast<float>(n));
  }

  if (snap) {
    std::copy(target_, target_ + kMaxChannels, gain_);
    std::copy(bassTarget_, bassTarget_ + kMaxChannels, bassGain_);
  }
}

void SpatialVoice::process(float x, float* frame, int channels) {
  low_[0] += (x - low_[0]) * lowK_;
  low_[1] += (low_[0] - low_[1]) * lowK_;
  const int n = std::min(channels, kMaxChannels);
  for (int c = 0; c < n; ++c) {
    gain_[c] += (target_[c] - gain_[c]) * k_;
    bassGain_[c] += (bassTarget_[c] - bassGain_[c]) * k_;
    frame[c] += x * gain_[c] + low_[1] * bassGain_[c];
  }
}

}  // namespace dio
