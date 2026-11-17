# Immersive multichannel sound on Apple platforms: the path to AVSampleBufferAudioRenderer

Written 2026-11-17. This is a reference for anyone (human or AI) who wants an app to play **multichannel
sound that the system places on the speakers and spatializes on AirPods**, on macOS, tvOS and iOS, without
handling channel maps by hand. It records what was tried in this repository, in order, what each attempt
measured, why it was dropped, and what finally worked. The code is `src/audio/audio_apple.mm` (backend),
`src/core/negotiate.*` (the choice of layout, pure logic) and `src/app/session.*` (the menu's *Sound* entry).
Everything here was measured on one Mac (macOS 27, AirPods Pro, a 16-channel loopback device); what was not
verified is listed at the end. Statements that come from Apple's headers or documentation are marked so.

## The short answer

Play through **`AVSampleBufferAudioRenderer`** (with an `AVSampleBufferRenderSynchronizer`), and feed it
**interleaved PCM tagged with a Core Audio channel layout** (`AudioChannelLayoutTag`, for example
`kAudioChannelLayoutTag_Atmos_7_1_4`) in a `CMAudioFormatDescription`. The system then

- places each channel on the output channel with the same position (speaker devices, any channel order),
- folds a richer layout into the channels the device has, without leaking onto unused ones,
- and offers its own **3D audio** (spatial audio, head tracking) on AirPods and other headphones that have it.

The same code serves macOS, tvOS and iOS. Only the way the app learns *what the route is* differs (below).
Neither a plain Core Audio output (miniaudio, an `AudioUnit`) nor an `AVAudioEngine` graph gets the AirPods'
3D audio, whatever its channel layout.

## The path, in order

### 1. Start: one portable backend (miniaudio) with a channel map

Goal: ambient sound for up to 9.1.6 speakers. The synth renders to a speaker layout (`src/core/layout.*`) and
the backend opens the device with a matching channel count. On Windows and Linux the vendored miniaudio does
this and still does.

On macOS it was not enough. miniaudio assumes a channel map for a given count, so it lost the positions of a
7.1.4 device (the heights) and plays on channels the device does not use. The first fix was
`src/audio/coreaudio_layout.*`: read the device's own channel layout from Core Audio
(`kAudioDevicePropertyPreferredChannelLayout`), turn the labels into positions, and *route* the synth's channels
to the device channels with the same position (`routeLayout()` in negotiate). It worked (measured by recording
a loopback device and taking the RMS of every channel) but it was custom channel-map handling for one
platform, with gaps that come from the user's Audio MIDI Setup defaults rather than from anything the app
could decide.

### 2. The owner's question: could macOS, tvOS and iOS be treated the same, Apple-natively?

The owner wanted Apple-native behavior and no custom channel maps on macOS. A study of the Apple audio APIs
followed. Findings that mattered (headers of the macOS 27 SDK, verified by reading and by small probes):

- `AVAudioSession` (route, `outputNumberOfChannels`, `setSupportsMultichannelContent`, `spatialAudioEnabled`
  of a port, `AVAudioSessionSpatialPlaybackCapabilitiesChangedNotification`, `renderingMode`) exists on
  tvOS and iOS only; it is **unavailable on macOS**. `setIntendedSpatialExperience:` and
  `CASpatialAudioExperience` are visionOS only. The Core Audio headers have no "this route spatializes" property.
- A tvOS or iOS route reports a channel **count**, not positions.
- On macOS an `AVAudioEngine`'s `outputNode` format carries the device's layout *with labels*
  (`outputFormatForBus:0`), so labels can be read without Core Audio calls of our own.

### 3. Attempt A: one `AVAudioEngine` backend for all three platforms

Idea: hand the engine a tagged multichannel source, connect the main mixer to the output node in the device's
own format, and let the system map channels by label. Measured:

- The mixer stays **stereo** (everything folded to two channels) unless it is connected to the output in the
  device format. With that connection, label mapping works on speaker devices, and richer sources fold down.
- It was implemented and the speaker-device tables were right.
- **On AirPods the system reported "3D audio unavailable"**, for both a plain tagged source and the mixer
  connected to the output. So the engine path is fine for speakers and useless for headphones' spatial audio.

A note on method, because it cost time: for a while the app-level checks ran a *stale binary* (an old
miniaudio build in a different build directory than the script wrote to). The probes (small stand-alone
programs, not the app) were valid; the app-level conclusions from that period were redone later with the
right binary. When a result contradicts the design, check which executable ran.

### 4. Research: what must the app feed to which API to get 3D audio on AirPods?

From Apple's documentation and headers: the system offers spatial audio to content that goes through
`AVPlayerItem` or **`AVSampleBufferAudioRenderer`**, controlled by `allowedAudioSpatializationFormats`
(multichannel by default; stereo or mono only if the app opts in). A plain Core Audio client is not a
candidate, which matches the measurement in 3. The renderer takes `CMSampleBuffer`s, so the app builds
tagged PCM itself.

### 5. The probe that decided it

A small program (not part of the repo; ~100 lines of Objective-C++) enqueued tagged 7.1.4 PCM into an
`AVSampleBufferAudioRenderer`. With AirPods as the output: **"3D audio available and active"**, and the test
tone moved around the listener. With 9.1.6 as well. The owner confirmed this by ear and by the menu bar.

The same renderer was then run into the 16-channel loopback device, set in turn to 9.1.6, 7.1.4, 5.1 and 2.0,
with amplitude-coded sources (each source channel a different level, recovered from the recording):

- 7.1.4, 9.1.6 and 5.1 sources are mapped **by label**, identically to the engine's mapping. On the 9.1.6
  device the wides and top-middles come last (out 9 to 12 and 13 to 16), as the device's labels say.
- A richer source folds into the device's channels; **nothing reaches unused channels**.

So one renderer covers the speaker case (like the engine) *and* AirPods (unlike the engine).

### 6. Design: one Apple backend, and a rule for the route

The owner asked for "a better and more stable way to determine what route to take" and for tvOS to use the same
approach. The result:

- **One backend** on macOS, tvOS and iOS: renderer + synchronizer, a serial user-interactive queue and a 20 ms
  timer, 100 ms chunks, 0.2 s kept queued ahead of playback. Event-driven sounds therefore start about a
  quarter of a second late (designed, not measured). miniaudio stays for Windows and Linux only.
- **Layout choice** (`negotiate()`, pure logic, unit-tested): the route reports a channel count, positions if it
  has them, and `spatializes`. *Immersive* on a route of one or two channels that spatializes gets **7.1.4**
  (the route stays a pair; the system makes the pair from the layout). On a speaker device the device's own
  layout is used, stepping down (9.1.6 to 7.1.4 to 7.1 to 5.1 to 2.0) where the device has no place for a
  channel. *Stereo* is an explicit 2.0, which the system leaves alone (no 3D audio, by design).
- **Where `spatializes` comes from:** on tvOS and iOS, `spatialAudioEnabled` of the route's ports (API exists
  there; **not tried on hardware**). On macOS there is no such property, so it is a heuristic: a default output of
  two channels or fewer whose Core Audio transport type is Bluetooth (`kAudioDevicePropertyTransportType`).
  AirPods report `'blue'`. A non-AirPods Bluetooth device would be treated the same (not verified).
- **Where the channel count and positions come from on macOS:** a throwaway `AVAudioEngine` whose `outputNode`
  format supplies them (see 2); no Core Audio channel-layout reader of our own, no routing code.
- **Route changes:** the default-output-device property listener (macOS), the route-change and spatial-capability
  notifications (tvOS, iOS) and `AVSampleBufferAudioRendererWasFlushedAutomaticallyNotification` (all) restart
  the renderer with the same choice.

### 7. The menu: off / stereo / immersive

The old *Sound on/off* plus *Speakers* (auto and nine layouts) was replaced by one *Sound* entry,
off / stereo / **immersive** (default). The small print says what really plays: `Audio output: 7.1.4 on 2
channels (spatialized)`, `... (mixed down)`, `Audio output: 2.0 on 2 channels`, `Audio output: sound off`.
`--audio-layout` remains as a test option naming the immersive layout.

### Verified on AirPods Pro (macOS), reported from the system's menu

| Mode | App says | System says |
|---|---|---|
| immersive | `7.1.4 on 2 channels (spatialized)` | 3D audio available and active |
| stereo | `2.0 on 2 channels` | 3D audio unavailable |

## What to copy into another project

1. Make the *content* a tagged multichannel PCM stream in an `AVSampleBufferAudioRenderer`
   (see `tryRenderer()` and `enqueueChunk()` in `src/audio/audio_apple.mm`): an ASBD with the channel count,
   an `AudioChannelLayout` with the tag, `CMAudioFormatDescriptionCreate`, interleaved samples in the **tag's
   channel order**, `CMAudioSampleBufferCreateReadyWithPacketDescriptions` with a presentation time that
   counts frames, `[synchronizer setRate:1.0 time:kCMTimeZero]`, and keep a short lead queued.
2. Do **not** expect an `AVAudioEngine`, `AudioUnit` or miniaudio output to get AirPods' 3D audio.
3. Do not detect the route yourself where you can avoid it: let the system place and fold. You need the route
   only to decide *how many channels to produce* (immersive on a pair that spatializes: a 7.1.4 layout).
4. Measure speaker placement with a loopback device (BlackHole 16ch) and amplitude-coded channels instead of
   by ear; measure AirPods by the system's own "3D audio" indicator.
5. Keep the choice of layout in code that has no audio API in it (`negotiate()`), so it is testable on CI.

## Not verified

- tvOS and iOS hardware: the audio-session calls, `spatialAudioEnabled`, the route's channel count. (The tvOS
  simulator's route is stereo.)
- Whether the AirPods spatializer renders 9.1.6 as 9.1.6 (only 7.1.4 is chosen for them).
- Other Bluetooth devices on macOS, and a device change while playing.
- The latency of event-driven sounds (designed at 0.2 to 0.3 s).
- Everything by ear in a room: the tables are recordings, the AirPods result is the system's indicator.

## Where the details are

- [architecture.md](architecture.md): "Apple platforms" and "Sound modes".
- [findings.md](findings.md) F-26: the measurements and what is unverified, in the order they were made.
- [spatial-audio-plan.md](spatial-audio-plan.md): the original plan; its AirPods question 4 is answered here.
- Commits on `feature/immersiveAudio` (the series that replaced the first approach): "Play on the speaker layout of the
  device", "Play on the audio route's speaker layout on macOS, tvOS and iOS", "Replace the Speakers entry by an
  off, stereo or immersive Sound entry".
