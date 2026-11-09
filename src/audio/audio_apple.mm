/// @file audio/audio_apple.mm
/// @brief Playback device wrapper for tvOS and iOS, built on AVAudioEngine.

#include "audio/audio.h"

#import <AVFoundation/AVFoundation.h>

#include <vector>

namespace dio {

/// Device state: the engine, and the parameters shared with the audio thread.
struct AudioOutput::Impl {
  /// Pulls samples from the synth.
  AVAudioEngine* engine = nil;
  bool open = false;
  Ambience ambience{48000};
  /// Interleaved samples from the synth, split into the engine's two channel buffers.
  std::vector<float> scratch = std::vector<float>(2 * 4096);
  /// Parameters, written by the main thread and read by the audio thread.
  std::atomic<int> kind{0};
  std::atomic<float> night{0}, rain{0}, volume{1};
  std::atomic<unsigned> flashes{0};
  std::atomic<float> storm{0}, whale{0}, tornado{0}, avalanche{0};
  std::atomic<float> worm{0}, siren{0}, wail{0}, train{0}, plane{0}, fire{0};
  std::atomic<unsigned> nibbles{0}, impacts{0}, crushes{0}, squeals{0};

  /// Reads the shared parameters into one AmbientParams.
  AmbientParams params() const {
    AmbientParams p;
    p.kind = static_cast<AmbientKind>(kind.load());
    p.night = night.load();
    p.rain = rain.load();
    p.volume = volume.load();
    p.flashes = flashes.load();
    p.storm = storm.load();
    p.whale = whale.load();
    p.tornado = tornado.load();
    p.avalanche = avalanche.load();
    p.nibbles = nibbles.load();
    p.impacts = impacts.load();
    p.worm = worm.load();
    p.siren = siren.load();
    p.wail = wail.load();
    p.train = train.load();
    p.plane = plane.load();
    p.fire = fire.load();
    p.crushes = crushes.load();
    p.squeals = squeals.load();
    return p;
  }
};

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}
AudioOutput::~AudioOutput() { stop(); }

bool AudioOutput::start() {
  if (impl_->open) return true;
  @autoreleasepool {
    NSError* error = nil;
    AVAudioSession* session = [AVAudioSession sharedInstance];
    // Ambient background sound: plays on its own, mixes with other audio.
    if (![session setCategory:AVAudioSessionCategoryPlayback
                  withOptions:AVAudioSessionCategoryOptionMixWithOthers
                        error:&error] ||
        ![session setActive:YES error:&error]) {
      return false;
    }
    // The engine's mixer takes the standard format: separate buffers per channel.
    AVAudioFormat* format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000 channels:2];
    Impl* impl = impl_.get();
    AVAudioSourceNode* source = [[AVAudioSourceNode alloc]
        initWithFormat:format
           renderBlock:^OSStatus(BOOL*, const AudioTimeStamp*, AVAudioFrameCount frames,
                                 AudioBufferList* out) {
             if (impl->scratch.size() < 2 * static_cast<size_t>(frames)) impl->scratch.resize(2 * frames);
             impl->ambience.render(impl->params(), impl->scratch.data(), static_cast<int>(frames));
             float* left = static_cast<float*>(out->mBuffers[0].mData);
             float* right = static_cast<float*>(out->mBuffers[1].mData);
             for (AVAudioFrameCount i = 0; i < frames; ++i) {
               left[i] = impl->scratch[2 * i];
               right[i] = impl->scratch[2 * i + 1];
             }
             return noErr;
           }];
    AVAudioEngine* engine = [[AVAudioEngine alloc] init];
    [engine attachNode:source];
    // connect:to:format: is deprecated from tvOS 27 but the replacement does not exist on 17.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [engine connect:source to:engine.mainMixerNode format:format];
#pragma clang diagnostic pop
    if (![engine startAndReturnError:&error]) return false;
    impl_->engine = engine;
    impl_->open = true;
  }
  return true;
}

void AudioOutput::stop() {
  if (!impl_->open) return;
  [impl_->engine stop];
  impl_->engine = nil;
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
}

}  // namespace dio
