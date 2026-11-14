/// @file audio/audio_apple.mm
/// @brief Playback device wrapper for macOS, tvOS and iOS, built on AVSampleBufferAudioRenderer.
///
/// The synth's layout is handed to the system as channel-layout-tagged PCM, the way Apple's own players
/// hand it a movie's sound, and the system does the rest: it places each channel on the device channel with
/// the same label, mixes a layout that is richer than the device down, and spatializes it on AirPods and other
/// spatial-capable headphones (the system's "3D audio", with its Control Center switch and head tracking).
/// An AVAudioEngine output does none of the last: the system offers no spatial audio for it (checked with
/// AirPods Pro on macOS), which is why this is not built on the engine. The positions of the device's channels
/// come from the labels of its channel layout (macOS; the engine is only asked what the output looks like),
/// the audio session knows a count and whether the route spatializes (tvOS, iOS). The tags, their channel
/// order and the placement on speaker devices were measured on macOS (docs/ai/findings.md F-26); the session
/// calls on tvOS and iOS are written from the documentation and must be checked on a device (the simulator's
/// route is stereo); see docs/ai/spatial-audio-plan.md, section 5.3.
///
#include "audio/audio.h"

#include "core/seqlock.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreMedia/CoreMedia.h>
#if TARGET_OS_OSX
#import <CoreAudio/CoreAudio.h>
#endif
#include <TargetConditionals.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <vector>

namespace dio {

namespace {
/// The synth's sample rate.
constexpr int kRate = 48000;
/// Frames the synth renders into one sample buffer (100 ms).
constexpr int kChunk = 4800;
/// How far ahead of the playback position sound is kept queued, in seconds: the delay of a sound that
/// an action starts, against the room for the thread that feeds the renderer to be late.
constexpr double kLead = 0.2;

/// How Core Audio names a named layout, and what its buffers carry, in order.
struct AppleLayout {
  AudioChannelLayoutTag tag = 0;      ///< the layout tag of the format's channel layout
  Channel order[kMaxChannels] = {};   ///< which canonical channel each buffer of that layout carries
  int channels = 0;                   ///< how many (0: Core Audio has no tag for the layout)
};

/// The Core Audio layout of a named layout: the MPEG tags for stereo, 5.1 and 7.1, and the Atmos
/// speaker layouts for the ones with heights. The channel orders are those the documentation gives for the
/// tags and are to be verified on a device; 9.1.4 has no tag (it steps down to 7.1.4).
AppleLayout appleLayoutOf(LayoutId id) {
  AppleLayout a;
  const Channel L = Channel::L, R = Channel::R, C = Channel::C, F = Channel::LFE, Ls = Channel::Ls, Rs = Channel::Rs,
                Lrs = Channel::Lrs, Rrs = Channel::Rrs, Ltf = Channel::Ltf, Rtf = Channel::Rtf, Ltm = Channel::Ltm,
                Rtm = Channel::Rtm, Ltr = Channel::Ltr, Rtr = Channel::Rtr, Lw = Channel::Lw, Rw = Channel::Rw;
  auto set = [&](AudioChannelLayoutTag tag, std::initializer_list<Channel> order) {
    a.tag = tag;
    a.channels = static_cast<int>(order.size());
    std::copy(order.begin(), order.end(), a.order);
  };
  switch (id) {
    case LayoutId::Stereo: set(kAudioChannelLayoutTag_Stereo, {L, R}); break;
    case LayoutId::S5_1: set(kAudioChannelLayoutTag_MPEG_5_1_A, {L, R, C, F, Ls, Rs}); break;
    case LayoutId::S7_1: set(kAudioChannelLayoutTag_MPEG_7_1_C, {L, R, C, F, Ls, Rs, Lrs, Rrs}); break;
    case LayoutId::S5_1_2: set(kAudioChannelLayoutTag_Atmos_5_1_2, {L, R, C, F, Ls, Rs, Ltm, Rtm}); break;
    case LayoutId::S5_1_4: set(kAudioChannelLayoutTag_Atmos_5_1_4, {L, R, C, F, Ls, Rs, Ltf, Rtf, Ltr, Rtr}); break;
    case LayoutId::S7_1_2: set(kAudioChannelLayoutTag_Atmos_7_1_2, {L, R, C, F, Ls, Rs, Lrs, Rrs, Ltm, Rtm}); break;
    case LayoutId::S7_1_4: set(kAudioChannelLayoutTag_Atmos_7_1_4, {L, R, C, F, Ls, Rs, Lrs, Rrs, Ltf, Rtf, Ltr, Rtr}); break;
    case LayoutId::S9_1_6:
      set(kAudioChannelLayoutTag_Atmos_9_1_6, {L, R, C, F, Ls, Rs, Lrs, Rrs, Lw, Rw, Ltf, Rtf, Ltm, Rtm, Ltr, Rtr});
      break;
    case LayoutId::S9_1_4: break;
  }
  return a;
}

#if TARGET_OS_OSX
/// The channel labels of `layout`, in channel order: its own descriptions, or those of its tag. Empty if it
/// is given by a bitmap or the tag is unknown.
std::vector<uint32_t> labelsOf(const AudioChannelLayout& layout) {
  std::vector<uint32_t> labels;
  if (layout.mChannelLayoutTag == kAudioChannelLayoutTag_UseChannelDescriptions) {
    for (UInt32 i = 0; i < layout.mNumberChannelDescriptions; ++i) labels.push_back(layout.mChannelDescriptions[i].mChannelLabel);
    return labels;
  }
  if (layout.mChannelLayoutTag == kAudioChannelLayoutTag_UseChannelBitmap) return labels;
  AudioChannelLayoutTag tag = layout.mChannelLayoutTag;
  UInt32 size = 0;
  if (AudioFormatGetPropertyInfo(kAudioFormatProperty_ChannelLayoutForTag, sizeof tag, &tag, &size) != noErr || size == 0) return labels;
  std::unique_ptr<void, decltype(&std::free)> memory(std::malloc(size), &std::free);
  auto* resolved = static_cast<AudioChannelLayout*>(memory.get());
  if (!resolved || AudioFormatGetProperty(kAudioFormatProperty_ChannelLayoutForTag, sizeof tag, &tag, &size, resolved) != noErr) return labels;
  for (UInt32 i = 0; i < resolved->mNumberChannelDescriptions; ++i) labels.push_back(resolved->mChannelDescriptions[i].mChannelLabel);
  return labels;
}

/// What the default output device looks like to the engine: its channel count and, if its channel layout
/// carries labels, the position of each channel (the system's own account, set in Audio MIDI Setup).
DeviceReport reportOfOutput() {
  DeviceReport report;
  AVAudioEngine* probe = [[AVAudioEngine alloc] init];
  AVAudioFormat* format = [probe.outputNode outputFormatForBus:0];
  report.channels = std::max<int>(1, static_cast<int>(format.channelCount));
  if (format.channelLayout == nil) return report;
  const std::vector<uint32_t> labels = labelsOf(*format.channelLayout.layout);
  if (labels.empty()) return report;
  report.hasMap = true;
  report.exact = true;
  const int used = std::min({static_cast<int>(labels.size()), report.channels, kMaxChannels});
  for (int i = 0; i < kMaxChannels; ++i) report.position[i] = i < used ? positionForCoreAudioLabel(labels[i]) : DevicePosition::None;
  return report;
}

/// True if the default output device is Bluetooth (AirPods and other headphones, whose two channels the system
/// can spatialize). The system offers no query for "this route spatializes" on macOS, so the transport is what
/// is asked; a Bluetooth speaker counts as well, which does no harm (the system then mixes the layout down).
bool defaultOutputIsBluetooth() {
  AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                        kAudioObjectPropertyElementMain};
  AudioObjectID device = kAudioObjectUnknown;
  UInt32 size = sizeof device;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device) != noErr || device == kAudioObjectUnknown) return false;
  address = {kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  UInt32 transport = 0;
  size = sizeof transport;
  if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &transport) != noErr) return false;
  return transport == kAudioDeviceTransportTypeBluetooth || transport == kAudioDeviceTransportTypeBluetoothLE;
}
#endif
}  // namespace

/// Device state: the renderer, the thread that feeds it, and the parameters shared with that thread.
struct AudioOutput::Impl {
  AVSampleBufferAudioRenderer* renderer = nil;              ///< plays the queued sample buffers
  AVSampleBufferRenderSynchronizer* synchronizer = nil;     ///< its clock (rate 1 while playing)
  dispatch_queue_t queue = nil;                             ///< the serial queue the synth renders on
  dispatch_source_t timer = nil;                            ///< wakes the queue to top up the renderer
  CMAudioFormatDescriptionRef format = nullptr;             ///< interleaved float PCM with the layout's tag
  long long position = 0;                                   ///< frames queued so far (the next buffer's presentation time)
  int channels = 0;                                         ///< channels of the format (those of the tag)
  bool open = false;
  Ambience ambience{kRate};
  /// Interleaved samples from the synth, in the synth's channel order.
  float scratch[kChunk * kMaxChannels];
  /// For each channel of the format's tag, the channel of the synth's layout it carries (-1: silence).
  int perm[kMaxChannels];
  /// Parameters, written by the main thread and read by the synth's thread, as one consistent copy.
  Seqlock<AmbientParams> shared;
  /// The parameters the synth uses: the last copy it read (kept if a read does not succeed).
  AmbientParams current;
  /// One-off sounds from the main loop to the synth's thread.
  EventRing<SoundEvent, 64> events;
  LayoutChoice choice;                    ///< what the output was last started with
  Layout layout;                          ///< what the synth renders to
  int deviceChannels = 2;                 ///< channels of the route
  bool downmixed = false;                 ///< the layout has more channels than the route
  std::atomic<bool> routeChanged{false};  ///< the route or the renderer changed (set from a notification)
  id<NSObject> routeObserver = nil;       ///< observer of the audio session's route changes (tvOS, iOS)
  id<NSObject> spatialObserver = nil;     ///< observer of the user switching spatial audio on or off for the route (tvOS, iOS)
  id<NSObject> flushObserver = nil;       ///< observer of the renderer flushing itself (the system does that on a route change)
#if TARGET_OS_OSX
  AudioObjectPropertyListenerBlock deviceListener = nil;  ///< called when the default output device changes
#endif
};

namespace {
/// Renders one chunk and queues it. Called on the impl's queue only.
void enqueueChunk(AudioOutput::Impl& impl) {
  impl.shared.load(impl.current);
  SoundEvent e;
  while (impl.events.pop(e)) impl.ambience.event(e);
  impl.ambience.render(impl.current, impl.scratch, kChunk);
  const int lc = impl.layout.channels();
  const size_t bytes = static_cast<size_t>(kChunk) * static_cast<size_t>(impl.channels) * sizeof(float);
  CMBlockBufferRef block = nullptr;
  if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, bytes, kCFAllocatorDefault, nullptr, 0, bytes,
                                         kCMBlockBufferAssureMemoryNowFlag, &block) != noErr) return;
  char* data = nullptr;
  if (CMBlockBufferGetDataPointer(block, 0, nullptr, nullptr, &data) != noErr || data == nullptr) { CFRelease(block); return; }
  float* dst = reinterpret_cast<float*>(data);
  for (int i = 0; i < kChunk; ++i)  // the format's channel order: that of the tag
    for (int k = 0; k < impl.channels; ++k) dst[i * impl.channels + k] = impl.perm[k] < 0 ? 0.0f : impl.scratch[i * lc + impl.perm[k]];
  CMSampleBufferRef sample = nullptr;
  if (CMAudioSampleBufferCreateReadyWithPacketDescriptions(kCFAllocatorDefault, block, impl.format, kChunk,
                                                           CMTimeMake(impl.position, kRate), nullptr, &sample) == noErr) {
    [impl.renderer enqueueSampleBuffer:sample];
    impl.position += kChunk;
    CFRelease(sample);
  }
  CFRelease(block);
}

/// Keeps `kLead` seconds queued ahead of the playback position. After a stall (the position is past what was
/// queued) the sound resumes at the position instead of playing catch-up. Called on the impl's queue only.
void topUp(AudioOutput::Impl& impl) {
  const double now = std::max(0.0, CMTimeGetSeconds(impl.synchronizer.currentTime));
  if (static_cast<double>(impl.position) / kRate < now) impl.position = static_cast<long long>(now * kRate);
  while (static_cast<double>(impl.position) / kRate - now < kLead) enqueueChunk(impl);
}

/// Builds the format and the renderer for `layout` (named, or a single speaker) and starts it. Returns false,
/// with nothing left running, if the layout has no Core Audio tag.
bool tryRenderer(AudioOutput::Impl& impl, const Layout& layout, LayoutId id) {
  AudioChannelLayoutTag tag = kAudioChannelLayoutTag_Mono;
  std::fill(impl.perm, impl.perm + kMaxChannels, -1);
  if (layout.channels() == 1) {
    impl.channels = 1;
    impl.perm[0] = 0;
  } else {
    const AppleLayout apple = appleLayoutOf(id);
    if (apple.channels == 0 || apple.channels != layout.channels()) return false;
    tag = apple.tag;
    impl.channels = apple.channels;
    for (int k = 0; k < apple.channels; ++k)  // which of the synth's channels goes to channel k of the tag
      for (int i = 0; i < layout.channels(); ++i)
        if (layout.speaker(i).channel == apple.order[k]) impl.perm[k] = i;
  }
  AudioStreamBasicDescription asbd = {};
  asbd.mSampleRate = kRate;
  asbd.mFormatID = kAudioFormatLinearPCM;
  asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  asbd.mChannelsPerFrame = static_cast<UInt32>(impl.channels);
  asbd.mBitsPerChannel = 32;
  asbd.mBytesPerFrame = 4 * asbd.mChannelsPerFrame;
  asbd.mFramesPerPacket = 1;
  asbd.mBytesPerPacket = asbd.mBytesPerFrame;
  AudioChannelLayout channelLayout = {};
  channelLayout.mChannelLayoutTag = tag;
  CMAudioFormatDescriptionRef format = nullptr;
  if (CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &asbd, sizeof channelLayout, &channelLayout, 0, nullptr, nullptr, &format) != noErr) return false;
  impl.format = format;
  impl.layout = layout;
  impl.ambience.setLayout(layout);  // (nothing renders yet)
  impl.position = 0;
  impl.renderer = [[AVSampleBufferAudioRenderer alloc] init];
  impl.synchronizer = [[AVSampleBufferRenderSynchronizer alloc] init];
  [impl.synchronizer addRenderer:impl.renderer];
  dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0);
  impl.queue = dispatch_queue_create("diorascii.audio", attr);
  topUp(impl);  // (before the clock runs: the first 200 ms are queued, then it starts)
  [impl.synchronizer setRate:1.0 time:kCMTimeZero];
  AudioOutput::Impl* p = &impl;
  impl.timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, impl.queue);
  dispatch_source_set_timer(impl.timer, dispatch_time(DISPATCH_TIME_NOW, 0), 20 * NSEC_PER_MSEC, 5 * NSEC_PER_MSEC);
  dispatch_source_set_event_handler(impl.timer, ^{
    if (p->renderer.status == AVQueuedSampleBufferRenderingStatusFailed) p->routeChanged = true;  // (start it again)
    else topUp(*p);
  });
  dispatch_resume(impl.timer);
  return true;
}

/// Stops the timer, waits for the queue to finish its last chunk, and releases everything of the renderer.
void teardown(AudioOutput::Impl& impl) {
  if (impl.timer != nil) {
    dispatch_source_cancel(impl.timer);
    dispatch_sync(impl.queue, ^{});  // (a chunk that is being rendered is finished)
    impl.timer = nil;
  }
  if (impl.synchronizer != nil) [impl.synchronizer setRate:0.0];
  [impl.renderer flush];
  impl.renderer = nil;
  impl.synchronizer = nil;
  impl.queue = nil;
  if (impl.format != nullptr) CFRelease(impl.format);
  impl.format = nullptr;
}
}  // namespace

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}
AudioOutput::~AudioOutput() { stop(); }

bool AudioOutput::start(const LayoutChoice& choice) {
  if (impl_->open) return true;
  Impl& impl = *impl_;
  impl.choice = choice;
  impl.routeChanged = false;
  @autoreleasepool {
    DeviceReport report;
#if TARGET_OS_OSX
    report = reportOfOutput();  // no audio session on macOS: the engine reports the device
    report.spatializes = report.channels <= 2 && defaultOutputIsBluetooth();
#else
    NSError* error = nil;
    AVAudioSession* session = [AVAudioSession sharedInstance];
    // Ambient background sound: plays on its own, mixes with other audio.
    if (![session setCategory:AVAudioSessionCategoryPlayback
                  withOptions:AVAudioSessionCategoryOptionMixWithOthers
                        error:&error]) {
      return false;
    }
    [session setSupportsMultichannelContent:YES error:nil];  // the route may then take more than two channels
    if (![session setActive:YES error:&error]) return false;
    const NSInteger most = session.maximumOutputNumberOfChannels;
    if (most > 2) [session setPreferredOutputNumberOfChannels:most error:nil];  // ask for all the route has
    report.channels = std::max<int>(1, static_cast<int>(session.outputNumberOfChannels));
    for (AVAudioSessionPortDescription* port in session.currentRoute.outputs)
      if (port.spatialAudioEnabled) report.spatializes = true;  // (the user's setting for the route, e.g. AirPods)
#endif
    impl.deviceChannels = report.channels;

    // The choice (or the richest layout that fits) is tried, stepping down to a smaller one if the layout has
    // no tag. On tvOS and iOS the route reports a count and no positions.
    Negotiation n = negotiate(choice, report);
    LayoutId id = LayoutId::Stereo;
    bool named = Layout::parse(n.layout.name(), id);
    bool ok = false;
    for (;;) {
      ok = tryRenderer(impl, n.layout, id);
      if (ok || (!named && n.layout.channels() == 1)) break;
      teardown(impl);
      LayoutId lower;
      if (!lowerLayout(id, lower)) break;  // stereo did not open either
      id = lower;
      n.layout = Layout::named(id);
      n.downmixed = n.layout.channels() > report.channels;
    }
    if (!ok) return false;
    impl.downmixed = n.downmixed;
  }
  Impl* p = impl_.get();
#if TARGET_OS_OSX  // (no audio session: the default device is what changes; the system flushes the renderer for the rest)
  AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                        kAudioObjectPropertyElementMain};
  p->deviceListener = ^(UInt32, const AudioObjectPropertyAddress*) { p->routeChanged = true; };
  AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &address, dispatch_get_main_queue(), p->deviceListener);
#else
  p->routeObserver = [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification
                                                                       object:nil
                                                                        queue:nil
                                                                   usingBlock:^(NSNotification*) { p->routeChanged = true; }];
  // (also when the user switches spatial audio on or off for the route: the layout may change with it)
  p->spatialObserver = [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionSpatialPlaybackCapabilitiesChangedNotification
                                                                       object:nil
                                                                        queue:nil
                                                                   usingBlock:^(NSNotification*) { p->routeChanged = true; }];
#endif
  p->flushObserver = [[NSNotificationCenter defaultCenter] addObserverForName:AVSampleBufferAudioRendererWasFlushedAutomaticallyNotification
                                                                       object:p->renderer
                                                                        queue:nil
                                                                   usingBlock:^(NSNotification*) { p->routeChanged = true; }];
  impl_->open = true;
  return true;
}

void AudioOutput::stop() {
  if (!impl_->open) return;
#if TARGET_OS_OSX
  if (impl_->deviceListener != nil) {
    AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                          kAudioObjectPropertyElementMain};
    AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &address, dispatch_get_main_queue(), impl_->deviceListener);
    impl_->deviceListener = nil;
  }
#else
  if (impl_->routeObserver != nil) [[NSNotificationCenter defaultCenter] removeObserver:impl_->routeObserver];
  impl_->routeObserver = nil;
  if (impl_->spatialObserver != nil) [[NSNotificationCenter defaultCenter] removeObserver:impl_->spatialObserver];
  impl_->spatialObserver = nil;
#endif
  if (impl_->flushObserver != nil) [[NSNotificationCenter defaultCenter] removeObserver:impl_->flushObserver];
  impl_->flushObserver = nil;
  teardown(*impl_);
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
