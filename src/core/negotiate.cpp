/// @file core/negotiate.cpp
/// @brief Layout negotiation: positions, ports, choices and the step-down.

#include "core/negotiate.h"

#include <algorithm>

namespace dio {
namespace {

/// The richest named layout that a device with `channels` channels and no map can be given.
LayoutId richestFor(int channels) {
  if (channels >= 16) return LayoutId::S9_1_6;
  if (channels >= 12) return LayoutId::S7_1_4;
  if (channels >= 8) return LayoutId::S7_1;
  if (channels >= 6) return LayoutId::S5_1;
  return LayoutId::Stereo;
}

/// True if `layout`'s channels are those of the named layout `id`, in the same order.
bool sameChannels(const Layout& layout, LayoutId id) {
  const Layout named = Layout::named(id);
  if (layout.channels() != named.channels()) return false;
  for (int i = 0; i < named.channels(); ++i)
    if (layout.speaker(i).channel != named.speaker(i).channel) return false;
  return true;
}

/// The device position of a canonical channel, with a name for every channel of the named layouts (None for a
/// channel that is not one of them).
DevicePosition positionOfChannel(Channel channel) {
  switch (channel) {
    case Channel::L: return DevicePosition::FrontLeft;
    case Channel::R: return DevicePosition::FrontRight;
    case Channel::C: return DevicePosition::FrontCenter;
    case Channel::LFE: return DevicePosition::Lfe;
    case Channel::Lw: return DevicePosition::WideLeft;
    case Channel::Rw: return DevicePosition::WideRight;
    case Channel::Ls: return DevicePosition::SideLeft;
    case Channel::Rs: return DevicePosition::SideRight;
    case Channel::Lrs: return DevicePosition::BackLeft;
    case Channel::Rrs: return DevicePosition::BackRight;
    case Channel::Ltf: return DevicePosition::TopFrontLeft;
    case Channel::Rtf: return DevicePosition::TopFrontRight;
    case Channel::Ltm: return DevicePosition::TopMiddleLeft;
    case Channel::Rtm: return DevicePosition::TopMiddleRight;
    case Channel::Ltr: return DevicePosition::TopBackLeft;
    case Channel::Rtr: return DevicePosition::TopBackRight;
    default: return DevicePosition::None;
  }
}

/// Finds a device channel for every channel of `layout` among the device's positions: each takes the first free
/// channel of its position. @return false if any channel finds none (`route` is then not to be used)
bool routeLayout(const Layout& layout, const DeviceReport& report, int route[kMaxChannels]) {
  const int used = std::min(report.channels, kMaxChannels);
  bool taken[kMaxChannels] = {};
  for (int i = 0; i < kMaxChannels; ++i) route[i] = -1;
  for (int i = 0; i < layout.channels(); ++i) {
    const DevicePosition want = positionOfChannel(layout.speaker(i).channel);
    if (want == DevicePosition::None) return false;
    for (int d = 0; d < used; ++d) {
      if (taken[d] || report.position[d] != want) continue;
      route[i] = d;
      taken[d] = true;
      break;
    }
    if (route[i] < 0) return false;
  }
  return true;
}
}  // namespace

DevicePosition positionForCoreAudioLabel(uint32_t label) {
  switch (label) {
    case 1: return DevicePosition::FrontLeft;           // Left
    case 2: return DevicePosition::FrontRight;          // Right
    case 3: return DevicePosition::FrontCenter;         // Center
    case 4: return DevicePosition::Lfe;                 // LFEScreen
    case 5: return DevicePosition::SideLeft;            // LeftSurround
    case 6: return DevicePosition::SideRight;           // RightSurround
    case 7: return DevicePosition::FrontLeftCenter;     // LeftCenter
    case 8: return DevicePosition::FrontRightCenter;    // RightCenter
    case 9: return DevicePosition::BackCenter;          // CenterSurround
    case 10: return DevicePosition::SideLeft;           // LeftSurroundDirect
    case 11: return DevicePosition::SideRight;          // RightSurroundDirect
    case 12: return DevicePosition::TopCenter;          // TopCenterSurround
    case 13: return DevicePosition::TopFrontLeft;       // VerticalHeightLeft
    case 14: return DevicePosition::TopFrontCenter;     // VerticalHeightCenter
    case 15: return DevicePosition::TopFrontRight;      // VerticalHeightRight
    case 16: return DevicePosition::TopBackLeft;        // TopBackLeft
    case 17: return DevicePosition::TopBackCenter;      // TopBackCenter
    case 18: return DevicePosition::TopBackRight;       // TopBackRight
    case 33: return DevicePosition::BackLeft;           // RearSurroundLeft
    case 34: return DevicePosition::BackRight;          // RearSurroundRight
    case 35: return DevicePosition::WideLeft;           // LeftWide
    case 36: return DevicePosition::WideRight;          // RightWide
    case 49: return DevicePosition::TopMiddleLeft;      // LeftTopMiddle
    case 51: return DevicePosition::TopMiddleRight;     // RightTopMiddle
    case 37: return DevicePosition::Lfe;                // LFE2 (a second bass channel stays silent, see Layout::fromDevice)
    case 42: return DevicePosition::Mono;               // Mono
    case 52: return DevicePosition::TopBackLeft;        // LeftTopRear
    case 53: return DevicePosition::TopBackCenter;      // CenterTopRear
    case 54: return DevicePosition::TopBackRight;       // RightTopRear
    default: break;
  }
  if (label >= 0x10000 && label < 0x20000) return DevicePosition::Aux;  // Discrete_0 and up
  return DevicePosition::None;  // Unused (0), Unknown (0xFFFFFFFF) and the labels that mean no speaker
}

DeviceChannel deviceChannelFor(DevicePosition position) {
  DeviceChannel d;
  switch (position) {
    case DevicePosition::Mono: break;
    case DevicePosition::FrontLeft: d.azimuth = -30; break;
    case DevicePosition::FrontRight: d.azimuth = 30; break;
    case DevicePosition::FrontCenter: break;
    case DevicePosition::Lfe: d.lfe = true; break;
    case DevicePosition::FrontLeftCenter: d.azimuth = -15; break;
    case DevicePosition::FrontRightCenter: d.azimuth = 15; break;
    case DevicePosition::BackLeft: d.azimuth = -135; break;
    case DevicePosition::BackRight: d.azimuth = 135; break;
    case DevicePosition::BackCenter: d.azimuth = 180; break;
    case DevicePosition::SideLeft: d.azimuth = -90; break;
    case DevicePosition::SideRight: d.azimuth = 90; break;
    case DevicePosition::TopCenter: d.elevation = 90; break;
    case DevicePosition::TopFrontLeft: d.azimuth = -45; d.elevation = 45; break;
    case DevicePosition::TopFrontCenter: d.elevation = 45; break;
    case DevicePosition::TopFrontRight: d.azimuth = 45; d.elevation = 45; break;
    case DevicePosition::TopBackLeft: d.azimuth = -135; d.elevation = 45; break;
    case DevicePosition::TopBackCenter: d.azimuth = 180; d.elevation = 45; break;
    case DevicePosition::TopBackRight: d.azimuth = 135; d.elevation = 45; break;
    case DevicePosition::WideLeft: d.azimuth = -60; break;
    case DevicePosition::WideRight: d.azimuth = 60; break;
    case DevicePosition::TopMiddleLeft: d.azimuth = -90; d.elevation = 45; break;
    case DevicePosition::TopMiddleRight: d.azimuth = 90; d.elevation = 45; break;
    case DevicePosition::None:
    case DevicePosition::Aux: d.known = false; break;
  }
  return d;
}

void portsForLayout(const Layout& layout, DevicePort out[kMaxChannels]) {
  int aux = 0;
  for (int i = 0; i < kMaxChannels; ++i) out[i] = DevicePort{};
  for (int i = 0; i < layout.channels() && i < kMaxChannels; ++i) {
    DevicePosition p = DevicePosition::Aux;  // what has no standard position
    switch (layout.speaker(i).channel) {
      case Channel::L: p = DevicePosition::FrontLeft; break;
      case Channel::R: p = DevicePosition::FrontRight; break;
      case Channel::C: p = DevicePosition::FrontCenter; break;
      case Channel::LFE: p = DevicePosition::Lfe; break;
      case Channel::Ls: p = DevicePosition::SideLeft; break;
      case Channel::Rs: p = DevicePosition::SideRight; break;
      case Channel::Lrs: p = DevicePosition::BackLeft; break;
      case Channel::Rrs: p = DevicePosition::BackRight; break;
      case Channel::Ltf: p = DevicePosition::TopFrontLeft; break;
      case Channel::Rtf: p = DevicePosition::TopFrontRight; break;
      case Channel::Ltr: p = DevicePosition::TopBackLeft; break;
      case Channel::Rtr: p = DevicePosition::TopBackRight; break;
      default: break;  // Lw, Rw, Ltm, Rtm and unmapped channels
    }
    out[i].position = p;
    if (p == DevicePosition::Aux) out[i].aux = aux++;
  }
}

bool LayoutChoice::parse(std::string_view text, LayoutChoice& out) {
  if (text == "auto") {
    out = LayoutChoice{};
    return true;
  }
  LayoutId id;
  if (!Layout::parse(text, id)) return false;
  out.automatic = false;
  out.id = id;
  return true;
}

Negotiation negotiate(const LayoutChoice& choice, const DeviceReport& report) {
  Negotiation n;
  const int deviceChannels = std::max(1, report.channels);
  if (!choice.automatic) {
    n.layout = Layout::named(choice.id);
    if (report.hasMap && report.exact) {
      // The device's own map is known: play the layout on the channels of its positions. Where the device has no
      // place for one of its channels, step down to the richest layout it has (7.1.4 on a 5.1 device is 5.1), which
      // is better than letting the library mix it into an assumed map (that plays on channels the device does not
      // use). A plain stereo or mono device is left to the system, whose mix-down (and spatializer) is wanted there.
      LayoutId id = choice.id;
      for (;;) {
        const Layout l = Layout::named(id);
        if (routeLayout(l, report, n.route)) {
          n.layout = l;
          n.deviceChannels = deviceChannels;  // the device stays as it is; the layout's channels are written to its own
          n.routed = true;
          return n;
        }
        LayoutId lower;
        if (deviceChannels <= 2 || !lowerLayout(id, lower)) break;
        id = lower;
      }
    }
    n.deviceChannels = n.layout.channels();
    n.reopen = true;
    // More channels than the device has; or a device whose own map is known and has no speaker for one of the
    // layout's channels (a stereo device asked for 7.1.4), so that the system has to mix it into the others.
    n.downmixed = n.layout.channels() > deviceChannels || (report.hasMap && report.exact);
    return n;
  }
  if (report.spatializes && deviceChannels <= 2) {  // headphones whose system spatializer wants more than a pair
    n.layout = Layout::named(LayoutId::S7_1_4);
    n.deviceChannels = deviceChannels;  // the route stays as it is; the system makes the pair from the layout
    n.downmixed = true;
    return n;
  }
  int used = std::min(deviceChannels, kMaxChannels);
  if (report.hasMap) {
    while (used > 1 && report.position[used - 1] == DevicePosition::None) --used;  // unused channels at the end are no speakers
    if (report.exact) {  // a platform's map whose channels are exactly those of a named layout, in another order or with gaps: that layout, routed
      int active = 0;
      for (int i = 0; i < used; ++i) active += report.position[i] != DevicePosition::None;
      for (int id = kLayoutCount - 1; id >= 0; --id) {
        const Layout named = Layout::named(static_cast<LayoutId>(id));
        if (named.channels() != active || !routeLayout(named, report, n.route)) continue;
        bool inOrder = active == used;
        for (int i = 0; i < named.channels(); ++i) inOrder = inOrder && n.route[i] == i;
        n.layout = named;
        n.deviceChannels = deviceChannels;
        n.routed = !inOrder;  // (in the device's own order it is a plain open, as for a map from the library)
        return n;
      }
    }
    DeviceChannel dc[kMaxChannels];
    for (int i = 0; i < used; ++i) dc[i] = deviceChannelFor(report.position[i]);
    n.layout = Layout::fromDevice(dc, used);
    for (int id = kLayoutCount - 1; id >= 0; --id) {  // the device's own order is a named layout's: use the named one
      if (sameChannels(n.layout, static_cast<LayoutId>(id))) {
        n.layout = Layout::named(static_cast<LayoutId>(id));
        break;
      }
    }
    n.deviceChannels = deviceChannels;  // (more than the layout's, if the device has more than kMaxChannels: the rest stays silent)
    n.reopen = false;
    return n;
  }
  if (deviceChannels == 1) {  // a single speaker
    const DeviceChannel one;
    n.layout = Layout::fromDevice(&one, 1);
    n.deviceChannels = 1;
    return n;
  }
  n.layout = Layout::named(richestFor(deviceChannels));
  n.deviceChannels = n.layout.channels();
  n.reopen = true;  // without a map the device's order is not known: it is told the count and the map
  return n;
}

void routeChannels(const float* in, int layoutChannels, float* out, int deviceChannels, int frames, const int route[kMaxChannels]) {
  for (int f = 0; f < frames; ++f) {
    for (int c = 0; c < deviceChannels; ++c) out[c] = 0.0f;
    for (int c = 0; c < layoutChannels; ++c) out[route[c]] = in[c];
    in += layoutChannels;
    out += deviceChannels;
  }
}

void padChannels(const float* in, int layoutChannels, float* out, int deviceChannels, int frames) {
  for (int f = 0; f < frames; ++f) {
    for (int c = 0; c < layoutChannels; ++c) out[c] = in[c];
    for (int c = layoutChannels; c < deviceChannels; ++c) out[c] = 0.0f;
    in += layoutChannels;
    out += deviceChannels;
  }
}

bool lowerLayout(LayoutId from, LayoutId& to) {
  switch (from) {
    case LayoutId::S9_1_6:
    case LayoutId::S9_1_4: to = LayoutId::S7_1_4; return true;
    case LayoutId::S7_1_4:
    case LayoutId::S7_1_2: to = LayoutId::S7_1; return true;
    case LayoutId::S5_1_4:
    case LayoutId::S5_1_2: to = LayoutId::S5_1; return true;
    case LayoutId::S7_1: to = LayoutId::S5_1; return true;
    case LayoutId::S5_1: to = LayoutId::Stereo; return true;
    case LayoutId::Stereo: return false;
  }
  return false;
}

}  // namespace dio
