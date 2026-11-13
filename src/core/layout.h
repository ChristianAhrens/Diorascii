/// @file core/layout.h
/// @brief Speaker layouts: which channels an output has and where they stand around the listener.

#pragma once
#include <cstdint>
#include <string_view>

namespace dio {

/// The most channels any layout has (9.1.6). Per-channel arrays in the synth and the spatializer
/// are this long.
inline constexpr int kMaxChannels = 16;

/// The canonical channels, in canonical order. A layout lists its channels in this order,
/// independent of any backend; backends map between this order and the device's own.
enum class Channel : uint8_t {
  L,    ///< front left, -30 degrees
  R,    ///< front right, +30 degrees
  C,    ///< front centre
  LFE,  ///< low-frequency effects; carries bass only, has no direction
  Lw,   ///< wide left, -60 degrees
  Rw,   ///< wide right, +60 degrees
  Ls,   ///< side left, -90 degrees (-110 in the 5.1 family)
  Rs,   ///< side right, +90 degrees (+110 in the 5.1 family)
  Lrs,  ///< rear left, -135 degrees
  Rrs,  ///< rear right, +135 degrees
  Ltf,  ///< top front left, -45 degrees, 45 degrees up
  Rtf,  ///< top front right, +45 degrees, 45 degrees up
  Ltm,  ///< top middle left, -90 degrees, 45 degrees up
  Rtm,  ///< top middle right, +90 degrees, 45 degrees up
  Ltr,  ///< top rear left, -135 degrees, 45 degrees up
  Rtr,  ///< top rear right, +135 degrees, 45 degrees up
  None  ///< a device channel that cannot be mapped; it stays silent
};

/// The named layouts, from stereo up to 9.1.6.
enum class LayoutId : uint8_t { Stereo, S5_1, S7_1, S5_1_2, S5_1_4, S7_1_2, S7_1_4, S9_1_4, S9_1_6 };
/// Number of LayoutId values.
inline constexpr int kLayoutCount = 9;

/// One output channel: which canonical channel it is and where its speaker stands.
struct Speaker {
  Channel channel = Channel::None;  ///< the canonical identity (None: silent)
  float azimuth = 0;                ///< degrees from straight ahead, positive to the right
  float elevation = 0;              ///< degrees above the horizon
  float weight = 1;                 ///< front-bias weight of the panning law (1 = neutral)

  /// True for the LFE channel.
  bool isLfe() const { return channel == Channel::LFE; }
  /// True for a channel the panner may use: neither LFE nor unmapped.
  bool isDirectional() const { return channel != Channel::LFE && channel != Channel::None; }
};

/// What a device reports about one of its output channels (filled in by a backend).
struct DeviceChannel {
  float azimuth = 0;      ///< degrees from straight ahead, positive to the right
  float elevation = 0;    ///< degrees above the horizon
  bool lfe = false;       ///< the channel is a low-frequency channel
  bool known = true;      ///< false if the backend has no position for it (it stays silent)
};

/// An ordered set of output channels and their positions. Small and copyable.
class Layout {
 public:
  /// A stereo layout.
  Layout();
  /// One of the named layouts.
  static Layout named(LayoutId id);
  /// Parses "2.0", "5.1", "7.1", "5.1.2", "5.1.4", "7.1.2", "7.1.4", "9.1.4" or "9.1.6".
  /// @return false (and leaves `id` alone) for anything else
  static bool parse(std::string_view text, LayoutId& id);
  /// The name a named layout is known by, for example "7.1.4".
  static const char* nameOf(LayoutId id);
  /// Builds a layout from what a device reports. Each channel keeps the position given and is
  /// identified with the nearest canonical channel; LFE channels stay LFE, channels without a
  /// known position are silent. Only the first kMaxChannels entries are used; the backend writes
  /// silence to the rest.
  static Layout fromDevice(const DeviceChannel* channels, int count);

  /// Number of channels the layout writes per frame.
  int channels() const { return count_; }
  /// The speaker of output channel `i` (0 up to channels() - 1).
  const Speaker& speaker(int i) const { return speakers_[i]; }
  /// Index of the LFE channel, or -1 if there is none.
  int lfeIndex() const { return lfe_; }
  /// True if any speaker stands above the horizon.
  bool hasHeight() const;
  /// True for a plain left/right pair on the horizon (a left speaker at a negative azimuth and a
  /// right one at a positive one), which uses the stereo panning law.
  bool isStereo() const;
  /// The layout's name: for example "7.1.4", or "custom" if it came from a device.
  const char* name() const { return name_; }

 private:
  /// Tag for the private constructor that leaves the layout empty (used by the builders).
  struct Empty {};
  /// An empty layout; the builders fill it in.
  explicit Layout(Empty) {}

  Speaker speakers_[kMaxChannels];  ///< the channels, in canonical order for a named layout
  int count_ = 0;                   ///< number of channels used
  int lfe_ = -1;                    ///< index of the LFE channel, -1 if none
  const char* name_ = "custom";     ///< name for display
};

/// The canonical position of a channel (the 5.1 family moves the sides to +-110 itself).
/// LFE and None have no position and give 0, 0.
void canonicalPosition(Channel channel, float& azimuth, float& elevation);
/// The canonical channel (never LFE or None) whose position is nearest to the given direction,
/// by angle on the sphere.
Channel nearestChannel(float azimuth, float elevation);

}  // namespace dio
