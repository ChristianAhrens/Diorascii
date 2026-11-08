/// @file audio/audio.h
/// @brief Plays the synthesized ambience on the default sound device.

#pragma once
#include <atomic>
#include <memory>

#include "core/ambience.h"

namespace dio {

/// Opens the default playback device and feeds it from an Ambience. Platform differences are
/// handled by miniaudio. The device runs on its own thread; setParams() is safe to call from
/// the main loop at any time.
class AudioOutput {
 public:
  AudioOutput();
  ~AudioOutput();  ///< stops the device
  /// Not copyable: the object owns the device.
  AudioOutput(const AudioOutput&) = delete;
  /// Not assignable: the object owns the device.
  AudioOutput& operator=(const AudioOutput&) = delete;

  /// Opens and starts the device. Returns false if there is none (headless machine, SSH
  /// session, no permission), in which case the app simply stays silent.
  bool start();
  /// Stops and closes the device. Does nothing if it is not running.
  void stop();
  /// True while the device is running.
  bool running() const;
  /// Sets what the sound reflects; takes effect smoothly.
  void setParams(const AmbientParams& p);

  struct Impl;  ///< device state (public so the audio callback can reach it)

 private:
  /// Device state.
  std::unique_ptr<Impl> impl_;
};

}  // namespace dio
