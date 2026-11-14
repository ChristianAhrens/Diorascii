/// @file audio/audio.cpp
/// @brief Playback device wrapper around miniaudio.

#include "audio/audio.h"

#include "core/seqlock.h"
#ifdef __APPLE__
#include "audio/coreaudio_layout.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstring>

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio.h"

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dio {

/// Frames the synth renders at a time when the device has more channels than the synth can write.
constexpr ma_uint32 kScratchFrames = 1024;

struct AudioOutput::Impl {
  ma_device device;
  bool open = false;
  Ambience ambience{48000};
  /// Parameters, written by the main loop and read by the audio thread, as one consistent copy.
  Seqlock<AmbientParams> shared;
  /// The parameters the audio thread uses: the last copy it read (kept if a read does not succeed).
  AmbientParams current;
  /// One-off sounds from the main loop to the audio thread.
  EventRing<SoundEvent, 64> events;
  LayoutChoice choice;                  ///< what the device was last started with
  Layout layout;                        ///< what the synth renders to
  int deviceChannels = 2;               ///< channels the device buffer has
  bool downmixed = false;               ///< the layout has more channels than the device
  bool routed = false;                  ///< the layout's channels go to the device channels in `route` (see Negotiation::routed)
  int route[kMaxChannels] = {};         ///< for routed: the device channel of each layout channel
  std::atomic<bool> routeChanged{false};  ///< the device said its route changed (set on its thread)
  /// Where the synth renders when the device has more channels than the layout (more than kMaxChannels).
  float scratch[kScratchFrames * kMaxChannels];
};

namespace {
/// The position miniaudio names, in the vocabulary of the negotiation.
DevicePosition positionOf(ma_channel c) {
  switch (c) {
    case MA_CHANNEL_MONO: return DevicePosition::Mono;
    case MA_CHANNEL_FRONT_LEFT: return DevicePosition::FrontLeft;
    case MA_CHANNEL_FRONT_RIGHT: return DevicePosition::FrontRight;
    case MA_CHANNEL_FRONT_CENTER: return DevicePosition::FrontCenter;
    case MA_CHANNEL_LFE: return DevicePosition::Lfe;
    case MA_CHANNEL_BACK_LEFT: return DevicePosition::BackLeft;
    case MA_CHANNEL_BACK_RIGHT: return DevicePosition::BackRight;
    case MA_CHANNEL_FRONT_LEFT_CENTER: return DevicePosition::FrontLeftCenter;
    case MA_CHANNEL_FRONT_RIGHT_CENTER: return DevicePosition::FrontRightCenter;
    case MA_CHANNEL_BACK_CENTER: return DevicePosition::BackCenter;
    case MA_CHANNEL_SIDE_LEFT: return DevicePosition::SideLeft;
    case MA_CHANNEL_SIDE_RIGHT: return DevicePosition::SideRight;
    case MA_CHANNEL_TOP_CENTER: return DevicePosition::TopCenter;
    case MA_CHANNEL_TOP_FRONT_LEFT: return DevicePosition::TopFrontLeft;
    case MA_CHANNEL_TOP_FRONT_CENTER: return DevicePosition::TopFrontCenter;
    case MA_CHANNEL_TOP_FRONT_RIGHT: return DevicePosition::TopFrontRight;
    case MA_CHANNEL_TOP_BACK_LEFT: return DevicePosition::TopBackLeft;
    case MA_CHANNEL_TOP_BACK_CENTER: return DevicePosition::TopBackCenter;
    case MA_CHANNEL_TOP_BACK_RIGHT: return DevicePosition::TopBackRight;
    default: return c >= MA_CHANNEL_AUX_0 ? DevicePosition::Aux : DevicePosition::None;
  }
}

/// The miniaudio channel a port of the negotiation is.
ma_channel channelOf(const DevicePort& port) {
  switch (port.position) {
    case DevicePosition::Mono: return MA_CHANNEL_MONO;
    case DevicePosition::FrontLeft: return MA_CHANNEL_FRONT_LEFT;
    case DevicePosition::FrontRight: return MA_CHANNEL_FRONT_RIGHT;
    case DevicePosition::FrontCenter: return MA_CHANNEL_FRONT_CENTER;
    case DevicePosition::Lfe: return MA_CHANNEL_LFE;
    case DevicePosition::FrontLeftCenter: return MA_CHANNEL_FRONT_LEFT_CENTER;
    case DevicePosition::FrontRightCenter: return MA_CHANNEL_FRONT_RIGHT_CENTER;
    case DevicePosition::BackLeft: return MA_CHANNEL_BACK_LEFT;
    case DevicePosition::BackRight: return MA_CHANNEL_BACK_RIGHT;
    case DevicePosition::BackCenter: return MA_CHANNEL_BACK_CENTER;
    case DevicePosition::SideLeft: return MA_CHANNEL_SIDE_LEFT;
    case DevicePosition::SideRight: return MA_CHANNEL_SIDE_RIGHT;
    case DevicePosition::TopCenter: return MA_CHANNEL_TOP_CENTER;
    case DevicePosition::TopFrontLeft: return MA_CHANNEL_TOP_FRONT_LEFT;
    case DevicePosition::TopFrontCenter: return MA_CHANNEL_TOP_FRONT_CENTER;
    case DevicePosition::TopFrontRight: return MA_CHANNEL_TOP_FRONT_RIGHT;
    case DevicePosition::TopBackLeft: return MA_CHANNEL_TOP_BACK_LEFT;
    case DevicePosition::TopBackCenter: return MA_CHANNEL_TOP_BACK_CENTER;
    case DevicePosition::TopBackRight: return MA_CHANNEL_TOP_BACK_RIGHT;
    case DevicePosition::Aux: return static_cast<ma_channel>(MA_CHANNEL_AUX_0 + port.aux);
    case DevicePosition::WideLeft:  // (miniaudio has no names for these: portsForLayout never asks for them)
    case DevicePosition::WideRight:
    case DevicePosition::TopMiddleLeft:
    case DevicePosition::TopMiddleRight:
    case DevicePosition::None: break;
  }
  return MA_CHANNEL_NONE;
}

void onAudio(ma_device* device, void* output, const void*, ma_uint32 frames) {
  auto* impl = static_cast<AudioOutput::Impl*>(device->pUserData);
  impl->shared.load(impl->current);
  SoundEvent e;
  while (impl->events.pop(e)) impl->ambience.event(e);
  const int layoutChannels = impl->layout.channels();
  auto* out = static_cast<float*>(output);
  if (!impl->routed && impl->deviceChannels == layoutChannels) {
    impl->ambience.render(impl->current, out, static_cast<int>(frames));
    return;
  }
  // The device has more channels than the synth can write, or the layout's channels go to the device channels of
  // their positions: the other channels stay silent.
  while (frames > 0) {
    const int n = static_cast<int>(std::min<ma_uint32>(frames, kScratchFrames));
    impl->ambience.render(impl->current, impl->scratch, n);
    if (impl->routed) routeChannels(impl->scratch, layoutChannels, out, impl->deviceChannels, n, impl->route);
    else padChannels(impl->scratch, layoutChannels, out, impl->deviceChannels, n);
    out += static_cast<size_t>(n) * static_cast<size_t>(impl->deviceChannels);
    frames -= static_cast<ma_uint32>(n);
  }
}

void onNotification(const ma_device_notification* notification) {
  auto* impl = static_cast<AudioOutput::Impl*>(notification->pDevice->pUserData);
  if (notification->type == ma_device_notification_type_rerouted) impl->routeChanged = true;  // (the main loop reopens the device: not from here)
}

/// Opens the device with `channels` channels (0: whatever the device has) and, if `ports` is given,
/// that channel map. Leaves it open on success.
bool openDevice(AudioOutput::Impl& impl, int channels, const DevicePort* ports) {
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = static_cast<ma_uint32>(channels);
  cfg.sampleRate = 48000;
  cfg.dataCallback = onAudio;
  cfg.notificationCallback = onNotification;
  cfg.pUserData = &impl;
  ma_channel map[MA_MAX_CHANNELS] = {};
  if (ports) {
    for (int i = 0; i < channels; ++i) map[i] = channelOf(ports[i]);
    cfg.playback.pChannelMap = map;
  }
  return ma_device_init(nullptr, &cfg, &impl.device) == MA_SUCCESS;
}

/// What the open device reports about itself.
DeviceReport reportOf(const AudioOutput::Impl& impl) {
  DeviceReport r;
  r.channels = static_cast<int>(impl.device.playback.channels);
  r.hasMap = true;
  for (int i = 0; i < r.channels && i < kMaxChannels; ++i) r.position[i] = positionOf(impl.device.playback.channelMap[i]);
#ifdef __APPLE__
  // miniaudio's map on macOS is an assumption (7.1, then auxiliary channels); the device's own layout is the truth.
  readCoreAudioOutputLayout(r);
#endif
  return r;
}
}  // namespace

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}
AudioOutput::~AudioOutput() { stop(); }

bool AudioOutput::start(const LayoutChoice& choice) {
  if (impl_->open) return true;
  Impl& impl = *impl_;
  impl.choice = choice;
  impl.routeChanged = false;

  // Sound libraries such as ALSA print their complaints to stderr, which would scribble over
  // the scene. Silence stderr while the device opens.
#ifndef _WIN32
  const int savedErr = dup(STDERR_FILENO);
  const int devNull = open("/dev/null", O_WRONLY);
  if (devNull >= 0) dup2(devNull, STDERR_FILENO);
#endif
  // First open with what the device has (0 channels): that is what it reports. Never infer the device
  // from a successful open with a count: a sound server accepts any count and remaps.
  bool ok = openDevice(impl, 0, nullptr);
  if (ok) {
    Negotiation n = negotiate(choice, reportOf(impl));
    if (n.reopen) {  // a layout was asked for: open with its own channel map (the device always reports a map, so automatic never gets here)
      const DeviceReport native = reportOf(impl);
      ma_device_uninit(&impl.device);
      LayoutChoice step = choice;
      for (;;) {
        n = negotiate(step, native);
        if (n.routed) {  // the device's own channels carry the layout: it stays as it opens by itself
          ok = openDevice(impl, 0, nullptr);
          break;
        }
        DevicePort ports[kMaxChannels];
        portsForLayout(n.layout, ports);
        ok = openDevice(impl, n.layout.channels(), ports);
        if (ok) break;
        LayoutId lower;
        if (!lowerLayout(step.id, lower)) break;  // stereo did not open: nothing is left to try
        step.id = lower;
      }
      if (!ok) ok = openDevice(impl, 0, nullptr);  // last resort: whatever the device has
      if (ok && !n.routed && impl.device.playback.channels != static_cast<ma_uint32>(n.layout.channels())) {
        n = negotiate(LayoutChoice{}, reportOf(impl));  // it did not take the map: render to what it says it has
      }
    }
    if (ok) {
      impl.layout = n.layout;
      impl.deviceChannels = static_cast<int>(impl.device.playback.channels);
      impl.downmixed = n.downmixed;
      impl.routed = n.routed;
      std::copy(std::begin(n.route), std::end(n.route), std::begin(impl.route));
      impl.ambience.setLayout(impl.layout);  // (the device is not started yet: nothing renders)
      ok = ma_device_start(&impl.device) == MA_SUCCESS;
      if (!ok) ma_device_uninit(&impl.device);
    }
  }
#ifndef _WIN32
  if (savedErr >= 0) { dup2(savedErr, STDERR_FILENO); close(savedErr); }
  if (devNull >= 0) close(devNull);
#endif
  impl.open = ok;
  return ok;
}

void AudioOutput::stop() {
  if (!impl_->open) return;
  ma_device_uninit(&impl_->device);
  impl_->open = false;
}

bool AudioOutput::running() const { return impl_->open; }

bool AudioOutput::update() {
  if (!impl_->open || !impl_->routeChanged.exchange(false)) return false;
  const LayoutChoice choice = impl_->choice;
  stop();
  return start(choice);
}

const Layout& AudioOutput::layout() const { return impl_->layout; }

int AudioOutput::deviceChannels() const { return impl_->deviceChannels; }

bool AudioOutput::downmixed() const { return impl_->downmixed; }

void AudioOutput::setParams(const AmbientParams& p) { impl_->shared.store(p); }

void AudioOutput::pushEvent(const SoundEvent& e) { impl_->events.push(e); }

}  // namespace dio
