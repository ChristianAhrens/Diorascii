/// @file audio/audio.cpp
/// @brief Playback device wrapper around miniaudio.

#include "audio/audio.h"

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
  /// Parameters, written by the main loop and read by the audio thread.
  std::atomic<int> kind{0};
  std::atomic<float> night{0}, rain{0}, volume{1};
  std::atomic<unsigned> flashes{0};
  std::atomic<float> storm{0}, whale{0}, tornado{0}, avalanche{0};
  std::atomic<float> worm{0}, siren{0}, wail{0}, train{0}, plane{0}, fire{0}, menace{0};
  std::atomic<unsigned> nibbles{0}, impacts{0}, crushes{0}, squeals{0};
};

namespace {
void onAudio(ma_device* device, void* output, const void*, ma_uint32 frames) {
  auto* impl = static_cast<AudioOutput::Impl*>(device->pUserData);
  AmbientParams p;
  p.kind = static_cast<AmbientKind>(impl->kind.load());
  p.night = impl->night.load();
  p.rain = impl->rain.load();
  p.volume = impl->volume.load();
  p.flashes = impl->flashes.load();
  p.storm = impl->storm.load();
  p.whale = impl->whale.load();
  p.tornado = impl->tornado.load();
  p.avalanche = impl->avalanche.load();
  p.nibbles = impl->nibbles.load();
  p.impacts = impl->impacts.load();
  p.worm = impl->worm.load();
  p.siren = impl->siren.load();
  p.wail = impl->wail.load();
  p.train = impl->train.load();
  p.plane = impl->plane.load();
  p.fire = impl->fire.load();
  p.crushes = impl->crushes.load();
  p.squeals = impl->squeals.load();
  p.menace = impl->menace.load();
  impl->ambience.render(p, static_cast<float*>(output), static_cast<int>(frames));
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

void AudioOutput::setParams(const AmbientParams& p) {
  impl_->kind = static_cast<int>(p.kind);
  impl_->night = p.night;
  impl_->rain = p.rain;
  impl_->volume = p.volume;
  impl_->flashes = p.flashes;
  impl_->storm = p.storm;
  impl_->whale = p.whale;
  impl_->tornado = p.tornado;
  impl_->avalanche = p.avalanche;
  impl_->nibbles = p.nibbles;
  impl_->impacts = p.impacts;
  impl_->worm = p.worm;
  impl_->siren = p.siren;
  impl_->wail = p.wail;
  impl_->train = p.train;
  impl_->plane = p.plane;
  impl_->fire = p.fire;
  impl_->crushes = p.crushes;
  impl_->squeals = p.squeals;
  impl_->menace = p.menace;
}

}  // namespace dio
