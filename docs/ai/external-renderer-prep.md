# Preparing for an external renderer: stems, individual outputs and ADM-OSC

Written 2026-11-17 on branch `feature/spatialAudio` (tip after the history rewrite, on `develop` at `f40b0b9`).
**Status: analysis only, nothing of this is implemented.** It records how the sound path is built today and
where it has to be cut so that a later feature can (a) play the sound stems and the ambience on dedicated,
individual audio outputs and (b) send the positioning metadata separately to an external renderer. The first
feature on top would send audio to a renderer and the metadata as ADM-OSC over UDP to the same renderer.
Everything below was read from `src/core/ambience.*`, `soundstage.h`, `spatializer.h`, `layout.*`,
`negotiate.*`, `src/audio/audio.*` and the sound section of [architecture.md](architecture.md); nothing was run
for it, and the Apple backend has never been compiled (see F-26 in [findings.md](findings.md)).
Related: [spatial-audio-plan.md](spatial-audio-plan.md), which states "object-based audio ... is out of scope"
for the OS renderers; this document is about the app's own objects leaving the process.

## 1. The pipeline today

    MAIN THREAD (20 fps)                                    AUDIO THREAD (device callback)
    World (simulation, headless)                            AudioOutput onAudio
      |- ambientFor(World) -> AmbientParams                   |- Seqlock<AmbientParams>.load()
      |    (levels + Emitter[19]: level, x, y, depth, spread) |- EventRing<SoundEvent, 64>.pop() -> Ambience::event()
      '- World::takeEvents -> SoundEvent{kind,x,y,depth,str}  '- Ambience::render(params, out, frames)   one monolith
            Session::ambient() / takeSoundEvents()                per sample:
            AudioOutput::setParams / pushEvent ------------->      1. beds: <scene>Sample() -> float[chain]
                                                                      (one chain = one speaker, independent noise)
                                                                   2. placed voices (58 "places", PlaceId):
                                                                      mono sample -> SpatialVoice::add(x, room_, nch)
                                                                      (DBAP gains, smoothing, LFE send)
                                                                      -> flushRoom() -> voiceOut_[chain]
                                                                      or noise weighted per chain: weight(id, chain)
                                                                   3. sum per chain -> softClip -> x volume
                                                                   4. LFE = low-pass(sum of chains) + bass sends
                                                                   5. feed_[] / routeGain_ -> interleaved frame
                                                                      of layout.channels() (<= kMaxChannels = 16)
                                                          miniaudio device opened with the Layout chosen by negotiate.*

## 2. What already helps

- The core is headless and I/O-free (CLAUDE.md rule 6): a second sink is another frontend, the simulation stays untouched.
- Position metadata exists in stage coordinates: `Emitter` and `SoundEvent` carry `x` (-1.5 to 1.5, +-1 are the screen
  edges), `y` (0 ground to 1 top), `depth` (0 near to 1 far) and `spread`. `Ambience::stageSource()` already turns them into
  azimuth and elevation (screen edges +-45 degrees, +-1.5 is 110, 0..1 high is 0..60 up). An ADM-OSC mapping needs only a
  coordinate convention on top.
- `Seqlock` and `EventRing` (core/seqlock.h) are generic lock-free hand-overs.
- Device ports are abstracted (`DevicePort`, `Layout::fromDevice`, AUX channels, `padChannels`).
- Golden tests (`tests/ambience_golden*.inc`, `--dump-golden`) pin the mix, so any new path can be held to "the sum of
  the stems renders exactly today's output".

## 3. Gaps

1. **No stems exist.** Only the mixed per-chain signal does. Beds are channel-based by construction (one independent noise
   chain per speaker). Of the 58 places, 26 `v.add(...)` calls are mono voices that are panned afterwards (the clean object
   candidates); 15 sounds are generated per chain with `weight(id, chain)` (thunder, cloud, tornado, worm, whale, train,
   flood, ...): decorrelated multichannel sounds, not mono objects. Everything is summed per chain, then `softClip`
   and the volume are applied, and the LFE is derived from the sum, so no per-voice or per-bus signal survives.
2. **Synthesis, placement and speaker rendering are fused in `Ambience::render()`.** Voices call
   `place_[id].v.add(sample, room_, nch)` and write speaker gains directly; there is no voice-output abstraction in front of it.
3. **Object metadata is private to the audio thread.** `Place::src` (azimuth, elevation, spread, lfeSend) is the real object
   metadata but nothing outside `Ambience` sees it. Many sounds are triggered inside the synth by its own random generator
   (car passes, honks, bubbles, crickets), so the main thread does not know about them: their metadata can only come from
   the audio thread, a direction no data flows in today (all flows are main to audio). `Source` has no distance, no per-object
   gain and no stable object index; `PlaceId` is an internal enum and some ids are transient slots (`kBubble0 + i`).
4. **The output side knows only a `Layout`.** One interleaved buffer of speaker positions, at most 16 channels. There is no
   output channel whose role is "object slot N"; `padChannels` only pads with silence; `negotiate.*` builds a layout from
   the device, not a channel plan. (Since 2026-11-17 a first piece of a channel plan exists: on macOS a named layout is
   *routed* to the device channels of the same positions, `Negotiation::routed` / `route[]` / `routeChannels()`, see F-26;
   it is layout channel to device channel only, with no roles.)
5. **No sample clock in the metadata.** `Ambience::t_` counts seconds rendered, but nothing ties a metadata message to an
   audio frame position.

## 4. Seams to prepare (design, not decided)

1. Cut the voice and bed output off from the renderer. Synthesis writes a `StemBlock`: the bed chains plus voice slots, each
   voice slot a mono buffer with an `ObjectMeta` (stable slot id, on, position, spread, lfeSend, gain, sample position).
   The renderer is an interface with two implementations: `LayoutRenderer` is today's logic (SpatialVoice, route, LFE,
   softClip) and must stay bit-identical to the golden values; a passthrough renderer maps beds and objects straight to outputs.
2. Classify every place as object or bed. The 15 per-chain sounds either become mono plus spread (changes the sound and the
   goldens) or are declared part of the bed.
3. Introduce an `OutputPlan`: a list of output channels, each with a role (bed speaker, object slot, LFE), separate from `Layout`.
4. An outbound metadata ring from the audio thread to a sender thread; only the sender touches UDP. It belongs outside
   `src/core` (rule 6), e.g. a `src/net` with plain sockets, no library (rule 7).
5. A fixed pool of object slots with stable indices, since ADM-OSC needs object numbers that stay put (transient voices such
   as bubbles and shots do not have them today).

## 5. Open decisions (owner)

- Which of the 15 per-chain sounds become objects and which stay in the bed, given that making them mono changes the sound
  and the golden values.
- The coordinate convention of the ADM-OSC messages (polar azimuth/elevation/distance or cartesian) and how the stage's
  `depth` maps to a distance.
- The rate of the metadata: the world's 20 fps, or once per render block, timestamped with the audio frame position.
- One multichannel interface (beyond `kMaxChannels = 16`?) or several devices for the dedicated outputs.
- Whether the bed goes to the renderer as a channel bed in the negotiated layout, or as the layout-independent source
  signals.
