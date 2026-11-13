# Plan: multichannel and spatial audio

Written 2026-11-11 on branch `feature/spatialAudio`. **Status: plan only, nothing is implemented.**
Facts about the current code were read from `src/core/ambience.*`, `src/audio/*` and
`apple/tvOS/*` on 2026-11-11 (develop at `c48ca07`). Statements about Apple, miniaudio and
operating-system behaviour come from the assistant's general knowledge, not from a test in this
repository; every one of them is marked **[verify]** and is a checkpoint in the spike (phase 0).
Nothing in this plan has been heard, seen or run on a surround system, HDMI receiver or AirPods.

**Revision 2026-11-13** (branch now after the v1.3.0 pin): the bed-plus-objects model is stated
explicitly (section 3.0), beds become **one independent chain per speaker by default** instead of
four shared groups (section 3.6, phase 3), a measured CPU baseline of today's stereo synth was added
(sections 2.4 and 8), the miniaudio auto-detect idea and the alternatives considered were added
(sections 3.3, 5.2), and the headphone stage was reframed as a speaker virtualizer (phase 9).
Revised parts are marked **(rev. 2026-11-13)**.

## 1. Goal and scope

Let the ambient sound use as many speakers as the output offers, up to 9.1.6, and let the
operating system spatialize it where it can.

**In scope**

- Channel-based multichannel output: 2.0, 5.1, 7.1, 5.1.2, 5.1.4, 7.1.2, 7.1.4, 9.1.4 and 9.1.6,
  plus whatever channel map a device reports.
- Multichannel over HDMI (terminal builds through miniaudio, tvOS through AVAudioEngine).
- Spatial audio on AirPods through the operating system's own spatializer, fed with a multichannel
  stream (tvOS first; macOS to be explored).
- Sounds that come from where their sprites are, instead of from random pans or the centre.
- Automatic choice of the layout from the device, with a manual override.

**Out of scope** (decided with the owner on 2026-11-11)

- Object-based audio and Dolby Atmos authoring or metadata. The output is channel-based beds.
  Atmos *speaker layouts* (7.1.4, 9.1.6) are used only as channel counts and positions.
- Recorded samples. The sound stays synthesized.

**Non-goals that follow from the project rules**

- No new libraries on the terminal path (rule 7). Apple frameworks on tvOS are already in use.
- The core stays headless, I/O-free and deterministic per seed (rule 6).

## 2. Where the code stands

### 2.1 Structure

- `Ambience` (`src/core/ambience.h/.cpp`, 927 lines) synthesizes everything. Hard-coded to two
  channels: `float out[2]`, per-ear filter state `x_[ch]`, `for (int ch = 0; ch < 2; ++ch)`.
  `render()` writes interleaved stereo float.
- `AmbientParams` carries about two dozen scalar fields. They reach the audio thread through the
  same number of `std::atomic`s, copied by hand in four places (`audio.cpp` twice,
  `audio_apple.mm` twice) — finding F-19. A field missing in a copy is silent in the real program
  while the synthesis tests still pass.
- miniaudio is opened with `channels = 2`, 48 kHz, f32 (`src/audio/audio.cpp`). The Apple backend
  (`audio_apple.mm`) builds a 2-channel standard `AVAudioFormat` and an `AVAudioSourceNode`, session
  category `Playback` with `MixWithOthers`.
- `ambientFor(const World&)` derives the parameters. Position information is reduced to
  `World::nearness(key)`: how close the nearest live entity with that tag/sprite is to the screen
  centre, **direction lost**. Events are bare counters (`flashCount`, `foodEatenCount`,
  `impactCount`, `crushCount`, `squealCount`, `shotCount`) that carry no position.

### 2.2 What each sound is today

Three kinds of stereo exist in the synth:

1. **Decorrelated beds.** Each ear runs its own noise and filter state, so the image is wide
   already: forest wind, rain, thunder rumble; aquarium hum, flow and growl; desert wind, sand,
   tornado roar and growl; mountain wind, ice hiss and stream; city traffic, tyre hiss, train
   rumble, fire roar, jet; moon pad (oscillators with a phase offset per ear).
2. **Panned point voices.** Constant-power pan `cos/sin(pan * pi/2)` with a **random** pan:
   aquarium bubbles (0.2–0.8) and whale phrases, mountain eagle cry and woodpecker, city cars
   (0.1–0.9) and horns, rifle shots (0.25–0.75). They do not follow entities.
3. **Dual mono.** The same sample in both ears: siren, fire-engine wail, train horn and klack,
   sandworm bellow, kraken gulp, avalanche thump, and the whole second catastrophe except its noise
   beds (rotor chop envelope, creature stamps and roars, flame streams, flood and meteor rumble).

### 2.3 The gap

The synth knows *how loud* things are but not *where*. A siren on the left and one on the right
sound identical, a shot does not come from the helicopter, the winged monster's screech is not
where it flies. Multichannel without positions would only spread noise around; the positions are
the work.

### 2.4 Measured baseline (rev. 2026-11-13)

Measured on 2026-11-13 with a throwaway benchmark (not committed) calling `Ambience::render()`
for 10 s of audio at 48 kHz stereo per scene, rain 0.6, night 0.5, storm 0.8, built with plain
`-O2` (not the release preset) on an Intel i3-4010U (2013, 1.7 GHz), single run, not profiled:

| Scene | CPU per second of audio |
|---|---|
| Forest | 3.1 % of one core |
| Aquarium | 3.0 % |
| Desert | 3.5 % |
| Moon | 4.3 % |
| Mountains | 4.9 % |
| City | 9.2 % |

Not known: how much of this is the per-channel bed chains (scales with channel count) and how much
is `sin`/`exp` in the voices (does not scale). The first phase-2 task is to profile that split.
The cost is dominated by what the existing per-sample loops do, so the numbers are only a guide
for relative scaling, not a prediction for Apple silicon.

## 3. Target design

### 3.0 The model: one bed, many objects, one layout (rev. 2026-11-13)

There is exactly one output format, the **channel layout** of the device. Two kinds of material are
mixed into it by the synth itself:

- **Bed:** continuous, non-directional or wrap-around ambience (wind, rain, hum, traffic, pad). It
  is synthesized per speaker, so every full-range speaker carries its own independent noise and
  the listener is enveloped, including the height speakers.
- **Objects:** discrete sources with a position (a siren, a shot, a whale, a stamping monster).
  They are synthesized mono and **rendered by Diorascii's own spatializer** (section 3.3) into the
  same channels as the bed. They are "objects" only inside the synth; the output stays plain
  channel PCM. This is what "object-based audio is out of scope" means: no Atmos metadata and no
  OS object renderer (Windows `ISpatialAudioClient`, Apple `AVAudioEnvironmentNode`), because none
  of them is portable and none is reachable through miniaudio.

The same model serves speakers (render to the layout), AirPods (the OS spatializes the layout) and,
if needed, any other headphones (the app virtualizes the layout, phase 9).

### 3.1 Space model: the diorama

The listener sits in front of a window onto the scene. This is the recommendation; the owner
decides in section 11.

- **Azimuth** from the sprite's centre x. The screen edges map to `±stageHalfAngle` (default 45°).
  Entities beyond the edges map outward to ±110° at the limit of the existing "starts before it is
  in view" reach (`width/2 + entityWidth/2 + 0.25*width`), so an approaching train is heard from the
  side before it appears. Entities that leave the screen continue toward the rear.
- **Elevation** from the sprite's centre y: ground level 0°, the top of the screen about 60°. In
  scenes without a sky (aquarium) the same mapping is used for "up the tank".
- **Distance** from the layer depth (`z`; larger is further away): gain fall-off, a low-pass that
  darkens far sources, and a little bleed into the surrounds.
- **Beds** (wind, rain, hum, traffic) wrap around the listener on all speakers, including heights
  for rain and wind.
- **LFE** gets a bass-managed send (below about 120 Hz) from the heavy sources: thunder rumble,
  stamps, impacts, avalanche, tornado, worm, whale, meteor, flame body.

### 3.2 Layouts

Internal canonical channel order, independent of any backend (backends map it, section 7):

| Index | Name | Azimuth | Elevation |
|---|---|---|---|
| 0 | L | -30° | 0° |
| 1 | R | +30° | 0° |
| 2 | C | 0° | 0° |
| 3 | LFE | – | – |
| 4 | Lw | -60° | 0° |
| 5 | Rw | +60° | 0° |
| 6 | Ls | -90° | 0° |
| 7 | Rs | +90° | 0° |
| 8 | Lrs | -135° | 0° |
| 9 | Rrs | +135° | 0° |
| 10 | Ltf | -45° | 45° |
| 11 | Rtf | +45° | 45° |
| 12 | Ltm | -90° | 45° |
| 13 | Rtm | +90° | 45° |
| 14 | Ltr | -135° | 45° |
| 15 | Rtr | +135° | 45° |

A `Layout` is an ordered subset: 2.0 = {L,R}; 5.1 = {L,R,C,LFE,Ls,Rs} with sides at ±110°;
7.1 = 5.1 plus {Lrs,Rrs}; 5.1.2 = 5.1 + {Ltm,Rtm}; 5.1.4 = 5.1 + {Ltf,Rtf,Ltr,Rtr}; 7.1.2, 7.1.4 likewise;
9.1.4 = 7.1.4 + {Lw,Rw}; 9.1.6 = 9.1.4 + {Ltm,Rtm}. Per-layout angle overrides (the 5.1 sides at 110° instead
of 90°) live in the layout table. Device-reported maps that match no named layout are mapped speaker
by speaker to the nearest canonical position by angle; unknown or unmappable channels stay silent.

### 3.3 Spatializer

**Alternatives considered (rev. 2026-11-13).** *miniaudio's `ma_spatializer`:* already vendored and
gives attenuation and Doppler, but it belongs to `ma_engine`'s node graph, its panning is basic and
its handling of height channels is unknown **[verify]**; the synth is procedural, so it would only
act as a pan stage. Not adopted unless a one-day check shows it covers heights and Doppler well.
*Ambisonics intermediate (first or third order):* layout independent and elegant for beds, but
point sources blur at low order and irregular layouts need an optimized decoder; more machinery than
this app needs. *Third-party renderers (OpenAL Soft, Steam Audio, Resonance Audio, libiamf):*
capable, but all violate rule 7. *VBAP:* needs triangulation of the speaker set; DBAP does not.

`Spatializer` in `src/core` (pure, headless):

- Distance-based amplitude panning (DBAP) over the unit sphere. Gain of speaker `i` for a source
  direction `s`: `g_i = w_i / (d_i^a + blur)`, `d_i = |s - speaker_i|`, normalised so that
  `sum g_i^2 = 1` (constant power), roll-off `a` from 6 dB per distance doubling, speaker weights
  `w_i` for front-bias. LFE is excluded from the panning and fed by the bass send.
  Chosen over VBAP because it needs no triangulation and works for any speaker set, including
  device-reported odd ones and layouts without height.
- **Spread** parameter widens a source (a huge monster or a flood is not a point): the gain vector
  is blended with the all-speaker distribution by `spread`.
- **Smoothing:** gains ramp per sample (one-pole, about 20 ms) toward the target so that moving
  sources never zipper or click. Targets are recomputed once per block (about 5–10 ms), not per sample.
- **Stereo (2.0)** uses exactly the existing law `cos/sin(pan * pi/2)` with `pan` derived from
  azimuth, so that positioned stereo is a drop-in replacement for today's random pans (section 6).
- **Bass management:** a 2nd-order low-pass send, per source `lfeSend` in [0,1]; the speakers get a
  matching high-pass-free blend (no crossover phase tricks in the first version; the send only adds).
  When a layout has no LFE the send is folded into L/R (and C) at the same level.

### 3.4 Emitters and events (the data the synth needs)

Added to `AmbientParams` (still plain data):

```cpp
struct Emitter {            // a continuous source, e.g. a siren
  float level = 0;          // 0..1, as the nearness fields are today
  float x = 0;              // -1.5..1.5, ±1 are the screen edges, beyond = off-screen
  float y = 0;              // 0 ground .. 1 top of the screen
  float depth = 0;          // 0 near .. 1 far (from z)
  float spread = 0;         // 0 point .. 1 fills the room
};
struct SoundEvent {         // a one-off, e.g. a rifle shot
  EventKind kind;
  float x, y, depth, strength;
};
```

- `World::emitter(key)` replaces `nearness(key)` for the directional sources. It returns the
  strongest entity's level *and* position. `nearness()` stays for existing callers and tests until
  the last sound is converted.
- Events: `World` pushes `SoundEvent`s into a small queue as the simulation produces them (shot
  fired, impact, nibble, gulp, squeal, lightning flash, monster stamp). The counters remain during
  the migration so old synthesis tests keep passing, and are removed in the last step.
- Position sources in the world (entity centre, from `Entity::x`, `width()`, `y`, `height()`):
  the nearest entity per key for emitters; for events the entity that caused it (helicopter for
  shots, fish for nibbles, kraken for gulps, the struck column for impacts, the flash's x for
  lightning).

### 3.5 Crossing the thread boundary

The audio thread reads a snapshot written by the main loop. Replace the per-field atomics with:

- a **seqlock-style double buffer** for the parameters (`AmbientParams` incl. emitters is
  trivially copyable; writer bumps a sequence counter before and after, reader retries on odd or
  changed counter; no locks, no allocation); and
- a **single-producer single-consumer ring buffer** for `SoundEvent`s (fixed capacity 64; overflow
  drops the oldest, never blocks).

This removes the four hand-copied field lists (closing F-19) and makes a new field work in every
backend automatically. The offline path used by tests passes the same struct straight to
`Ambience::render()`.

### 3.6 Synth restructuring

- `kMaxChannels = 16`; `Ambience(int sampleRate, uint64_t seed, Layout layout)` and
  `render(p, out, frames)` writes `layout.channels()` interleaved floats. Layout can change at run
  time (route change): only the spatializer matrix and the output stage change, no synth state is
  reset, so there is no click and no restart.
- **Beds (rev. 2026-11-13): one independent chain per full-range speaker is the default.** Today's
  bed code is already a `for (ch < 2)` loop in which every ear has its own noise draw and filter
  state, so it generalizes mechanically: the arrays `[2]` become `[kMaxChannels]` and the loop runs
  to the layout's channel count. Independent noise per speaker is the best possible decorrelation;
  shared groups make neighbouring speakers coherent, which collapses the envelopment into phantom
  images and combs when the listener moves. The earlier plan of four shared groups (front, side,
  rear, height) is demoted to a **fallback tier** used only if the benchmark says a layout is too
  expensive. Details:
  - Each speaker gets a **bed profile** per scene: a level trim and a tone tilt by position (rear a
    little darker and quieter, heights lighter, rain and wind heavier overhead), and a slow
    per-speaker gust modulation with different phases so that wind and storms drift around the room.
  - Channels 0 and 1 keep the existing `rng_` draw order. Channels 2 and up draw noise from their own
    `Rng` instances (seeded from the seed and the channel index), so the stereo draw order, the
    golden hashes and the event timing of the shared `rng_` stay untouched and each layout is still
    deterministic per seed.
  - LFE is not a noise chain: it is a low-passed sum of the rumble parts of the bed plus the object
    sends.
  - Tonal beds (moon pad, hum) must not call `sin` per channel: compute the sine and cosine of each
    partial once per sample and derive the per-channel phase offsets by rotation
    (`sin(a+o) = sin a cos o + cos a sin o`), a few multiplies per extra channel.
  - Voices and one-shot sounds are already computed once and added to both ears (dual mono), so they
    cost nothing extra per channel; they go through the spatializer.
  - Cost: bed work grows about 7.5x from 2.0 to 9.1.6 (15 full-range channels). Whether that fits is
    a measurement (sections 2.4 and 8), not an assumption.
- **Point voices** are generated mono, then `Spatializer` places them (azimuth, elevation,
  distance, spread, lfeSend). Voice structs that already carry `pan` (`Bubble`, `Song`, `Pass`,
  `Honk`, `Shot`, eagle and woodpecker members) get a full position instead.
- Everything stays allocation-free and lock-free in the audio callback.
- Sample-rate invariance (`noiseScale_`) and seed determinism are preserved. The RNG draw order for
  the stereo case is unchanged through phases 1–2 (verified by the golden hashes in section 9).

## 4. The sounds, converted

`bed` = decorrelated groups; `point` = mono voice placed by the spatializer; `LFE` = bass send
level; `pos` = where the position comes from. Elevation `h`: ground 0, sky 1.

### 4.1 Forest

| Sound | Today | Becomes | Notes |
|---|---|---|---|
| Wind | bed, per-ear chains | bed, all groups; heights at 0.6 | Gusts drift slowly around the room (a slow azimuth LFO on the group gains), not static. |
| Rain | bed | bed with strong height share (rain overhead) | Surround-heavy; fronts lower so dialogue-free scene stays open. |
| Thunder (crack, snap) | same in both ears | point at the strike's x, `h` 0.9 | Needs the flash's x as an event; the delay for the speed of sound stays. Crack at the position, the tail spread=1. |
| Thunder rumble, distant storm | per-ear noise | bed + LFE 0.8 | From the side the storm approaches (`gloom` emitter). |
| Crickets | mono, 0.9 right | 3 point voices at fixed ground positions in the surrounds | Night only; low level. |
| Flood (menace) | per-ear noise, swell | wide emitter following the wave front's x, spread 0.8, LFE 0.6 | `flood` front position and depth from the World. |

### 4.2 Aquarium

| Sound | Today | Becomes | Notes |
|---|---|---|---|
| Hum, muffled flow | bed | bed (front and side groups, no heights) | |
| Growl of something huge | per-ear deep noise | bed + LFE 1.0 | Slowly drifting position. |
| Bubbles | point, random pan | point at a rising position: x from a bubble source fish or random along the floor, `h` rises during the blip | Event per blip. |
| Nibbles | bubble blip, random pan | point at the food's x | Event with the eating fish's x,y. |
| Whale song | point, fixed pan per phrase | emitter following the whale's x; large spread; LFE 0.4 | `whale` emitter; azimuth follows motion, so it sweeps through the surrounds when it passes. |
| Kraken gulp | mono | point at the kraken's mouth, LFE 0.3 | Event. |
| Submarine leak (menace) | per-ear hiss | moving emitter at the submarine, spread 0.3; trail as a wide hiss behind it (spread grows with the trail length) | |

### 4.3 Desert

| Sound | Today | Becomes | Notes |
|---|---|---|---|
| Dry wind, sand hiss | bed | bed incl. heights | Sandstorm raises height and rear share. |
| Tornado roar | per-ear noise with a 2.8 Hz spin | emitter at the tornado's x, **rotating** azimuth around it (the spin becomes spatial), LFE 0.7 | |
| Sandworm growl and bellow | growl in the bed, bellow mono | emitter at the worm's x, ground level, LFE 1.0 | Bellow spread 0.5. |
| Crunch (chewing) | per-ear noise | point at the crush position, LFE 0.4 | Event. |
| Helicopter chop, whine (menace) | per-ear chop | emitter at the helicopter, `h` 0.8, spread 0.4; its Doppler-like pitch shift from azimuth velocity | Moves over the room; sweeps height channels as it passes overhead. |
| Rifle shots | point, random pan | point at the helicopter's door position | Event at the helicopter's x,y. |
| Crickets | mono | surround point voices | |

### 4.4 Moon

| Sound | Today | Becomes | Notes |
|---|---|---|---|
| Pad (drone) | oscillators with ear phase offset | bed, per-group phase offsets, no directional cue | Heights at 0.3. |
| Meteorite impact | thud, rumble, patter | point at the impact's x for the thud; rumble LFE 1.0, spread 1; debris patter as small events scattered around the impact x | Event with x. |
| Alien zap | mono warble | emitter at the alien, `h` according to its flight | |
| Big meteor (menace) | rumble, `level^2` | emitter **approaching from afar**: starts high and far (small, dark), grows louder and brighter, lowering in elevation, then the impact with spread 1.0, LFE 1.0 over the full width | Position follows the asteroid sprite. |

### 4.5 Mountains

| Sound | Today | Becomes | Notes |
|---|---|---|---|
| Thin wind, whistle, ice hiss | bed | bed incl. heights | The whistle gets a slow azimuth drift. |
| Stream | bed | fixed emitter at the stream's x, spread 0.3, ground | |
| Avalanche (rumble, roar, crackle, thump) | bed noise, mono thump | emitter at the slide's x that **moves down the slope** with its `y`, LFE 1.0, spread 0.7 | `avalancheLevel` plus position. |
| Squeals | mono | point at the buried animal | Event. |
| Eagle, woodpecker | point, random pan | point at the eagle sprite's x, high; woodpecker at a tree's x | Eagle position becomes an emitter-like event. |
| Yeti stamps and roar (menace) | mono | emitter at the yeti's x; starts **behind and high** (rear height channels) as it comes over the ridge, then moves to the front as it descends; stamps LFE 0.8 | Position from `behindRidge` state plus x. |

### 4.6 City

| Sound | Today | Becomes | Notes |
|---|---|---|---|
| Traffic, tyre hiss | bed | bed (front and sides weighted, no heights) | |
| Cars passing, engine | point, random pan, pitch sinks | point following the real vehicle sprite's x, pitch shift by azimuth velocity | Emitter per nearest vehicle or an event on pass. |
| Horns | point, random pan | point at the honking vehicle | Event. |
| Siren (ambulance) and wail (fire engine) | mono | emitters at their sprites, Doppler pitch, distance darkening | Come in from the side before visible. |
| Train | rumble, klack, horn | emitter that crosses; klack and horn from its position; LFE 0.5 rumble | Enters from the side/rear. |
| Plane | jet | emitter overhead, `h` 0.9, slow cross | |
| Fire | roar, crackle | emitter at the burning building(s) x, spread by `fireLevel`; crackle as point sparks near | Troubled columns from the World. |
| Monster stamps and roars (menace) | mono, shared roar | two emitters (lizard, winged), each at its sprite; stamps LFE 1.0; roars spread 0.4 | The winged one's screech stays silent once it is destroyed (preserved). |
| Flame streams | per-ear noise | two emitters at the mouths, moving with the fight; body into LFE 0.4 | Blue and violet streams as left and right of the fight. |

## 5. Backends

### 5.1 Layout negotiation (shared logic in `src/audio` or `src/app`)

`LayoutChoice`: `auto | 2.0 | 5.1 | 7.1 | 5.1.2 | 5.1.4 | 7.1.2 | 7.1.4 | 9.1.4 | 9.1.6`. In `auto`,
render to the richest layout the device reports, never more channels than the device offers.
Device-reported unnamed maps use the nearest-position mapping from section 3.2. If the chosen layout
fails to open, step down (9.1.6 → 7.1.4 → 7.1 → 5.1 → 2.0) and finally fall back to stereo. A route
change at run time (HDMI plugged, AirPods connected) re-evaluates `auto` and swaps the layout in the
synth without restarting it.

### 5.2 miniaudio (terminal builds: macOS, Linux, Windows)

- Auto layout (rev. 2026-11-13, measured in the phase-0 spike, finding F-24): open with
  `cfg.playback.channels = 0` so that miniaudio uses the device's native channel count and standard
  map, then read the real count and map from `device.playback` after init and build the synth layout
  from them. Measured on PulseAudio/PipeWire with a stereo device: 0 gives 2 channels, FL FR.
  **[verify on CoreAudio, WASAPI, ALSA and on a surround or HDMI device]**
- **Never infer the device from a successful open.** The same stereo device also opened with 6, 8, 12
  and 16 channels because the sound server remaps; success says nothing about speakers. The device's
  own count comes from `channels = 0` (or `ma_context_get_device_info` native data formats), and
  that is the ceiling for `auto`. A user override above it is allowed (the server or driver
  downmixes) but must be labelled as such in the menu text.
- Opening with an explicit count or map: `ma_format_f32`, 48 kHz, `cfg.playback.pChannelMap` set from
  the layout. An explicit 7.1.4 map with named height positions was accepted and reported back
  unchanged (PulseAudio). **[verify per backend whether a height map is honoured]** Let miniaudio's
  own conversion handle any mismatch rather than failing.
- Keep the stderr silencing around `ma_device_init` (ALSA complaints).
- macOS: the device's configured speaker layout decides (Audio MIDI Setup); an HDMI device often
  exposes up to 8 channels as PCM. **[verify]** No device is reconfigured by the app.
- Windows: WASAPI shared mode uses the format from the Sound control panel; exclusive mode is not
  used. **[verify]**
- Linux: PipeWire/PulseAudio expose a channel map; plain ALSA `default` is often stereo and needs a
  surround device. This is documented, not worked around. **[verify]**
- Channel order mapping table: canonical → miniaudio `MA_CHANNEL_*` positions (rev. 2026-11-13,
  checked against `third_party/miniaudio/miniaudio.h`). miniaudio names `FRONT_LEFT/RIGHT/CENTER`,
  `LFE`, `SIDE_LEFT/RIGHT`, `BACK_LEFT/RIGHT`, `FRONT_LEFT_CENTER/RIGHT_CENTER`, `TOP_FRONT_LEFT/RIGHT`,
  `TOP_BACK_LEFT/RIGHT`, a single `TOP_CENTER`, and `AUX_0` to `AUX_31`. It has **no wide pair** (Lw,
  Rw; `FRONT_LEFT_CENTER` sits between L and C, not outside L) and **no top-middle pair** (Ltm, Rtm).
  Its default maps above 8 channels are 7.1 plus `AUX` channels, **without** height positions, so every
  height layout needs an explicit `pChannelMap`. Rules: Lrs/Rrs → `BACK_*`, Ls/Rs → `SIDE_*`,
  Ltf/Rtf → `TOP_FRONT_*`, Ltr/Rtr → `TOP_BACK_*`; Lw/Rw and Ltm/Rtm → `AUX_n` in the canonical
  order. The app's own layout table stays authoritative (it knows the true angles); the device map is
  used only to route each canonical channel to a device index. How a driver routes `AUX` channels to
  speakers is **[verify]** and may differ per backend, which is why 9.1.4 and 9.1.6 are expected to
  work only where the device or an aggregate exposes that many channels.
- Expectation to set in the README: plain PCM over HDMI is usually capped at 8 channels, so 9.1.6
  and 7.1.4 reach a receiver as 16/12 channels only through multichannel interfaces; over normal
  HDMI the app renders to what the device reports (typically 7.1). **[verify]**

### 5.3 Apple (tvOS app, and iOS where the file is shared)

- `AVAudioSourceNode` with a non-interleaved float32 `AVAudioFormat` that carries an
  `AVAudioChannelLayout` for the chosen layout (`kAudioChannelLayoutTag_*`, including the Atmos
  speaker layouts for 5.1.2, 5.1.4, 7.1.2, 7.1.4 and 9.1.6) **[verify the tags exist on the
  deployment target]**; the render block de-interleaves the synth's output per channel.
- Session: `setSupportsMultichannelContent:YES` **[verify]**, `maximumOutputNumberOfChannels` and
  `setPreferredOutputNumberOfChannels:` to ask for the route's maximum **[verify]**; observe
  `AVAudioSessionRouteChangeNotification` and the spatial-audio availability notification **[verify
  names]** to re-run layout negotiation; read the current route's output channel count.
- AirPods: the system spatializer (and head tracking) applies to the multichannel stream; the app
  does not render HRTFs. Whether the operating system spatializes a stream from an ambient app, and
  whether `MixWithOthers` prevents it, is **the** question for the spike. **[verify]**
- HDMI: tvOS's own Audio Format setting (Auto, Dolby Digital, Dolby Atmos or PCM) decides what the
  receiver gets; whether PCM from `AVAudioEngine` is passed through as multichannel PCM or re-encoded,
  and up to how many channels, must be measured. **[verify]**
- Output node and mixer formats: the main mixer must be connected to the output with the route's
  format; connecting the source with a multichannel format to `mainMixerNode` may or may not keep the
  channels; the spike tests both a direct and a mixer path. **[verify]**
- Stereo/AirPods fallback: if the OS does not spatialize a stereo stream from the app, the spatial
  rendering for headphones must come from the app (optional phase 9, binaural).
- macOS terminal build: stays on miniaudio (zero new libraries on the terminal path). An
  AVAudioEngine path for macOS would be a separate decision (section 11).

### 5.4 Controls and settings

- CLI: `--audio-layout <auto|2.0|5.1|7.1|5.1.2|5.1.4|7.1.2|7.1.4|9.1.4|9.1.6>`; default `auto`.
- Menu entry (core menu, same style as the existing entries) cycling the choices; the active
  layout and what was found (e.g. `auto: 7.1`) shown in the menu text.
- tvOS: a remote-friendly cycle through the same menu.
- If the project stores settings between runs, the layout is stored with them; if not, it is
  per-session (check `src/app/session.cpp` first).

## 6. Compatibility and stereo

- 2.0 is the same engine with a two-speaker layout. During phases 1 and 2 the stereo output is
  **bit-identical** to today (golden hashes); the refactor moves no sound.
- In phase 3 the stereo output changes **deliberately** where a sound gets a real position (a shot
  comes from the helicopter, a siren from its vehicle): stereo users get the improvement too.
  Each conversion commit re-baselines its scene's golden hash and says so in the message.
- Mono-compatibility and stereo fold-down: the spatializer is energy-preserving, so a 7.1 fold-down
  to stereo is close in level to the stereo render. No separate fold-down code is planned; the
  device's own down-mix path is not relied on because the app renders to the device layout.

## 7. Phases and commits

Each commit builds warning-free, passes its tests and updates the docs it affects (rule 8). Sizes:
S under half a day, M a day or two, L several days. Order is dependency order.

### Phase 0 – Spike on hardware (M, needs the owner's devices) — not a commit to the app

Goal: replace every **[verify]** above with a measured answer before building on it. Throwaway test
code, not committed (or kept under `scripts/` if useful and original).

- tvOS device spike: `AVAudioSourceNode` with 2.0, 5.1, 7.1, 7.1.4 and 9.1.6 layouts; check what
  the receiver reports and how many channels arrive over HDMI; each tvOS Audio Format setting.
- AirPods (Pro/Max, with head tracking): does the spatializer engage for a multichannel stream from
  the app, with and without `MixWithOthers`? Does head tracking work?
- macOS terminal build: miniaudio device query and a multichannel open against a surround interface
  or an HDMI display; AirPods behaviour.
- Output: a short findings entry (new F-number) with the verified facts, and corrections to this plan.
- **Gate:** if AirPods do not spatialize an app stream at all, phase 9 (own binaural) moves up.

### Phase 1 – Core: layouts and spatializer (M)

- `src/core/layout.h/.cpp`: canonical channel table, named layouts, nearest-position mapping.
- `src/core/spatializer.h/.cpp`: DBAP, spread, per-sample smoothing, LFE bass send.
- Tests: speaker at the exact source position gets the largest gain; constant power (sum of squares
  1 ± 1e-5 for any direction and layout); a far-left source lights only left-side speakers and no
  right-side ones; a high source lights height channels; layouts without height fold the height
  share into the base layer; layouts without LFE fold the send into L/R/C; smoothing never steps
  more than a bound per sample; no NaN for degenerate positions.
- Docs: `architecture.md` Sound section (new subsection), `conventions.md` (sound-design
  conventions for multichannel).

### Phase 2 – Core: N-channel synth, stereo unchanged (L)

- `Ambience` takes a layout, renders `channels()` interleaved. Per-ear arrays become `kMaxChannels`
  or group-based. All sounds still produce their current stereo material, which is routed so that
  with the 2.0 layout the output is bit-identical.
- Golden hash test per scene (1 s, fixed seed, with every second-catastrophe and weather state
  forced) recorded **before** the refactor in a preparatory commit, checked after.
- New tests: any layout renders finite samples in [-1, 1]; silence is silent at every layout; the
  same seed yields the same output per layout; changing the layout mid-render does not click
  (bounded sample-to-sample jump) and does not reset sound state; sample-rate invariance holds.
- Benchmark script (`scripts/bench-audio.sh`, not part of `ctest`): CPU time per second of audio
  per scene and layout.
- Docs: `architecture.md`, `README.md` (once the CLI option exists, phase 5).

### Phase 3 – Core: per-speaker beds (M) (rev. 2026-11-13)

- Beds run one independent chain per full-range speaker (section 3.6) with a bed profile per scene.
  Stereo output stays identical (channels 0 and 1 are today's two chains on today's `rng_`).
- Tests: cross-correlation between any two speakers' bed output below a bound; each layout gets
  energy on every non-LFE speaker in a bed-only scene; height speakers silent for scenes that define
  no height share (aquarium bed); LFE receives only low-passed material.
- CPU budget check against the benchmark (section 8). **Gate:** if a layout is over budget, first
  profile (voices vs. chains), then cheapen the chains (shared noise source with per-speaker
  filtering), and only then fall back to shared groups for the rear and height speakers.

### Phase 4 – Thread boundary: snapshot and event ring (M)

- Seqlock snapshot of `AmbientParams`; SPSC ring buffer for `SoundEvent`s; both backends use them;
  the per-field atomics and hand copies are removed (F-19 closed).
- Tests: concurrent writer/reader stress test (no torn reads detected over a large number of
  iterations, run under ThreadSanitizer locally); ring overflow drops oldest; empty ring is a
  no-op; a field added to `AmbientParams` shows up in the render without touching a backend
  (a test that fills every field with a distinct value and compares).

### Phase 5 – World: emitters and events (L)

- `Emitter`, `SoundEvent` in the params; `World::emitter(key)` and event pushes at the places that
  count today; `ambientFor()` fills them. Counters and `nearness()` stay.
- Tests: emitter x/y/depth follow a moving entity; off-screen entities map beyond ±1; the nearest
  entity wins; events carry the causing entity's position; headless determinism per seed.
- Docs: `architecture.md` (sound parameters), `README.md` sound text if it changes.

### Phase 6 – Per-scene conversions (one commit per scene, six commits; L in total)

Order: city (most sources, tests the model), mountains, desert, aquarium, forest, moon. Each commit
converts the scene's sounds per section 4, re-baselines that scene's golden hash, and adds tests:

- an emitter at the left edge makes left speakers louder than right ones and the reverse;
- an off-screen emitter is heard from the side before the sprite appears;
- height-sourced sounds light the height channels in layouts that have them;
- LFE receives the rumble of the heavy sources and nothing from the quiet scenes;
- existing behaviours survive (the winged monster's sound is silent once destroyed; fire-stream
  sound has no periodic modulation; no sound from a scene that is not active).
- Docs: `README.md` sound description, `CHANGELOG.md` line per scene, `architecture.md`.

### Phase 7 – Backends (M each, can be developed in parallel after phase 4)

- 7a miniaudio: device query, layout negotiation with step-down, channel-map mapping, fallbacks,
  stderr silencing kept; manual open test against whatever hardware exists.
- 7b Apple: multichannel source node and layout, session configuration, route-change handling,
  per-channel de-interleave. Built for tvOS (and checked that the macOS build, which uses miniaudio,
  is untouched). Simulator build and the spike's findings; on-device verification by the owner.
- Tests: the negotiation logic (choice + device report → layout, including step-down and the
  unnamed-map mapping) is pure and lives in the core or app layer so it is unit-tested without a device.

### Phase 8 – Controls, docs and release notes (S)

- `--audio-layout`, menu entry, tvOS menu entry; `Session` tests for the key/menu, as for the `X` key.
- `README.md` (options, sound section, the HDMI PCM channel caveat), `CHANGELOG.md`, `docs/ai/*`,
  `CLAUDE.md` if a command or rule changes, new finding(s) for the open points below.

### Phase 9 – Optional: own binaural speaker virtualizer for headphones (L, only if the spike needs it)

- Reframed (rev. 2026-11-13): it is a final stage that virtualizes the **rendered layout** (each
  speaker channel becomes a virtual source at its canonical angle), not a second renderer for
  objects, so beds and objects keep one code path.
- Used only when the output is a stereo headphone route and the OS does not spatialize the app's
  stream. A head model (interaural time and level differences, a head-shadow filter, simple
  pinna-free elevation cue); no HRTF data files, so no licence issues, no new libraries.
- Quality expectations are modest compared with Apple's spatializer; ship as an opt-in (`--audio-layout headphones`).
- Not started unless phase 0 shows a need.

## 8. Performance budget

- The audio callback stays allocation-free, lock-free, branch-light.
- Budget (to be confirmed by the benchmark, not measured yet): the 7.1.4 render stays below 3 % of
  one performance core at 48 kHz on the development Mac, and the tvOS target (an Apple TV class
  chip) below 10 %. A layout that cannot meet it renders fewer bed groups before dropping channels.
- Levers if over budget: halve the update rate of spatial gains (per block already); share one
  noise source across groups with different filtering; skip groups whose mix gain is below -60 dB;
  lower the point-voice cap (voices already limited by the fixed-size arrays).
- The benchmark runs per scene and layout in a script, outside `ctest` (timing is machine
  dependent), results recorded in the phase commit messages.
- Reference point (rev. 2026-11-13, section 2.4): today's stereo synth costs 3 % (forest) to 9 %
  (city) of one core on a 2013 i3 with `-O2`. If the bed chains were all of that, 9.1.6 would be up
  to about 7x in the bed part, which is why the split between chains and voices must be profiled
  first, and why the budget above is stated for the development Mac and Apple TV, not for old
  x86 laptops. A minimum-hardware figure for the terminal build should be set once measured.

## 9. Testing strategy

All tests stay in the single dependency-free `tests/test_main.cpp`, headless, deterministic.

| Area | Tests |
|---|---|
| Layout | channel counts, names, ordering; nearest-position mapping of unnamed maps |
| Spatializer | constant power, directionality, height routing, fold-down, smoothing bounds, NaN safety |
| Synth | finite and bounded at every layout; per-seed determinism per layout; silence; mid-render layout change; sample-rate invariance; bed decorrelation |
| Stereo regression | golden hashes per scene recorded before phase 2; compared through phases 2–5; re-baselined per scene in phase 6 |
| Thread boundary | snapshot stress test, ring semantics, completeness of the field copy |
| World | emitters follow entities; off-screen mapping; events carry positions |
| Scenes | per-scene directional and LFE assertions listed in phase 6 |
| Negotiation | choice × device report → layout and step-down; route-change re-evaluation |
| Session | `--audio-layout` parsing, menu entry cycling |

What the tests cannot cover: whether it sounds right. Direction and level are asserted; timbre,
envelope shape, spaciousness, the feel of a sweep through the room and everything about real
speakers, receivers and AirPods needs ears (section 10).

## 10. What cannot be verified here, and who verifies it

| Item | Why | Needed |
|---|---|---|
| How any of it sounds | no sound has been heard by the assistant in this project | the owner, by ear, scene by scene |
| tvOS HDMI multichannel behaviour, Audio Format settings | no device | Apple TV with an AVR/soundbar |
| AirPods spatial and head tracking from an app stream | no device | AirPods Pro/Max with an Apple TV and a Mac |
| `AVAudioSession`, channel-layout tag and notification names | written from memory | check against the SDK in phase 0/7b |
| miniaudio multichannel on macOS HDMI, Windows, Linux | no multi-channel device, no Windows or Linux runs | a surround setup; CI covers builds only |
| 9.1.6 and 7.1.4 as 16/12 PCM channels | PCM over HDMI is usually capped at 8 | a multichannel interface or an Atmos-capable OS path |
| CPU on Apple TV | no device | the owner, with the benchmark |

## 11. Decisions for the owner

1. **Sound field.** Diorama (sources in the front arc, beds wrap around, off-screen things come
   from the sides and rear) or fully immersive (everything placed around the listener)?
   Recommendation: diorama.
2. **Stereo.** Bit-identical through the refactor and deliberately re-baselined per scene when
   positions arrive (this plan), or keep a frozen legacy stereo path forever? Recommendation:
   this plan; a frozen path doubles the synth code.
3. **Release order.** (Reworded 2026-11-13: v1.3.0 is pinned and this work is not in it.) Which
   release carries it, and does tvOS come with it or after? tvOS is where HDMI and AirPods matter,
   and it carries the unverified risk. The spike (phase 0) can run first and independently.
4. **macOS AirPods and Atmos path.** If the spike shows the terminal build gets only stereo on
   AirPods, is an AVAudioEngine backend for macOS (system frameworks, but a new path next to
   miniaudio) acceptable, or does the terminal build stay miniaudio-only with phase 9?
5. **Hardware** for phase 0: which Apple TV, receiver or soundbar, display, and AirPods.
6. **Settings persistence.** Is the audio layout remembered between runs?
7. **Bed density (rev. 2026-11-13).** Independent chain per speaker everywhere (best immersion,
   most CPU) or per-speaker only up to a limit with shared groups beyond it? Recommendation:
   per speaker everywhere, fall back per layout only if the benchmark requires it.
8. **Minimum hardware for the terminal build.** What CPU must 9.1.6 run on? Older laptops may
   need `auto` to cap the layout by measured headroom.

## 12. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| The OS does not spatialize a stream from this app (mix-with-others, ambient category) | AirPods feature impossible via the OS | Phase 0 gate; the phase 9 binaural renderer; drop `MixWithOthers` if that is the cause and the owner accepts interrupting other audio |
| PCM over HDMI capped at 8 channels | no height channels over plain HDMI | State it in the docs; step-down logic; heights only on devices that expose them |
| CPU cost on tvOS | stutter | Bed groups (not per-speaker chains), benchmark gate, voice caps |
| Spatial positions feel jumpy | worse than stereo | Smoothing, block-rate targets, slew limits on emitter movement, tuning by ear |
| Seed-determinism tests are fragile (F-21) | churn in golden hashes | Record golden hashes before the refactor; re-baseline only in scene commits, stating why |
| Two backends drift apart (F-19) | a field silent on one platform | Phase 4 removes the hand copies |
| Large change set | review load | Small, per-step commits, each building and testing on its own |

## 13. Definition of done

- Every layout from 2.0 to 9.1.6 renders finite, bounded, deterministic samples; tests pass on
  macOS (the platform actually run), builds are warning-free.
- Stereo behaviour unchanged through phases 2–5, deliberately improved scene by scene in phase 6.
- Positions follow entities in every scene; heavy sounds reach the LFE; sky sounds reach the heights.
- miniaudio and Apple backends negotiate the layout, step down on failure, and follow route changes.
- `--audio-layout`, menu entry, README, CHANGELOG, `docs/ai/*`, code comments (Doxygen on every new
  declaration) all updated; findings list the open, unverified items (section 10).
- The owner has listened to every scene in stereo, 5.1 or 7.1, and on AirPods, and what was heard
  is recorded in findings, or the gaps are stated.
