/// @file core/negotiate.h
/// @brief Choosing the layout to render to: what the user asked for, what the device reports, and how to step down.
///
/// Pure logic with no device in it, so that it can be tested; the backends (src/audio) turn their own
/// channel positions into DevicePosition and open the device as the result says.

#pragma once
#include <cstdint>
#include <string_view>

#include "core/layout.h"

namespace dio {

/// Where a backend says one of its device channels is, in the vocabulary all the backends share (these are
/// the positions miniaudio names; the Apple backend maps its channel-layout labels onto the same ones).
enum class DevicePosition : uint8_t {
  None,             ///< unknown or unused: the channel stays silent
  Mono,             ///< the one channel of a mono device
  FrontLeft,        ///< -30 degrees
  FrontRight,       ///< +30 degrees
  FrontCenter,      ///< straight ahead
  Lfe,              ///< the low-frequency channel
  FrontLeftCenter,  ///< between front left and centre, -15 degrees
  FrontRightCenter, ///< between front right and centre, +15 degrees
  BackLeft,         ///< -135 degrees
  BackRight,        ///< +135 degrees
  BackCenter,       ///< straight behind
  SideLeft,         ///< -90 degrees
  SideRight,        ///< +90 degrees
  TopCenter,        ///< straight up
  TopFrontLeft,     ///< -45 degrees, 45 degrees up
  TopFrontCenter,   ///< straight ahead, 45 degrees up
  TopFrontRight,    ///< +45 degrees, 45 degrees up
  TopBackLeft,      ///< -135 degrees, 45 degrees up
  TopBackCenter,    ///< straight behind, 45 degrees up
  TopBackRight,     ///< +135 degrees, 45 degrees up
  WideLeft,         ///< -60 degrees (no backend library names it; only a platform's own channel layout does)
  WideRight,        ///< +60 degrees (as WideLeft)
  TopMiddleLeft,    ///< -90 degrees, 45 degrees up (as WideLeft)
  TopMiddleRight,   ///< +90 degrees, 45 degrees up (as WideLeft)
  Aux               ///< an auxiliary channel, whose speaker only the device knows (the position is not known to us)
};

/// What a device channel is, as far as the synth can tell (Aux and None stay silent).
DeviceChannel deviceChannelFor(DevicePosition position);

/// The position of a channel that Apple's CoreAudio labels with `label` (an `AudioChannelLabel`; the numbers are
/// part of the CoreAudio ABI, so this needs no Apple header and can be tested everywhere). The side surround
/// channels are the sides (labels 5, 6, 10 and 11) and the rear surround channels the rears (33 and 34), as in the
/// 7.1 and 7.1.4 layouts; the wide (35, 36) and top-middle (49, 51) channels have their own positions; the discrete
/// channels are Aux; unknown, unused and unlisted labels are None.
DevicePosition positionForCoreAudioLabel(uint32_t label);

/// The device channel a canonical channel is written to when the app opens the device with its own channel map.
struct DevicePort {
  DevicePosition position = DevicePosition::None;  ///< the position to ask for
  int aux = 0;                                     ///< for Aux: which auxiliary channel (0 is the first)
};

/// The ports of a layout's channels, in the layout's order, for opening a device with the audio library's own channel
/// map. The channels the library has no names for (wide and top-middle pairs, and anything unmapped) take the
/// auxiliary channels in order.
void portsForLayout(const Layout& layout, DevicePort out[kMaxChannels]);

/// What the user asked for: the richest layout the device reports, or one named layout.
struct LayoutChoice {
  bool automatic = true;             ///< true: render to what the device reports
  LayoutId id = LayoutId::Stereo;    ///< the named layout, if not automatic

  /// Parses "auto" or a layout name such as "5.1" or "7.1.4". @return false for anything else (`out` is left alone)
  static bool parse(std::string_view text, LayoutChoice& out);
  /// "auto" or the layout's name.
  const char* name() const { return automatic ? "auto" : Layout::nameOf(id); }
};

/// What a device reports about its output.
struct DeviceReport {
  int channels = 2;                                 ///< how many channels it has
  bool hasMap = false;                              ///< the positions of the first kMaxChannels channels are known
  /// The positions are the ones the platform itself gives the device's channels (CoreAudio's channel layout of the
  /// device), not a default assumption of the audio library. Only then may a named layout be played by routing its
  /// channels to the device's channels of the same position, without the library remapping them (which loses the
  /// positions that its own, assumed map lacks).
  bool exact = false;
  DevicePosition position[kMaxChannels] = {};       ///< where each of them is
  /// The route turns a multichannel signal into a spatial rendering itself (AirPods and other spatial-capable
  /// headphones, which are two channels to the application). Automatic then renders the richest layout
  /// the spatializer is known to take (7.1.4) instead of a stereo pair, and the system makes the rest.
  bool spatializes = false;
};

/// How to render: the layout, and how the device must be opened.
struct Negotiation {
  Layout layout;               ///< what the synth renders
  int deviceChannels = 2;      ///< how many channels the device is opened with
  bool reopen = false;         ///< the device must be (re)opened with `layout`'s own channel map (else the open that made the report is right)
  bool downmixed = false;      ///< the layout has more channels than the device, or channels it has no speaker for: the sound server, driver or audio library mixes it down
  /// Each channel of `layout` has a device channel of its position, `route[i]`, and the device is not opened again
  /// with the layout's map: a backend that places channels by position itself (the Apple backend, which hands the
  /// system the layout as tagged PCM) uses the layout alone; `route` is the placement it makes. Only on a device
  /// whose positions are `exact` and that has a channel for every channel of the layout (see negotiate()).
  bool routed = false;
  int route[kMaxChannels] = {};  ///< for routed: the device channel of each layout channel
};

/// Chooses the layout. Automatic: 7.1.4 on a route of one or two channels that `spatializes`; else the device's own map if it has one, without the unused channels at its end (a
/// named layout if the map is exactly that layout's channels in its order, or, for an `exact` map, in any order, in
/// which case the layout is `routed`), else the richest of 9.1.6, 7.1.4, 7.1, 5.1 and 2.0 that fits the
/// count; a single channel is a single speaker. A named choice is rendered as asked, even above what the
/// device has (`downmixed`); on a device with an `exact` map it is routed to the device's channels of the same
/// positions (`routed`) instead of reopening the device with the layout's map, and if the device has no place for
/// one of its channels the richest layout below it that the device has is routed instead (`layout` then names
/// that one: 7.1.4 asked on a 5.1 device gives 5.1), except on a device of one or two channels, which is opened
/// with the layout's map as for any device without an exact map and left to the system to mix down.
Negotiation negotiate(const LayoutChoice& choice, const DeviceReport& report);

/// Copies `frames` interleaved frames of `layoutChannels` channels into frames of `deviceChannels` channels
/// (at least as many): the extra channels of a device with more speakers than the synth has are silent.
void padChannels(const float* in, int layoutChannels, float* out, int deviceChannels, int frames);

/// The next layout to try when `from` does not open: 9.1.6 and 9.1.4 to 7.1.4, 7.1.4 and 7.1.2 to 7.1,
/// 5.1.4 and 5.1.2 to 5.1, 7.1 to 5.1, 5.1 to 2.0.
/// @return false for 2.0, which has nowhere lower to go (`to` is left alone)
bool lowerLayout(LayoutId from, LayoutId& to);

}  // namespace dio
