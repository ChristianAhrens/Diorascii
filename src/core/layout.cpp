/// @file core/layout.cpp
/// @brief Speaker layout tables and the mapping of device channels onto the canonical ones.

#include "core/layout.h"

#include <algorithm>
#include <cmath>

namespace dio {
namespace {

/// Pi, for degree conversions.
constexpr float kPi = 3.14159265358979f;

/// Bit of a channel in a layout's channel set.
constexpr uint32_t bit(Channel c) { return 1u << static_cast<unsigned>(c); }

/// Channel set of 2.0.
constexpr uint32_t kStereoSet = bit(Channel::L) | bit(Channel::R);
/// Channel set of 5.1.
constexpr uint32_t k51Set = kStereoSet | bit(Channel::C) | bit(Channel::LFE) | bit(Channel::Ls) | bit(Channel::Rs);
/// Channel set of 7.1.
constexpr uint32_t k71Set = k51Set | bit(Channel::Lrs) | bit(Channel::Rrs);
/// The two top-middle channels.
constexpr uint32_t kMidHeights = bit(Channel::Ltm) | bit(Channel::Rtm);
/// The four top-front and top-rear channels.
constexpr uint32_t kQuadHeights = bit(Channel::Ltf) | bit(Channel::Rtf) | bit(Channel::Ltr) | bit(Channel::Rtr);
/// The two wide channels.
constexpr uint32_t kWides = bit(Channel::Lw) | bit(Channel::Rw);

/// The channel set of each named layout, in LayoutId order.
constexpr uint32_t kSets[kLayoutCount] = {
    kStereoSet,                                    // 2.0
    k51Set,                                        // 5.1
    k71Set,                                        // 7.1
    k51Set | kMidHeights,                          // 5.1.2
    k51Set | kQuadHeights,                         // 5.1.4
    k71Set | kMidHeights,                          // 7.1.2
    k71Set | kQuadHeights,                         // 7.1.4
    k71Set | kQuadHeights | kWides,                // 9.1.4
    k71Set | kQuadHeights | kWides | kMidHeights,  // 9.1.6
};

/// The names of the named layouts, in LayoutId order.
constexpr const char* kNames[kLayoutCount] = {"2.0", "5.1", "7.1", "5.1.2", "5.1.4", "7.1.2", "7.1.4", "9.1.4", "9.1.6"};

/// True for the layouts of the 5.1 family, whose side speakers stand further back (110 degrees).
bool isFiveOneFamily(LayoutId id) { return id == LayoutId::S5_1 || id == LayoutId::S5_1_2 || id == LayoutId::S5_1_4; }

/// Unit vector (x right, y ahead, z up) of a direction in degrees.
void unitVector(float azimuth, float elevation, float v[3]) {
  const float a = azimuth * kPi / 180.0f, e = elevation * kPi / 180.0f;
  v[0] = std::sin(a) * std::cos(e);
  v[1] = std::cos(a) * std::cos(e);
  v[2] = std::sin(e);
}

}  // namespace

void canonicalPosition(Channel channel, float& azimuth, float& elevation) {
  azimuth = 0;
  elevation = 0;
  switch (channel) {
    case Channel::L: azimuth = -30; break;
    case Channel::R: azimuth = 30; break;
    case Channel::Lw: azimuth = -60; break;
    case Channel::Rw: azimuth = 60; break;
    case Channel::Ls: azimuth = -90; break;
    case Channel::Rs: azimuth = 90; break;
    case Channel::Lrs: azimuth = -135; break;
    case Channel::Rrs: azimuth = 135; break;
    case Channel::Ltf: azimuth = -45; elevation = 45; break;
    case Channel::Rtf: azimuth = 45; elevation = 45; break;
    case Channel::Ltm: azimuth = -90; elevation = 45; break;
    case Channel::Rtm: azimuth = 90; elevation = 45; break;
    case Channel::Ltr: azimuth = -135; elevation = 45; break;
    case Channel::Rtr: azimuth = 135; elevation = 45; break;
    default: break;  // C straight ahead; LFE and None have no position
  }
}

Channel nearestChannel(float azimuth, float elevation) {
  if (!std::isfinite(azimuth)) azimuth = 0;
  if (!std::isfinite(elevation)) elevation = 0;
  float s[3];
  unitVector(azimuth, elevation, s);
  Channel best = Channel::C;
  float bestDot = -2;
  for (int i = 0; i < kMaxChannels; ++i) {
    const Channel c = static_cast<Channel>(i);
    if (c == Channel::LFE) continue;
    float az, el, p[3];
    canonicalPosition(c, az, el);
    unitVector(az, el, p);
    const float dot = s[0] * p[0] + s[1] * p[1] + s[2] * p[2];
    if (dot > bestDot) {  // strict: the earlier channel wins a tie
      bestDot = dot;
      best = c;
    }
  }
  return best;
}

Layout::Layout() { *this = named(LayoutId::Stereo); }

Layout Layout::named(LayoutId id) {
  Layout out{Empty{}};
  out.name_ = kNames[static_cast<int>(id)];
  const uint32_t set = kSets[static_cast<int>(id)];
  for (int i = 0; i < kMaxChannels; ++i) {
    const Channel c = static_cast<Channel>(i);
    if (!(set & bit(c))) continue;
    Speaker& s = out.speakers_[out.count_];
    s.channel = c;
    canonicalPosition(c, s.azimuth, s.elevation);
    if (isFiveOneFamily(id) && (c == Channel::Ls || c == Channel::Rs)) s.azimuth = c == Channel::Ls ? -110.0f : 110.0f;
    if (c == Channel::LFE) out.lfe_ = out.count_;
    ++out.count_;
  }
  return out;
}

bool Layout::parse(std::string_view text, LayoutId& id) {
  for (int i = 0; i < kLayoutCount; ++i) {
    if (text == kNames[i]) {
      id = static_cast<LayoutId>(i);
      return true;
    }
  }
  return false;
}

const char* Layout::nameOf(LayoutId id) { return kNames[static_cast<int>(id)]; }

Layout Layout::fromDevice(const DeviceChannel* channels, int count) {
  Layout out{Empty{}};
  out.count_ = std::clamp(count, 0, kMaxChannels);
  out.lfe_ = -1;
  out.name_ = "custom";
  for (int i = 0; i < out.count_; ++i) {
    Speaker& s = out.speakers_[i];
    const DeviceChannel& d = channels[i];
    if (d.lfe && out.lfe_ < 0) {
      s = Speaker{Channel::LFE, 0, 0, 1};
      out.lfe_ = i;
    } else if (d.lfe) {
      s = Speaker{Channel::None, 0, 0, 1};  // a second LFE channel stays silent
    } else if (!d.known || !std::isfinite(d.azimuth) || !std::isfinite(d.elevation)) {
      s = Speaker{Channel::None, 0, 0, 1};
    } else {
      s = Speaker{nearestChannel(d.azimuth, d.elevation), d.azimuth, d.elevation, 1};
    }
  }
  return out;
}

bool Layout::hasHeight() const {
  for (int i = 0; i < count_; ++i)
    if (speakers_[i].isDirectional() && speakers_[i].elevation > 1.0f) return true;
  return false;
}

bool Layout::isStereo() const {
  return count_ == 2 && speakers_[0].isDirectional() && speakers_[1].isDirectional() &&
         speakers_[0].azimuth < 0 && speakers_[1].azimuth > 0 && speakers_[0].elevation == 0 &&
         speakers_[1].elevation == 0;
}

}  // namespace dio
