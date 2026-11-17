# Architecture

State as of v1.2.0 plus the unreleased work of 2026-11-09 (see CHANGELOG `[Unreleased]`). Line counts are approximate and exist to show where the weight is.

## Layers

```
src/core   (no I/O, deterministic per seed; library diorascii_core)
  Rng, Frame/Cell, Sprite/Props, SceneDef, AssetPack, World + Entity + behaviors, shading.h (light and
  haze applied to colors, shared by renderers), Menu,
  Ambience (sound synth)            + generated embedded_assets.cpp and version.h
        |  Frame = grid of {codepoint, fg, bg, attrs}          ^  Action (input)
        v                                                       |
src/term   VT/ANSI encoder (vt.cpp) + Terminal (terminal_posix.cpp | terminal_win.cpp)
src/audio  device wrapper: AVSampleBufferAudioRenderer on macOS, tvOS and iOS (tagged PCM, the system places the channels and spatializes), miniaudio on Windows and Linux (own thread); parameters via a seqlock; opens the device with the negotiated speaker layout
src/app    Session (scene, menu, style, time of day, host clock hour as reported by the frontend, pause, toasts,
           info line, sound request; no I/O) + localclock.h (the host's local hour; the only clock access,
           used by the frontends) + main.cpp: CLI and the terminal main loop around a Session
```

Planned (v1.2.0): a native Apple shell for tvOS (and later iOS if it becomes a supported target),
written in Objective-C++ and built entirely by CMake (Xcode generator, asset catalog for the
layered icon and Top Shelf image; no hand-maintained Xcode project). It reuses `src/core` and
the session logic unchanged: it renders the `Frame` with a bundled monospace font and translates
Siri Remote input into `Action`s. **No SDL3 and no desktop window**: Diorascii is a terminal app,
and every desktop OS has a terminal. Decided 2026-11-08, replacing the earlier SDL3 plan.

CMake targets: `diorascii_core`, `diorascii_app`, `diorascii_term`, `diorascii_miniaudio` (vendored single header,
built without project warnings), `diorascii_audio`, executable `diorascii`, test executable
`diorascii_tests`.

## Frame loop (src/app/main.cpp)

`Session` (src/app/session.*, library `diorascii_app`) holds all interactive state, including the overarching mode (`cycle_`: user or cycle, cycle being the default (`Config::cycle`, `--user` to start in user mode); in cycle mode `stepCycle` fires the random action every 10-90 s and moves to the next scene, with a new seed, when `sceneDone()`: `World::daysElapsed()` >= 1 since the scene was marked, a rare event started and ended (a late one is waited for, the scene stays on for days) (`raresStarted`, `rareActive`), and an action run; a manual Time/Interact/Rare action, or the menu entry of one, calls `setCycle(false)` and then does its job; the cycle's own scene switch crossfades (`fadePending_`: `flush` moves the old world to `outgoing_`, which keeps stepping for `kFadeSeconds`; `render` dissolves its picture into the new one with `crossfade()` from core/frame.h; `ambient()` fades the old scene's sound out over the first half and the new one's in over the second, so the audio backends need no change; a resize or a manual rebuild ends the fade) (stepping the clock on from Cycle); entering the mode sets the clock to Cycle, and the user's time mode is restored when leaving through Mode), and is driven by
any frontend: `handle(Action)`, `resize`, `step(dt)`, `render(Frame&)`, plus `soundWanted()` /
`soundUnavailable()` / `ambient()` for the frontend's sound device and `takeRepaint()`. The
terminal `main.cpp` adds only pacing, the terminal and the audio device. Fixed 20 fps (`--fps`), world steps at `dt = 1/fps`; input polled in 5 ms slices; the menu is
drawn into the same `Frame` as the scene. It shows the layout the sound is rendered to now ("Audio output: 7.1 on 8 channels", "... (mixed down)", "... (spatialized)", or "Audio output: sound off"), the version and the copyright as small print (`Menu::setFooter`, set by `Session::updateFooter`). Frames are encoded by the pure function
`encodeFrame(prev, cur, ColorMode)` which emits only changed cells. `--dump N` runs N steps
headless and prints the scene as text (also used by CI as a smoke test on Windows).

## Input

`Action` (core/input.h) is the only input vocabulary: Quit, Pause, Interact (the scene's primary
action, key `f`), Info, Redraw, Up/Down/Left/Right, Select, Back, Menu, Style, Time, Rare (`x`),
Audio (`a`). Backends map keys to actions; the core never sees key codes. The ASCII menu is
driven by the same actions, which is what makes it usable with a Siri Remote later.

## Rendering

- `Style`: `full` (gradient backdrop, truecolor), `terminal` (no backdrop, 16 ANSI colors that
  follow the user's theme), `mono` (the terminal's own colors only).
- `ColorMode`: TrueColor, Ansi256, Ansi16, Console. Console is the Linux text console
  (`TERM=linux`, also detected inside tmux via the client's terminal type, or `--console`):
  16 colors but only 8 usable backgrounds, snapped to the dark ones and dimmed; **no shade
  glyphs** (`░▒▓`) because console fonts draw them as `#`. With no `--style`, Console mode starts in
  the `terminal` style (`defaultStyleFor` in `vt.cpp`), and that style keeps Console mode instead of
  Ansi16 there, because the console may not support the bright backgrounds (100-107).
- Plain black is never used for text (it vanishes on dark backgrounds).
- Sprite key `backing` darkens the backdrop behind a sprite's glyph cells (full style only) so
  pale animals read on bright backdrops; `contrast: yes` picks dark-on-pale / light-after-dark.

## Data formats (reference: root README)

- `*.sprite`: `key: value` header, then `--- art` frames and an optional `--- mask`. Mask letters
  are colors (lower case = darker), digits 1-9 pick from per-entity palettes (warm/cool/earth),
  `_` is an opaque space. A mask color on blank art is a solid block and requires `solid: yes`
  (enforced by a lint test).
- `*.scene`: headers (colors, layout, `action:`, `day-night`, `gravity`, `event-*`, `rare-*`,
  mountain keys...) and sections `--- population`, `--- decor`, `--- events`, `--- rare`.
- Sprites are shared between scenes; spawn density lives in the scene, not the sprite.
- Assets are embedded by `cmake/embed.cmake`; `AssetPack` later additions replace earlier ones,
  which is how `--assets DIR` overrides work. `AssetPack` reports scene references to missing
  sprites.

## Simulation (src/core/world.cpp, ~1,500 lines)

`World` owns `Entity` objects (sprite + position + tags + behavior list). A sprite names its
behaviors; `world.cpp` maps names to functions in one table:

`swim fly bob rise sink emit_bubbles seek_food hunt walk ballistic meteorite wander hop rain
chase twister emit_dust catastrophe ledge soar avalanche blaze drive engine leak flood poach maul clash asteroid`

Catastrophe sprites (behavior `catastrophe`) choose how they strike with sprite headers (README has
the reference): `strike-mode` `centre` (all victims at once at `strike-at`: thunderstorm), `sweep`
(each victim as the attacker's middle passes it: the moon alien) or `devour` (each victim in its way
as its front reaches it: the sandworm). With `strike-reach` a devouring attacker first lashes a
tongue or tentacle at a victim (`World::lash`, `Tongue`, `renderTongues`; entities carry a unique
`id` so a tongue finds its victim again): `strike-aim: ahead` (walking things ahead of its front,
the worm's tongue) or `around` (anything alive around it, from along its underside: the kraken),
`strike-lashes` at once, `lash-color`, `mouth-row` as the row it starts from. The victim is crushed
when the tip arrives. `World::crushCount()` and `squealCount()` feed the sound.

Cross-cutting systems, all in `World`:

| System | Notes |
|---|---|
| Layout | surface/seabed/ground rows, river, `lanes` for depth on a bank; the mountains scene has terrain (`ridgeRow`, `zoneAt`, `Zone` bands with wobbling limits) |
| Light / time | `Light{day,sun,glow,stars,haze,hazeColor}`, phase 0..1, `TimeMode` (cycle, dawn, day, dusk, night, realtime: the frontend reports the host's local hour through `Session::setClockHour`, see `app/localclock.h`, since the core reads no clock; `Realtime` is last in the `t`/menu order and never the default); sun/moon arcs (8x5 sprites; a sky lower than the body cuts it off at the top); `airless` scenes (the moon) cycle too, but the sky stays black with stars and only the ground brightens and darkens a little; the sun (`sun_corona`, bigger than the earth) and the earth are its arc bodies. The haze color is dimmed to the time of day (a night sandstorm stays dark) and the star level follows the overcast (clouds and dust hide the stars) |
| City | `city: yes` scenes (`src/core/city.cpp`): procedural skyline (`Building`, three layers, rolled to the terminal height between `skyline:` shares (0.18..0.70) with facade and roof kinds, never reaching the top, so always lower than the mountains), a main street of 4 ground rows, elevated viaduct at `railY_` (habitat `rail`, only if the sky is at least 16 rows; drawn as deck, beam, pillars, masts and wire), street rows (sidewalk, road with center line, sidewalk) and lamps drawn by `renderCity`, which `World::render` calls between the far entities (z >= 25: sky, clouds, planes) and the near ones. Lanes come from `feet-rows` and `traffic`; the F key is `action: summon ambulance`. Side streets (`Avenue`, `placeAvenues()`, key `avenues`, placed first so that the front row of buildings (`buildCity`) and `spawnDecor` keep out of their mouths) are painted by `renderStreet` (`paintAvenue`: a broad road 13 columns wide that narrows a little and ends after 4-7 rows, hazier with distance) over the skyline and under the viaduct, with a zebra crossing at the mouth. Decor lines marked `anchor yes` store where they stood (`decorAnchor_`), and later lines aligned `from-left|from-right|from-center` follow it (the desert's pond `oasis|oasis_small|oasis_big` with its palms and reeds); decor in the far distance (z >= 30, the pyramids) does not take room from things in front of it. Decor lines take `jitter`, `chance`, `per-cols` and `a|b` alternatives (`World::placeDecor`: up to 24 tries for a free column, outside the side streets; ground decor taller than 3 rows keeps clear of other decor; only the station stays when no column is free); the station (`tag: station`, random column) and the trees therefore differ per seed. `placeCrossings()` (after `populate`) fills `crossings_`: one at the station's foot, others about every 29 columns, none in a side street. Trains use the `train` behavior (`World::trainStep`, `Entity::stopPhase`: at speed, braking from 3 s of travel away, standing 9 s level with the station, pulling out; not `driveState`, which the avenue cars use); standing, it releases 2-5 `commuter` sprites (rail habitat, behavior `commute`, `World::commuteStep`) that walk to the station column and are replaced by a pedestrian (`onDeath` cleared) on the street. The wire is at `railY_ - 7`, above the 6-row station. The train's sound follows `World::trainSound()` (`motion` = speed / cruising speed, `brake` while `stopPhase` 1, `hiss` in the first 1.6 s of phase 2; `AmbientParams::trainMotion/trainBrake/trainHiss`): the klack clock and rumble scale with the smoothed motion, so a standing train is silent apart from the squeal and the hiss. `glimpses_` are further, non-drivable streets painted between the middle and front rows, so that they only show through gaps in the front row. Vehicles use the `drive` behavior (`World::driveStep`, entity fields `driveState`: main road, turning in, up the avenue, down it, turning out; vehicles with five frames show side, rear three-quarter, rear, front three-quarter and front views as they turn): `turns:` is the chance to take the next avenue; `stepCity` sends cars down the avenues. Traffic follows `World::trafficLevel()` (about 0.06 at night, 0.26 by day, 1 at 8:00, 0.95 at 17:30): sprites with `rush: yes` that are (re)spawned join with that chance and otherwise retry after 2-7 s through `restock_`, so the street fills and empties with a lag; the avenue cars use it too. Sprite mirroring puts runs of 3+ letters back in reading order. Vehicles and planes are line art with `backing: 0.55` (the earlier solid-block versions read as blobs). Render order in a city: far entities (z >= 25), skyline + side streets (`renderCityBack`), side-street cars (z 24), viaduct + street + lamps (`renderStreet`), the rest. The fire engine uses the `engine` behavior (`World::engineStep`): it drives to the burning building (the origin of the fire) with tag `wail` and large blinking light bars (like the ambulance's, 12 cells, red/blue swapping), parks (`driveState` 1), sprays `water` effects (solid blue blocks; they shorten the burn) and leaves, tag cleared so the wail stops, as soon as no building is burning any more; from its arrival until the last building is back the site is cordoned (`cordon_`, `cordonHalfWidth()`: at least 18 columns each side, wider once the fire has spread, `fitCordon()`) with `barrier` sprites across road and sidewalks: grounded movers whose body would reach into it freeze at its edge (`pauseLeft`). A fire is decided at its start (`kFireFails` 0.3, `forceFireOutcome` for tests): either the engine beats it (burn 24 s, sped up by the spray, then `Scaffold` 8 s, renovated) or it gets out of hand: `spreadLeft_` 3-5 more buildings ignite one after another (nearest first, every 2.5-4 s after 7-10 s) and every building goes `Burning` -> `Ruin` (collapses over 3.5 s, smouldering rubble `rubbleRows`, 32 s) -> `Scaffold` (`ruined`: grows back from the rubble over 16 s) -> new looks. The `x` catastrophe is the invisible sprite `blaze` (behavior `blaze` -> `World::blazeStep`): a front skyscraper gets `State::Burning` (24 s: flames and glowing windows drawn by `renderSkyline`, smoke effects, a fire engine) and then `State::Scaffold` (8 s: scaffolding and a crane), after which `rollBuilding` gives it new looks; `fireLevel()` feeds the sound |
| Sleeping | ~70% of most species sleep at night, each with its own bedtime; sleepers do not hunt, eat or get hunted |
| Events | one scheduled visitor at a time per scene (`--- events`) |
| Rare catastrophes | separate slow timer, one at a time; strike at `strike-at` progress (`strike-mode`: `centre` all at once, `sweep` each victim as the attacker's middle passes it, `devour` as its front reaches it, preceded by a tongue or tentacles (`World::lash`, `Tongue`, drawn in `renderTongues`; `strike-aim: around` for the kraken) when `strike-reach` is set); wipes `life` or named sprites; dead things restock gradually; screen flash; `x` summons one |
| Actions | `Drop` (food), `Impact` (meteor + crater that fills with dust), `Weather` (rain, sandstorm, tornado that flings animals), snowstorm; selected by `action:` in the scene |
| Hunting | `hunt`/`chase`: predators tagged `hunt` eat `prey`; lion and bear make one kill each |

Collision is simple tag/position checks, not a physics engine. Randomness comes from an
in-house xoshiro `Rng`, so a seed reproduces a world exactly.

## Sound (src/core/ambience.*, src/audio)

`ambientFor(World)` reduces world state to `AmbientParams` (kind, night, rain, flashes, storm
proximity, nibbles, whale/tornado nearness, impacts, avalanche level, sandworm nearness, crush count (desert: chewing, aquarium: gulping), avalanche squeal count, siren (ambulance, quick two-tone), wail (fire engine, slow sweep; the engine drops its tag when it leaves, so it drives off silent), train/plane nearness and fire level for the city, volume). The audio backends (`audio.cpp`, `audio_apple.mm`) hand the whole struct to the audio thread through a `Seqlock<AmbientParams>` (`core/seqlock.h`), so a new field needs no change in them (it must stay trivially copyable). `Ambience::render`
fills an interleaved float buffer of `layout().channels()` channels per frame (stereo unless an `Ambience` is built with, or `setLayout` is given, another `Layout`; the backends still use stereo); everything is smoothed and scenes crossfade, so nothing clicks. The
audio thread reads one consistent copy of the parameters (a seqlock: the writer never waits, the reader retries a few times and otherwise keeps its last copy), never a mix of two updates; `EventRing<T, N>` in the same header is the lock-free, drop-oldest queue for the events of phase 5 (tested, not yet used). Playback is miniaudio 0.11.21 (Unlicense/MIT-0),
playback only, decoding and engine compiled out, stderr silenced while the device opens (ALSA
noise). On by default (owner, 2026-11-08); `--mute`, key `a` or the menu switch it; silent when no device exists (announced only for `--audio` or the `a` key).

**Layouts and the spatializer** (2026-11-13, phase 1 of `spatial-audio-plan.md`; **not yet used by the synth or the backends**).
`src/core/layout.*` holds `Layout`: an ordered set of `Speaker`s (canonical `Channel`, azimuth in degrees with + to the right, elevation, weight)
for the nine named layouts 2.0 to 9.1.6 (`LayoutId`), at most `kMaxChannels` = 16, in canonical channel order (L R C LFE Lw Rw Ls Rs Lrs Rrs Ltf Rtf Ltm Rtm Ltr Rtr).
The 5.1 family stands its sides at 110 degrees, everything else at 90. `Layout::fromDevice` builds a layout from what a backend reports
(`DeviceChannel`: position, LFE flag, known or not): each channel keeps the device's angle (the panner needs only positions) and takes the identity of the
nearest canonical channel (`nearestChannel`); unknown channels and a second LFE are `Channel::None`, which stays silent.
`src/core/spatializer.*` has `Spatializer::gains`, the panning law, and `SpatialVoice`, a placed voice with smoothing. Stereo keeps the synth's own
law `cos/sin(pan * pi/2)` with `pan = 0.5 + 0.5 * sin(azimuth)`; every other layout uses distance-based amplitude panning (DBAP) on the unit sphere with
`spatial::kRolloff` and `spatial::kBlur`, then blends with an even distribution by `spread` and renormalizes, so the gains of the directional speakers
always have sum of squares 1 and LFE and silent channels get 0. A layout without height puts a high source on the nearest speakers; nothing special is
needed. `SpatialVoice` glides every gain to its target with a one-pole filter of `spatial::kSmoothingSeconds` (20 ms; `setTarget` once per block,
`process` per sample, `snap` for a voice that starts in place) and adds a bass send: the sample, low-passed by two one-pole stages at
`spatial::kLfeCutoffHz`, times `Source::lfeSend`, goes to the LFE channel, or, if the layout has none, is folded at constant power into the speakers
within 45 degrees of the front (all of them if none is). Everything is allocation-free and lock-free. All constants are tuned by measurement only; they
have not been heard.

**The synth in a layout** (2026-11-13, phase 2 of `spatial-audio-plan.md`). `Ambience(rate, seed, layout)` and `setLayout` take a `Layout`. The sounds
themselves are still the stereo pair they have always been: `render` computes `stereo[2]` per frame as before and a routing stage
(`route()`, `feed_`, `routeGain_`) plays it on the layout's front pair (`Layout::stereoPair`: the L and R channels, else the leftmost and rightmost directional speakers;
one speaker gets the mean) and leaves every other channel at exactly 0, until phases 3 to 6 give beds and voices their places. With the stereo layout the output is
**bit-identical** to the synth before the refactor (checked on this machine with `--dump-golden` and `cmp`, 12 cases of 2 s). A layout change does not reset anything: the
routing gains glide with `spatial::kSmoothingSeconds`, so a channel that keeps its role plays on exactly as before and a channel with a new role fades in (one-pole curve
`1 - (1 - k)^(n + 1)`, tested sample by sample). The random generator `rng_` is untouched by the layout, so each layout is deterministic per seed. `scripts/bench-audio.sh`
(`tests/bench_audio.cpp`, not in ctest) prints the CPU share per scene and layout.

**Emitters and sound events** (2026-11-14, phase 5). `core/soundstage.h` defines the data: `Emitter {level, x, y, depth, spread}` (a continuous source), `EmitterKey` (twelve creatures found by the tag or sprite name that `World::nearness` has always used)
and `SoundEvent {kind, x, y, depth, strength}` with `EventKind` (Flash, Nibble, Impact, Crush, Squeal, Shot). **The stage is the screen as a window**: x runs -1 (left edge) to 1 (right edge) and up to +-1.5 off-screen (`kStageReach`; a creature is heard from the
side before it appears, as `nearness` already did), y 0 on the ground (`seabedY_`, the first ground row) to 1 at the top of the screen, depth `z / 32` (larger z is further away). `World::emitter(key)` returns the creature `nearness` reports with its position (`level` is exactly
`nearness`; no creature gives all zeros), and `AmbientParams::emitters[]` carries all twelve (`ambientFor` fills them); the plain level fields (`whale`, `siren`, ...) stay until the scenes are converted (phase 6). The world pushes a `SoundEvent` wherever its counters tick
(`flash` at the strike column, `noteFoodEaten` at the fish's mouth, `impactAt`/`bigImpactAt` at the column (strength 1 or 2), `noteCrushed` at the victim or the devourer's front (strength: how many), the squeal at the buried animal, the shot at the muzzle) in a queue of 64 that
`World::takeEvents` empties; `Session::takeSoundEvents` hands them to the frontend, which calls it every frame (also while muted) and pushes them with `AudioOutput::pushEvent` into an `EventRing<SoundEvent, 64>` that the audio thread drains before each render into
`Ambience::event` (at most 32 kept until the render call ends). **No sound uses the positions yet**: the counters still trigger the sounds, and phase 6 switches each scene to the emitters and events. Not covered: the sources that have a level but no creature to find
(fire, avalanche, flood, toxin, meteor, flames) have no emitter; they get a position source when their scene is converted.

**Placed sounds: the city** (2026-11-14, phase 6, first scene). A *place* (`Ambience::Place`: a `SpatialVoice`, its `Source`, an `on` flag; ids in `PlaceId`) puts one sound where it is. Voices are mono and go through the place into `room_` (by output
channel), which `flushRoom` maps onto the bed chains (`speakerOf_`) and the bass channel; **a noise source that has a place** (the train's rumble and clatter, the fire's roar, the jet, the streams of fire, the stamps' rumble) keeps its independent noise
per chain and is weighted by the place's gain towards that chain's speaker (`weight()`), so it stays wide and decorrelated but is where the source is. Placed sounds bypass the bed trims and the drift (a jet overhead reaches the height speakers although the
city's bed has no overhead share) and are scaled by the scene's presence (`voiceOut_`; the catastrophe's by `menaceBus_`). **Level reference**: the old mono sounds were added to both ears at gain 1, which is 1/sqrt(2) in the stereo panning law, so they are scaled by `kStereoRef` = sqrt(2) (and the
noise weights likewise): a source straight ahead is exactly as loud in stereo as it has always been (the sum of the squared weights is 2 in any layout). **Direction**: `stageSource` turns an emitter's x into an azimuth (the screen edges are +-45 degrees, +-1.5 is 110) and y into an
elevation (0 to 60 degrees); `aimCity` aims the places once per block from `AmbientParams::emitters` (a place keeps its last position while its emitter is gone and the sound dies away; a place that was off starts in position). The cars and horns are still the synth's
own random events, with the old random pan turned into an azimuth (so the stereo pan is exactly the old one); each car has its place. Bass: `lfeSend` goes to the LFE channel, but **a layout without one gets no send** (`forLayout`), because the full-range speakers already play
the whole sound (the spatializer's fold-in would double it). The monsters' stamps and the lizard's roar have a full send, the train 0.5. Two more emitter keys came with it: `Fire` (the burning building with the strongest fire) and `Flames` (the middle of the streams).
The city's overhead share of the bed is now 0. **Not done in the city**: Doppler pitch, distance darkening, and emitters per real vehicle sprite (the plan's "point following the real vehicle").

**Placed sounds: the mountains** (2026-11-14, phase 6, second scene; same mechanism as the city). Placed: the avalanche (`EmitterKey::Avalanche`, new: the slab that is about to let go, then its head, so it **moves down the slope**; its rumble and roar are noise weighted per chain, its
crackle and thump one mono voice with a full bass send and spread 0.7), the yeti (`Beast`: stamps with bass send 0.8, bellow, rumble weighted per chain), each buried animal's squeal (a place per squeal voice, started where the latest `Squeal` event said: **the frontend must push events before it sets the parameters**,
so that a sound the counters start finds its place), the eagle (the old random pan as a direction, 35 degrees up) and the woodpecker (5 degrees), and the whistle (a place that drifts slowly round the room, +-70 degrees, 0.03 Hz). The stream is a bed on the ground chains only. Stereo
is unchanged (the golden values hold without re-baselining; the whistle's drift is far below the tolerance). **Not done in the mountains**: the yeti coming over the ridge from behind and high (the world has no such state), the stream's own x (it is a bed on the ground, not a point), the eagle following its sprite
(the cry is the synth's own random event, only placed).
**Placed sounds: the desert** (2026-11-14, phase 6, third scene; same mechanism). Placed: the tornado's roar (`Tornado`: noise weighted per chain, spread 0.4; in layouts with more than two channels its azimuth turns +-30 degrees with the 2.8 Hz spin, so the spin becomes spatial; in stereo it stays the swell of the roar, which keeps the stereo sound), the sandworm (`Worm`: the growl as weighted noise, the bellow as one mono voice, spread 0.5, bass send 1.0), the chewing (a place per `Crunch` slot, started where the latest `Crush` event said: weighted noise for the bites, the grumble as a voice with bass send 0.4), the helicopter (`Chopper`, spread 0.4: the rotor's chop as weighted noise on the catastrophe bus, the whine as a voice) and each rifle shot (a place per shot slot, at the muzzle of the latest `Shot` event; a caller that sends no events keeps the old random pan as a direction). Wind and sand stay beds; the crickets stay voices of the stereo pair (the forest's crickets are the same code, so both are converted together).
Stereo is unchanged except for the shots: their old pan law was linear (gains 1.2-p and 0.2+p), the spatializer's is constant power, which moves the shot's window by up to 1.1 % (the two desert golden cases were re-baselined for that window only, everything else holds within the tolerance). A silent layer is not computed past the stereo pair (whose generator must advance as it always did), which makes the calm desert cheaper in surround. `SpatialVoice` now snaps a gain that has all but arrived at 0 and flushes its bass filter when it is silent, so that no placed sound sinks into the denormal numbers. **Not done in the desert**: Doppler shift of the helicopter, the sandstorm's extra rear and height share, the crickets as surround voices, the worm's position while it is under the sand (it is the sprite's).
**Placed sounds: the aquarium, and the water all around** (2026-11-14, phase 6, fourth scene). Placed: the whale's phrase (`Whale` emitter: spread 0.6, bass send 0.4; it starts where the whale is and follows it while it sings; a caller without a whale on the stage keeps the old random pan), the kraken's growl (`Gloom`, weighted noise per chain, spread 0.5), the kraken's gulps (one place at the `Crush` event, bass send 0.3), the submarine's hiss (new `EmitterKey::Submarine`, spread 0.35) and its gas bubbles, each nibble's blip (at the `Nibble` event, the fish's mouth) and every bubble (a place per bubble slot). **Immersion** (not in the original plan): the aquarium's bed now has a height share (0.5), so the water also plays overhead, and its drift is deeper (0.25), so it wanders round the room; in layouts with more than two channels (`room3d`) a bubble cluster is a **string that rises** (`Ambience::bubbleString`: it starts anywhere between -150 and 150 degrees, so also behind the listener, and each bubble is 10-20 degrees higher than the one before, from `fxRng_`, never from the stereo sound's generator) and every speaker has its **own tiny bubbles** (`micro_`, about one a second and a half per speaker, 1.5-5 kHz, drawn from `microRng_`) so that no speaker is a dead spot in the water. In a stereo pair none of this exists: the stereo sound is unchanged to the golden values (no re-baselining), because bubbles keep their old pan law when they have no position (`panSource`), the whale's and the kraken's emitters default to the centre with the old levels, and the extras are off. **Not done in the aquarium**: the submarine's trail as a widening hiss behind it, a rising position during a single blip (a blip is 40-100 ms; the rise is across a string), depth darkening of far bubbles.
**Placed sounds: the forest** (2026-11-14, phase 6, fifth scene). Placed: a lightning strike's crack and snap (a place at the latest `Flash` event: the bolt's column, from the top of the sky; the snap is one mono sound, which it always was in both ears; only the latest strike has the place, an earlier one still ringing glides to it), the far claps and the rumble of the storm on its way (`Gloom` emitter, the thundercloud, spread 0.7; `thunderLevels` now splits strikes from far claps; the place `kCloud` is on while a far clap or the storm rumbles), the flood's roar (new `EmitterKey::Flood`: `World::floodFront()`, on the ground, spread 0.8, bass send 0.6) and, in a room, the crickets (three fixed voices at -112, 105 and 158 degrees; the desert's two at -125 and 100). The strikes' own rumble stays a bed that rolls all round (spread 1). The rain is overhead: `rainShape_` plays it 1.5 times in the height speakers and 0.8 in the front ones past the stereo pair. In a stereo pair the crickets stay in both ears (`room3d` is off), so stereo is unchanged to the golden values (no re-baselining: with every emitter at the centre the placed sounds equal what they replaced).
**A level slip found on the way**: the placed sounds' reference `kStereoRef` (sqrt(2)) kept the stereo level but made them 1.41 times as loud as the old sound in a single-speaker layout, which plays the mean of the stereo pair; `stereoRef_` is now 1 there (a test checks it). **Two tests that took the forest as "the unconverted scene" were narrowed**: the front pair of a room, the single speaker and a layout change are compared with the forest's wind and rain by day with nothing placed (`placedFreeParams`), because a placed sound leaves the front pair by design. **Not done in the forest**: the position of each strike's rumble, a place per strike (only the latest has one), a flood roar that widens with the water, the gusts' azimuth drift of the wind (the bed's drift does it).
**Placed sounds: the moon** (2026-11-14, phase 6, sixth and last scene). Placed: a meteorite's thud (a place at the latest `Impact` event, bass send 1.0, spread 0.2; the big meteor's impact, strength 2, has spread 1.0: the full width), the debris that patters down (one resonating voice at the impact; in a room each click lands somewhere round it, +-35 degrees and 0-25 degrees up, from `fxRng_`), the alien's warble (new `EmitterKey::Alien`: it starts where the alien is and follows it while it sounds) and the big meteor's rumble (new `EmitterKey::Meteor`: the asteroid sprite's place, `World::emitter`; the level is how far it has fallen; it comes in high and gets lower, and its spread grows from 0.2 to 1.0 with the level squared, so it fills the room as it lands). The impact's rumble and the drone stay beds. Stereo is unchanged to the golden values (no re-baselining). **Not done on the moon**: the meteor brightening as it nears, a place per meteorite (only the latest has one), the alien's own height through its flight (the emitter gives it, only the start and the glide are placed), the plan's drone height share 0.3 (it is 0.4 since phase 3).
**With the moon all six scenes are converted** (phase 6 of the plan is complete).
**Layout negotiation and the miniaudio backend** (2026-11-14, phase 7a of `spatial-audio-plan.md`; the Apple backend is phase 7b). `src/core/negotiate.*` is pure logic with no device in it: `LayoutChoice` (`auto` or a layout name), `DeviceReport` (the channel count and, if the backend knows them, the positions as `DevicePosition`, the vocabulary all backends share) and `negotiate()`, which gives a `Negotiation` (the layout the synth renders, the device's channel count, whether the device must be opened again with our own channel map, whether the layout is **downmixed** because it has more channels than the device). Rules: with a map, `auto` renders to the device's own layout (a named layout if the map is exactly that layout's channels in order, else a custom one in the device's order and with the device's angles); with a count only, the richest of 9.1.6, 7.1.4, 7.1, 5.1, 2.0 that fits; one channel is one speaker; a named choice is rendered as asked, even above what the device has. `lowerLayout()` is the step-down (9.1.6 and 9.1.4 to 7.1.4, 7.1.4 and 7.1.2 to 7.1, 5.1.4 and 5.1.2 to 5.1, 7.1 to 5.1, 5.1 to 2.0), `portsForLayout()` the canonical channels as device positions (the wide and top-middle pairs, which miniaudio has no names for, take `AUX_0...` in order), `padChannels()` the silence for the channels of a device that has more than the synth's sixteen. **Apple platforms** (2026-11-17, F-26): `src/audio/audio_apple.mm` is the one backend for macOS, tvOS and iOS, on `AVSampleBufferAudioRenderer` and an `AVSampleBufferRenderSynchronizer`. The synth's layout is the format of interleaved float PCM sample buffers with the layout's Core Audio tag (`appleLayoutOf`: MPEG tags for 2.0, 5.1 and 7.1, Atmos tags for the layouts with heights; 9.1.4 has none and steps down); a serial queue renders 100 ms chunks and keeps 0.2 s queued ahead of the synchronizer's time (so a sound that an action starts plays 0.2 to 0.3 s late), restarting at the playback position after a stall. The system does the rest, as for any player's movie: it places each channel on the device channel with the same label, mixes a layout that is richer than the device down, and spatializes multichannel content on AirPods and other spatial-capable headphones ("3D audio", the user's Control Center switch, head tracking). An `AVAudioEngine` output does not get that (3D audio stays unavailable with a tagged 7.1.4 source and the mixer connected to the output), which is why the backend is not built on the engine. The route: on macOS the engine is only asked what the output looks like (channel count, labels of its channel layout, through `positionForCoreAudioLabel()`: LeftSurround 5 and 6 are the sides, RearSurround 33 and 34 the rears, VerticalHeight 13 and 15 and LeftTopRear/RightTopRear 52 and 54 the heights, LeftWide/RightWide 35 and 36 and LeftTopMiddle/RightTopMiddle 49 and 51 the wide and top-middle pairs, discrete labels Aux, unknown and unused None) into an `exact` `DeviceReport`, and whether the default output device is Bluetooth (`kAudioDevicePropertyTransportType`: the SDK has no query for "this route spatializes" on macOS) sets `DeviceReport::spatializes`; on tvOS and iOS the audio session knows a count and `spatialAudioEnabled` of the route's port. `negotiate()` picks the layout as for any backend: `auto` on a one- or two-channel route that spatializes is 7.1.4, otherwise `auto` ignores the channels without a position and, if the others are exactly the channels of a named layout in any order, renders to that layout (`Negotiation::routed`, `route[]`: the placement the system makes); a named choice with no place on the device **steps down** through `lowerLayout` (9.1.6 on a 7.1.4 map plays as 7.1.4, the menu shows `9.1.6 > 7.1.4`), except on a device of one or two channels, where the system mixes it down (`--audio-layout 2.0` stays a native stereo pair even on AirPods). A change of the default output device (macOS), of the route or of the user's spatial audio setting (tvOS, iOS), or the system flushing the renderer is a notification that sets a flag; `update()` starts the output again. The menu shows a spatialized route as `7.1.4 (mixed down)`. Gaps in a device's map come from the channel map that Audio MIDI Setup defaults to for a channel count above what the layout needs; they are handled (the channels without a position are skipped) but not a design goal. The miniaudio backend below is Windows and Linux only (miniaudio reads no layout on macOS: it assumes one, and its output gets no 3D audio). `AudioOutput::start(choice)` opens the device with 0 channels first (that is what it reports; a successful open with a count says nothing, see F-24), negotiates, and for a named choice opens it again with the layout's channel count and map, stepping down until it opens (and, last, with whatever the device has); `layout()`, `deviceChannels()` and `downmixed()` tell what happened. A `rerouted` notification sets a flag and the main loop's `AudioOutput::update()` reopens the device with the same choice (never from the notification itself: miniaudio forbids it); the synth's layout is set while the device is stopped. **Measured on this machine** (PulseAudio/PipeWire, stereo): `auto` gives 2.0 with 2 channels; every explicit layout up to 9.1.6 opens (the server remaps) and is labelled downmixed. **Not verified**: any device with more than two real speakers, CoreAudio, WASAPI, ALSA, route changes, the more-than-sixteen-channels path on a device (the copy is unit-tested), and whether a driver routes `AUX` channels to speakers.
**Sound modes** (2026-11-14, phase 8; reworked 2026-11-17). The core menu has a *Sound* choice, off / stereo / immersive (default immersive); there is no speaker entry. `Session::soundMode()` is the mode; `layoutChoice()` is 2.0 in the stereo mode and otherwise the immersive choice, which is auto unless `Session::Config::audioLayout` (`--audio-layout`) names a layout (a 2.0 there starts the sound in the stereo mode). Switching between stereo and immersive while the sound is on sets a flag that `takeLayoutChange()` returns once: the frontend (terminal `syncSound`, tvOS `syncSound`) stops its output and opens it again with `layoutChoice()`, then calls `soundLayout(name, channels, downmixed, spatialized)`, which puts what was found into the menu's small print (`Negotiation::spatialized` / `AudioOutput::spatialized()`: a multichannel layout on a route of a pair whose system spatializer makes the sound; false with miniaudio). The `a` key (`Action::Audio`) goes round off, stereo, immersive and toasts the mode. Per session; nothing is stored between runs. `Menu::optionText(id)` reads the selected option's text.
**The Apple backend** (2026-11-14, phase 7b; reworked 2026-11-17: see the Apple platforms paragraph above, on `AVSampleBufferAudioRenderer` since then; the first version was on `AVAudioEngine`).

**The forest chain is gated** (2026-11-13, finding F-25). The forest's own sounds (wind, rain, thunder, crickets) are made only while `mix_[Forest]` is above 0.001, as the other scenes are
(`forestOn` in `render`). The gust and wander state, `thunderLevels` and `roll_` keep running because other scenes and the draw order depend on them, and a silent forest still takes the same
draws from `rng_` that it took when it ran (`noise(rng_)` twice per channel and sample, once more while a clap rings): without them every other scene's noise and events would change. A new
forest-only sound added to the per-channel loop must go inside `if (forestOn)` and keep that draw count in the `else`.

**Per-speaker beds** (2026-11-14, phase 3). `route()` gives every directional speaker of the layout a **bed chain**: chains 0 and 1 are the stereo pair (the front L/R, or the extremes) and
the rest follow in output order (`chains_` of them, at most `kMaxChannels`); the LFE channel plays `kBass`. The per-ear filter state of every scene is `[kMaxChannels]` and the sample functions run
`for ch < chains_`, skipping chains that are not `live(kind, ch)`. **Bed** (continuous noise through filters: wind, rain, hum, flow, traffic, roar, rumble, stream, ice, the moon drone) exists in
every chain; **voices** (discrete events and tones: bubbles, whale song, gulps, cries, knocks, avalanche clicks and thump, squeals, horns, sirens, engines, train clatter and horn, crackling, thunder's snap,
crickets, chewing, the bellow, the moon's thud, debris and warble, the whine and the roars of the catastrophes) are still computed once and play on chains 0 and 1 only, until phases 5 and 6 place them. A new sound
must be classed as one or the other and, if it is a bed, keep its noise per chain. Chains 0 and 1 draw noise from the shared `rng_` exactly as the stereo synth always did (so the stereo output, the golden values and the
event timing are bit-identical, and the front pair is bit-identical in every layout); chains 2 and up each have their own `Rng` (`bedRng_`, seeded from the seed and the chain), so a speaker's bed does not depend on how many
other speakers there are. The **bed profile** is `trim_[kind][chain]` (chains 2 and up: 0.85 behind 100 degrees, 0.7 at the centre, the scene's height share overhead: forest 0.6, aquarium 0.5 (since the aquarium conversion), desert 0.7, moon 0.4,
mountains 0.7, city 0.3) times a slow **drift** (`updateDrift`, every 32 samples, a different very slow sine per chain and scene, depth 0.1 to 0.3), so that wind and storms wander around the room; the stereo pair has
neither. The moon's chord has no noise: chain `c` plays it with a phase offset (0, 0.9, then golden-angle steps) derived from one sine and cosine per partial by rotation, which costs a few multiplies per chain; tones can only differ in
phase between speakers, so the moon's speakers stay correlated (they differ, not decorrelate). The **bass channel** is the sum of all chains (scaled by 1/sqrt(chains), they are independent) through two low-pass
stages at `spatial::kLfeCutoffHz`, clamped to +-1. All trims, depths, offsets and the bass level are constants chosen by measurement, **not by ear**; no layout has been heard. The levels of a scene are not compensated
for the number of speakers: every speaker plays as loud as a stereo channel does, so a 9.1.6 room is louder overall than stereo (unverified whether that is wanted).

**Second catastrophes** (2026-11-10). Every scene lists a second sprite in
`--- rare` with weight 0.25 against 1, so it comes one time in five (`spawnRare` is a weighted pick). `RarePick`
(`Weighted`, `Common`, `Other`) lets `triggerRare` choose: `x` and the timer are weighted, `X` (`Action::RareOther`), the
menu entry *Other rare event* and `--rare-other` take the lowest weight; tests take `Common` so that fixed seeds keep the
first catastrophe. The second catastrophes run as `isRare` entities, so the one-at-a-time rule and the rare timer work as
before; they are `World` members wrapped by thin behaviors in the table of `world.cpp`.
The code is in `src/core/disasters.cpp`:
- `leakStep` (aquarium): the grey submarine drives across the sea and leaves a `Toxin::Puff` about every column at its
  stern; `puffRadius` grows each puff at 6 cells/s up to `reach` (the height of the sea, rows counting 2.2 times); the cloud
  is the union of the puffs. `stepToxin` kills `life` inside any puff, delays everything in `restock_` and starts the
  linger/fade timers when the submarine has left; `renderToxin` tints the water by the deepest puff per cell.
- `floodStep` (forest): an invisible `flood` sprite starts `flood_`, a wall of water at 17 cells/s from a random side, depth
  `floodDepth(x)` full within 2 columns of the front. `renderFlood` draws the first 14 columns behind the front sparse
  (foam glyphs only, no fill) and fills them in to solid. `stepFlood` rips off whatever has water at its feet (`life` or
  `grounded`, so flying birds are spared), spawns `log` effects, sets `mud_` per column (fades over 110 s, drawn in
  `renderBackground`) and holds back replanting for 25-75 s after the water has gone.
- `poachStep` (desert): the helicopter chases the nearest living thing, hovers above it and calls `lash` with
  `lash-shot: yes`, which makes a `Tongue` a quick straight `.`-tracer, counted by `shotCount()`; restocking waits while it
  hunts; it leaves when nothing is left for 3 s or after 75 s.
- `maulStep` (mountains): the yeti climbs for 5 s over the highest ridge from behind: `Entity::behindRidge` makes `blit`
  hide the cells inside the mountain outline (`ridge_`) while its feet rise, then it is drawn in front. The mountain is a
  face with the meadow at the bottom, so its feet go to the row of its prey (never above the skyline) and it walks to the
  nearest grounded life (distance counts rows three times) and kills whatever comes under its body (`claim`); it never
  lashes, so it crosses the scene after its prey; flyers are out of its reach.
- `clashStep` (city): the lizard `lizard_titan` carries the show and calls `winged_titan` in; phases: arriving, fighting
  (26 s, blows in turns: blue breath from the lizard, violet from the winged one, or both; each is a harmless `Tongue` with
  `fire` set, drawn as a broad stream of `*#%&@` that holds and dies away), the winged one falls, the lizard walks off and
  `e.x = -1000` parks the entity while `stepBuildings` (factored out of `blazeStep`) rebuilds the ruins; `ruinBuilding` puts
  a building into the existing `Ruin` state, so rendering and rebuilding are shared with the fire. The monsters are 60x33
  cells; the fight needs at least 110x28. The devastation is ordered by distance from `meetX` (`Clash::order`): from the
  start of the fight the front moves out at 4.5 columns/s up to 45% of the width, each building catches fire (`Burning`,
  `Building::hold`), burns 4.5 s and is `ruinBuilding`-ed; ruins stay held until the fight is over, then the rebuilding
  front starts from the middle at the same speed and releases the holds (minimum smoulder 7 s), so the middle is rebuilt
  first. Everything grounded and moving on the street (not the train, not the avenue cars) gets `Entity::fleeing` when the
  fight is called and every step while it lasts: `World::step` runs it straight off the screen at 14+ cells/s away from
  `meetX` and puts its `onDeath` into `restock_`, which the clash keeps holding back until phase 3.
- `asteroidStep` (moon): the sprite `asteroid` has 8 frames of growing size on one 28x14 canvas; over 9 s it falls from a
  random start to the middle, the frame follows the progress, `bigImpactAt` digs a crater over the whole width, wrecks
  everything grounded (also what is still arriving) through `claim`, flashes the screen, and throws dust and rock out to
  both edges.

The sprites of the monsters, the submarine and the meteor were made with a throw-away silhouette rasteriser (solid body
cells, with `/ \ | - _` outline glyphs on the border cells and `backing` so that they read against any backdrop), not by
hand.
Sound: one field, `AmbientParams::menace` (the largest of the catastrophes' levels: `toxinLevel`, `floodLevel`, `rotorLevel`, `yetiLevel`, `titanLevel`, `asteroidLevel`, as far as they exist), synthesised by `menaceSample` per scene kind and copied in both audio backends. The aquarium's is a hiss of high-passed noise with glugging bubbles. The forest's is the rush of water: low-passed noise with a hiss over it. The desert's is the chop of a rotor (noise gated at 11.5 Hz) over a whine, with a crack for every rifle shot (the counter `shots`, another field). The mountains' is a creature: thuds every 0.55 s and a roar now and then (`yetiLevel` is the `nearness` of the tag `beast`). The city's monsters step and roar in the same way, bigger and slower (tag `titan`), the winged one's screech being a second voice gated by the field `winged` (nearness of `winged_titan`; it is never chosen without it), and every stream of fire roars by the field `flames` (`World::flameLevel()`: the swell of the fire-stream tongues; noise up to 2800 Hz over a deep body, with a slow random swell and faint crackles; no periodic modulation, which sounded like flapping). The moon's is a rumble in the ground that swells as the meteor falls (`asteroidLevel`).

Event sounds are driven by counters (flashes, impacts, crushes, squeals, shots) that only count as events when they increase; the first value seen is the baseline.

One `AmbientKind` per scene (`Silent, Forest, Aquarium, Desert, Moon, Mountains, City`) with a hand-written
synthesis function each. See [findings](findings.md) F-08 about the coupling this implies.

## Art notes

Clouds come in two sizes (`cloud_*` and `cloud_*_l`, about 1.5x), listed as separate population lines at half the density each. The sun and moon sprites are 8x5; the moon scene's sun is 13x8. New sprites need to be added to the list in `embedded_assets_all_load` only if they are meant to be required.

## Versioning, build, release

- `CHANGELOG.md`'s latest released heading is the version (`project(VERSION)`). A build not exactly
  on the tagged commit reports `x.y.z-dev+<commit>`; `-DDIORASCII_VERSION=` overrides (CI/release).
- Static runtime by default (`DIORASCII_STATIC_RUNTIME`): `/MT` on MSVC; on GCC `-static-libstdc++`
  only if a configure probe links (Fedora needs `libstdc++-static`), else dynamic with a message.
  GCC gets `-Wno-psabi`. macOS builds are universal arm64+x86_64, deployment target 12.0.
- Presets: `dev` (Debug), `release`, `release-macos`. Scripts: `build.sh`, `build.ps1`,
  `package-macos.sh`, `release-notes.sh`; `packaging/` holds installer definitions.
- CI: `linux.yml`, `macos.yml`, `windows.yml` on every push/PR; `docs.yml` (Doxygen + landing page
  to `gh-pages`) on push to `main`; `release.yml` on tags `x.y.z` (no `v`): verifies the tag
  against the changelog, builds and tests three platforms, creates a **draft** release with the
  changelog section as text and publishes it only when all three passed.
- Release assets are installers (2026-11-11), each putting `diorascii` into the PATH and upgrading an
  older install in place:
  - macOS: `Diorascii-x.y.z-macOS.pkg` (pkgbuild + productbuild, identifier
    `com.ChristianAhrens.Diorascii`, payload below `/usr/local`: `bin/diorascii`, `share/doc/diorascii/`).
    CI builds it ad-hoc/unsigned (`DIORASCII_ADHOC=1`). The owner builds the notarized one locally with
    `scripts/package-macos.sh`: binary signed with "Developer ID Application", package with the separate
    "Developer ID Installer" certificate, notarized with keychain profile
    `APP_NOTARIZATION_CREDENTIALS_PROFILE`, stapled, then uploaded over the CI asset by hand
    (`gh` is not installed on the owner's machine). macOS has no package uninstaller; the README gives the commands.
  - Windows: `Diorascii-x.y.z-Windows-Setup.exe`, Inno Setup script `packaging/windows/diorascii.iss`
    (the `AppId` GUID must never change; PATH entry in the machine or, for a per-user install, user
    environment, removed on uninstall; `ChangesEnvironment=yes`). `windows.yml` installs, upgrades and
    uninstalls it silently as a test. Not code-signed. The portable `.zip` is kept.
  - Linux: `.deb` and `.rpm` from CPack (`cpack -G DEB/RPM`, not default generators because RPM needs
    rpmbuild), prefix `/usr`, plus the `.tar.gz`, built natively per architecture (`ubuntu-latest` = x64,
    `ubuntu-24.04-arm` = arm64; the matrix entry's name `Linux-x64`/`Linux-arm64` is the asset suffix,
    and `linux.yml` mirrors the matrix; the arm64 build has not run yet, the first run is the proof). **Flatpak was considered and not done:** a Flatpak
    exports its command as `<app-id>` (not `diorascii`) and sandboxes a terminal program and its audio;
    `.deb`/`.rpm` give a plain `diorascii` in the PATH. Revisit if the owner wants Flathub reach.
- Landing page `pages/index.html`: scene toggle (fish/tree/palm/mountain/star icons) and
  day/night toggle over `Resources/Diorascii_<Scene>_<Day|Night>.mp4` backgrounds; the choice is
  remembered in the browser; all seven scenes (including City and Moon) have a Day and a Night clip. Videos
  are cropped to terminal content only, 20 fps, 1920 px wide. They and the README stills (rounded, with shadow)
  are produced from raw `.mov` screen recordings by `scripts/process-screengrabs.sh` (the crop rectangle is
  measured for the owner's 2350x1930 recordings; the script refuses other sizes). The `.mov` sources are not committed.

## tvOS build notes (spike 2026-11-08)

- `-DDIORASCII_TERMINAL=OFF` is the default when `CMAKE_SYSTEM_NAME` is tvOS or iOS: the terminal
  library, the `diorascii` executable and the tests are not built there. The core and the
  `diorascii_app` library (Session) configure and build for the tvOS simulator with plain CMake:
  `cmake -G Xcode -DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_SYSROOT=appletvsimulator`, then
  `cmake --build ... --target diorascii_app -- CODE_SIGNING_ALLOWED=NO` (succeeded).
- miniaudio does not compile as plain C++ for tvOS (it pulls in Objective-C Foundation headers).
  Plan: on tvOS/iOS implement `AudioOutput` with AVAudioEngine instead (same interface, drives
  `Ambience::render`), and keep miniaudio for the desktop terminal builds. This also settles F-09 for Apple
  mobile targets.

## tvOS app (apple/tvOS, built 2026-11-08)

Objective-C++ (ARC), generated entirely by CMake (`apple/tvOS/CMakeLists.txt`, Xcode generator):
`AppDelegate.mm` (app + scene delegate; the UIScene lifecycle is mandatory on tvOS 27),
`SessionViewController.mm` (20 fps `CADisplayLink`, Siri Remote input, sound sync) and
`GridView.mm` (draws the `Frame`: one filled rect per run of equal background, one cached Core Text
glyph per cell with no string layout per frame, focusable so the remote's touches and presses arrive, system
monospaced font 22.4 pt (about 130 columns; 28 pt gave about 100), fractional cell sizes with backgrounds snapped to whole points, cell size snapped to whole points to avoid seams, grid = safe area / cell).
Audio: `src/audio/audio_apple.mm` (AVSampleBufferAudioRenderer, tagged interleaved float PCM). Info.plist is a template (`$(...)` placeholders because
CMake would blank `${...}`); the asset catalog is assembled at configure time from
`Resources/Icon/tvOS/*.brandassets` (single source of truth for the icon). `SUPPORTED_PLATFORMS="appletvos appletvsimulator"` is set globally so one project offers both devices and simulators as Xcode run destinations (with the default only devices appear and Run builds but launches nothing in a simulator). Only the app target has an Xcode scheme (`CMAKE_XCODE_GENERATE_SCHEME OFF` globally, `XCODE_GENERATE_SCHEME ON` on the app): the generator's default `ALL_BUILD` scheme has nothing to run, so Run built the app and then did nothing. The core, app and audio libraries are compiled with `-O2` even in Xcode's Debug configuration: unoptimised, the sound synthesis dominated the process (~80 % of a core in the simulator, ~15 % now). Deployment target 17.0,
bundle id `com.ChristianAhrens.Diorascii`, team via `-DDIORASCII_APPLE_TEAM`.

Remote mapping: swipe left/right = scene, up = time of day, down = sound (up/down navigate while the
menu is open), click = scene action (Select while the menu is open), Play/Pause = pause, long click
= open menu, Menu = close menu, else leave the app. The menu has no Quit entry on tvOS (`Config::quitEntry`), and for the first five launches a toast hint, "Hold the click for the menu", appears 2.5 s after the start.
Keyboards (the Simulator's, or one attached to a real Apple TV) arrive as presses whose `type` is a raw HID value (2000 + usage, e.g. 2079 right arrow) and whose `key` is set, not as Siri Remote button types; `handleKey:` maps arrows, Return, Space, Escape and the terminal program's letters (m f x c t a i p r). Debug builds log input and focus with the prefix `DIORASCII-INPUT`. Verified in the tvOS 27.2 simulator: launches, draws, animates. **Not verified:** remote input,
sound by ear, performance on a real Apple TV, the signed device build.

`scripts/package-tvos.sh <apple-id> [team-id]` (2026-11-08) builds `build/package-tvos` with CMake, archives
(the app target is the only one installed into the archive: `SKIP_INSTALL`), exports an App Store `.ipa`
with a generated `exportOptions.plist` (`app-store-connect`, automatic signing), validates and uploads with
`altool` using the keychain item `AppSpecificPW`. `CFBundleVersion` is `DIORASCII_BUILD_NUMBER` (default: commit
count). Verified: configure and an unsigned archive. With the installed profile "Diorascii tvOS distribution profile" (App Store, no devices) the script signs manually (certificate chosen by the SHA-1 found in the profile and the keychain, since two Apple Distribution certificates share a name; the profile goes to the app target only, static libraries cannot have one); archive, export and the `.ipa` (signature, embedded profile, build number) were verified locally on 2026-11-08. Automatic signing fails for the archive without a registered Apple TV ("no devices from which to generate a provisioning profile"). **Not run:** validate and upload.

App Store Connect validation (2026-11-08) required, beyond a signed build: the Info.plist key
`TVTopShelfImage.TVTopShelfPrimaryImageWide` and a **2x image for every layer of the small app icon**. Both come
from the asset catalog: `Resources/Icon/tvOS/make_rasters.sh` renders the 2x layers and the Top Shelf images
(standard 1920x720 and wide 2320x720, 1x and 2x; draft crops of the icon art) from the layer SVGs with
ImageMagick and writes the catalog `Contents.json`; actool then adds the Info.plist keys itself.

## Apple/tvOS notes gathered so far

- App ID to register: `com.ChristianAhrens.Diorascii` (explicit; select all platforms that might
  ship). No special capabilities, App Services or capability requests are needed (offline, no
  accounts). Game controller / Siri Remote input is a framework, not a capability.
- tvOS needs: layered app icon, Top Shelf image, CMake `CMAKE_SYSTEM_NAME=tvOS` with the Xcode
  generator, a monospace font bundled with the app, coarse cell grid (about 100-160 columns for
  10-foot legibility), Menu button returns to the system at the root, no screensaver possible.
- miniaudio on Apple dlopens system frameworks by default; `MA_NO_RUNTIME_LINKING` was suggested for
  App Store builds but not yet decided (finding F-09). Audio session setup is the first thing to test on tvOS.
