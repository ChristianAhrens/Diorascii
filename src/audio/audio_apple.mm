/// @file audio/audio_apple.mm
/// @brief Playback device wrapper for tvOS and iOS, built on AVAudioEngine.

#include "audio/audio.h"

#include "core/seqlock.h"

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
  /// Parameters, written by the main thread and read by the audio thread, as one consistent copy.
  Seqlock<AmbientParams> shared;
  /// The parameters the audio thread uses: the last copy it read (kept if a read does not succeed).
  AmbientParams current;
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
             impl->shared.load(impl->current);
             impl->ambience.render(impl->current, impl->scratch.data(), static_cast<int>(frames));
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

void AudioOutput::setParams(const AmbientParams& p) { impl_->shared.store(p); }

}  // namespace dio
