/// @file audio/audio.h
/// @brief Plays the synthesized ambience on the default sound device.

#pragma once
#include <memory>

#include "core/ambience.h"
#include "core/negotiate.h"

namespace dio {

/// Opens the default playback device and feeds it from an Ambience. Platform differences are
/// handled by miniaudio. The device runs on its own thread; setParams() is safe to call from
/// the main loop at any time (one writer thread): the parameters reach the audio thread as one
/// consistent copy through a Seqlock, so a new field of AmbientParams needs no change here.
///
/// The synth renders to the device's speaker layout (see negotiate()): by default the richest one
/// the device reports, or the one that was asked for, stepping down to a smaller one if the device
/// does not open with it.
class AudioOutput {
 public:
  AudioOutput();
  ~AudioOutput();  ///< stops the device
  /// Not copyable: the object owns the device.
  AudioOutput(const AudioOutput&) = delete;
  /// Not assignable: the object owns the device.
  AudioOutput& operator=(const AudioOutput&) = delete;

  /// Opens and starts the device with the layout `choice` asks for. Returns false if there is no
  /// device (headless machine, SSH session, no permission), in which case the app simply stays
  /// silent. Must not be called while the device is running; the synth's layout is set while it is stopped.
  bool start(const LayoutChoice& choice = LayoutChoice{});
  /// Stops and closes the device. Does nothing if it is not running.
  void stop();
  /// True while the device is running.
  bool running() const;
  /// Call every now and then from the main loop: if the device reported a change of route (headphones
  /// plugged in, another output chosen) the device is opened again with the same choice, so that the
  /// layout follows the route. Does nothing otherwise.
  /// @return true if the layout may have changed
  bool update();
  /// The layout the synth renders to (stereo until the device has been opened).
  const Layout& layout() const;
  /// How many channels the device was opened with.
  int deviceChannels() const;
  /// True if the layout has more channels than the device has, so that the sound server or the driver mixes it down.
  bool downmixed() const;
  /// Sets what the sound reflects; takes effect smoothly.
  void setParams(const AmbientParams& p);
  /// Hands a one-off sound to the synth (see SoundEvent). Safe to call from the main loop; events
  /// pile up in a lock-free queue (the latest 64 are kept) and are taken by the audio thread.
  void pushEvent(const SoundEvent& e);

  struct Impl;  ///< device state (public so the audio callback can reach it)

 private:
  /// Device state.
  std::unique_ptr<Impl> impl_;
};

}  // namespace dio
