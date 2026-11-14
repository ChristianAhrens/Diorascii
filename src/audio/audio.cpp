/// @file audio/audio.cpp
/// @brief Playback device wrapper around miniaudio.

#include "audio/audio.h"

#include "core/seqlock.h"

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
};

namespace {
void onAudio(ma_device* device, void* output, const void*, ma_uint32 frames) {
  auto* impl = static_cast<AudioOutput::Impl*>(device->pUserData);
  impl->shared.load(impl->current);
  SoundEvent e;
  while (impl->events.pop(e)) impl->ambience.event(e);
  impl->ambience.render(impl->current, static_cast<float*>(output), static_cast<int>(frames));
}
}  // namespace

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}
AudioOutput::~AudioOutput() { stop(); }

bool AudioOutput::start() {
  if (impl_->open) return true;
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = 48000;
  cfg.dataCallback = onAudio;
  cfg.pUserData = impl_.get();

  // Sound libraries such as ALSA print their complaints to stderr, which would scribble over
  // the scene. Silence stderr while the device opens.
#ifndef _WIN32
  const int savedErr = dup(STDERR_FILENO);
  const int devNull = open("/dev/null", O_WRONLY);
  if (devNull >= 0) dup2(devNull, STDERR_FILENO);
#endif
  bool ok = ma_device_init(nullptr, &cfg, &impl_->device) == MA_SUCCESS;
  if (ok && ma_device_start(&impl_->device) != MA_SUCCESS) {
    ma_device_uninit(&impl_->device);
    ok = false;
  }
#ifndef _WIN32
  if (savedErr >= 0) { dup2(savedErr, STDERR_FILENO); close(savedErr); }
  if (devNull >= 0) close(devNull);
#endif
  impl_->open = ok;
  return ok;
}

void AudioOutput::stop() {
  if (!impl_->open) return;
  ma_device_uninit(&impl_->device);
  impl_->open = false;
}

bool AudioOutput::running() const { return impl_->open; }

void AudioOutput::setParams(const AmbientParams& p) { impl_->shared.store(p); }

void AudioOutput::pushEvent(const SoundEvent& e) { impl_->events.push(e); }

}  // namespace dio
