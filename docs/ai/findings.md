# Findings to work through with the owner

Collected 2026-11-08 while writing these docs. Nothing here has been changed in code. Status is
`open` until the owner decides; then record the outcome here and remove or move the entry to
the changelog. Severity: **H** wrong or risky now, **M** should be decided before v1.2.0, **L** tidy-up.

| ID | Sev | Topic | Status |
|---|---|---|---|
| F-01 | H | Attribution rule conflicts with the harness default | resolved: old rule stands |
| F-02 | H | README "Status" is stale | fixed, uncommitted |
| F-03 | M | GPL original is a public branch of this repo | owner will purge the branch |
| F-04 | M | Is history rewriting still allowed after 1.0.0? | resolved (guardrails) |
| F-05 | M | Quality tooling existed only in a scratch directory | resolved: not needed; new doc rule added |
| F-06 | H | Known use-after-free in a test | fixed (test bug), uncommitted |
| F-07 | M | Never-verified platforms and audio paths | todo |
| F-08 | M | Scene-specific logic baked into the "generic" core | to investigate in a dedicated, plain-language step |
| F-09 | M | miniaudio runtime linking on Apple | to investigate in a dedicated, plain-language step |
| F-10 | M | `package-macos.sh` pollutes the shared build dir; `dist/` not ignored | fixed, uncommitted |
| F-11 | M | Notarized macOS assets are a manual step | documented in README Releasing; the asset is now a .pkg (2026-11-11) |
| F-12 | L | Sound design is unheard by the assistant; lightning crack unfinished | todo |
| F-13 | L | Project memory was not carried over after the directory rename | done (see below) |
| F-14 | L | Copyright holder / vendor naming is inconsistent | todo |
| F-15 | L | No sanitizer, lint or `build.ps1` evidence in CI | todo |
| F-16 | L | Documentation drift risks | todo |
| F-18 | M | Unverified art and sound of the 2026-11-09 work | open |
| F-19 | M | Audio parameters are copied by hand in two backends | resolved 2026-11-14: one seqlock copy of the whole struct (plan phase 4) |
| F-20 | L | Real-time mode: time zones, DST, tvOS | open |
| F-21 | L | More scene logic in the generic core; fragile fixed-seed tests | open |
| F-22 | M | The City scene was never heard by the assistant, only seen in recordings; page and README now show it | open |
| F-23 | M | The second catastrophes: unseen, unheard; monster names and likeness; simplifications | open |
| F-24 | M | Spatial audio phase-0 spike: first miniaudio facts; hardware still unmeasured | open |
| F-25 | M | The forest's sound chain ran in every scene; per-speaker beds would multiply it | resolved 2026-11-13: gated (owner said yes) |
| F-26 | H | Multichannel sound: everything measured, nothing heard in a room; the Apple backend only on macOS and in the tvOS simulator | open |

## F-01 Attribution rule vs. harness default (H)
The owner's rule is no `Co-Authored-By`/"Generated with" lines. The original session stored it as
a project memory; the harness for this session still injects an instruction to add
`Co-Authored-By: Claude ... <noreply@anthropic.com>` to commits and a "Generated with" line to PRs.
This session's documentation work follows the owner's rule (nothing has been committed). **Decision needed:**
confirm the rule stands for all future work. It is encoded in `CLAUDE.md` and saved to this project's memory.

## F-02 Stale README status (H)
`README.md` "Status" (around line 414) says "Four scenes (aquarium, forest, desert, moon)" and
"Planned: a mountain scene with its own action". Mountains shipped in 1.1.0/1.1.1. The "Layout"
section does not mention `docs/` or `CLAUDE.md`. A grep finds no other "four scenes" wording in README or the page.
**Proposal:** fix Status (five scenes, sound, planned: windowed + tvOS frontends) in a `develop` commit.

## F-03 GPL branch on the public repo (M)
`origin/asciiquarium_orig_main` (tip `8bdb7d4`) exists on GitHub. It is the unmodified Perl fork,
GPL. The original session noticed it as "unexpected" and left it alone; `main` and `develop` share
no history with it. The README's provenance claims ("no code or art included") are true for the
tree, but the repository now hosts GPL code and art by Joan Stark with unclear license.
**Options:** delete the branch (keep the old fork as a separate private/archived repo);
keep it and say so in the README; leave as is. Owner's call. Also decide if the doc
should name this branch at all.

## F-04 History rewriting after 1.0.0 (M)
The standing rule ("fold fixes into originating commits, rewrite history") was explicitly scoped
"as long as we are working towards first versions". Since 1.0.0 the owner still asked for squashes
and message rewrites of **unpushed** `develop` commits, but the rule was never restated.
**Proposed reading** (encoded in guardrails): unpushed commits may be folded when asked; pushed
history on `main`/`develop` and anything tagged is never rewritten. Confirm or correct.

## F-05 Quality tooling lost (M)
The sessions used scratch scripts for: a Doxygen coverage detector/applier (`docify`), a
standard-include audit (`includes.py`), and a pty-to-image snapshot tool (`snap.py`). They were kept
outside the repo on purpose and are gone. Consequence: "no undocumented declarations" and "no
missing includes" are rules with no enforcement; new mountains code was never rechecked after the
1.0.0-era audits (the sessions only claim the tests pass).
**Options:** Doxygen `WARN_IF_UNDOCUMENTED` + `WARN_AS_ERROR` in the docs workflow (cheap; Doxygen
already runs there); an include-audit script under `scripts/`; or accept manual review.

## F-06 Known use-after-free in a test (H)
In the mountains session, under ASan, the test
`sleeping_animals_stay_put_are_not_hunted_and_scenes_without_a_cycle_ignore_it`
(`tests/test_main.cpp:868`) reported a use-after-free; it failed identically on the commit before
the mountains work, so it was left alone. It may be a test bug or a core bug (dangling `Entity*`
across `World::step`). **Next:** reproduce with `-fsanitize=address,undefined`, decide test vs. core,
fix; consider a `sanitize` CMake preset and CI job (F-15).

## F-07 Never-verified areas (M)
- Windows: `terminal_win.cpp` is only compiled and smoke-tested headlessly (`--dump`) on CI; it has
  never been run in a real console in these sessions. `scripts/build.ps1` likewise.
- Audio on Linux (ALSA, bare tty) and Windows (WASAPI): builds in CI, playback never confirmed.
- GCC: only reproduced through the owner's Fedora VM; an include audit was approximate.
- Linux console mode: judged from the owner's photos only; the owner accepted the result.
- Rendering colors have always been judged from text/escape sequences or rendered SVG snapshots, not by eye.
- The Pages Mountains button and videos were not opened in a browser by the assistant.

## F-08 Scene specifics inside the generic core (M)
The architecture goal is "a new scene is data". In practice:
- `AmbientKind` is an enum with one hand-written synth per scene, so a new scene needs C++ for
  sound, and `ambientFor` hard-codes sprite names (`"whale"`, `"tornado"`).
- `SceneDef::mountains` is a boolean switch; zones, ridges and the avalanche live in `world.cpp`.
- `world.cpp` is ~1,540 lines and `tests/test_main.cpp` ~2,560 lines; `ambience.cpp` ~530.
- The documented scripting hook (sprite behaviors beyond the C++ table) does not exist.
**Why it matters:** v1.2.0 (tvOS) and any further scene will touch these. **Proposal:**
split `world.cpp` (layout/terrain, weather, hunting, catastrophes) and the test file by topic
*before* adding frontends; consider data-driven sound recipes later. Not urgent for correctness.

## F-09 miniaudio runtime linking on Apple (M)
Suggested in the audio-licensing answer and never decided: define `MA_NO_RUNTIME_LINKING` for Apple
builds and link frameworks directly (CMake already links CoreFoundation/CoreAudio/AudioToolbox on
macOS) to avoid App Store review friction. For tvOS also plan `AVAudioSession` setup. Owner never
answered "add the define now?".

## F-10 `package-macos.sh` and the shared build directory (M)
`scripts/package-macos.sh` passes `-DDIORASCII_VERSION=<tag>` into `build/release-macos`, the same
directory `scripts/build.sh` uses. The value stays in `CMakeCache.txt`, so later local builds
report the old version (observed 2026-11-08: the cache holds `DIORASCII_VERSION=0.1.0`, a leftover from the very first
baseline, so local release builds there report `0.1.0`). Workaround:
`cmake -UDIORASCII_VERSION build/release-macos`. **Fix options:** separate build dir for packaging,
or unset the variable afterwards. Also `dist/` (script output) is not in `.gitignore`.

## F-11 Notarized release assets (M)
(Update 2026-11-11: the asset is now a `.pkg`, the manual swap is documented in the README. The installers
for Windows/Linux and the notarized `.pkg` run are **unverified**: see the unverified list in the 2026-11-11
history entry.) CI produces ad-hoc signed DMGs; the notarized one is built locally and swapped into the GitHub
release by hand (`gh` is not installed). Done for 1.0.0 (2026-11-07). The sessions do not show it
for 1.1.0 or 1.1.1, so those releases may still carry only the ad-hoc DMG - **check the releases
page**. The release checklist (build locally on the tag, upload with `--clobber` or via the web UI)
is not written down in the README or here. Candidates: document it, install `gh`, or add the
upload to the script.

## F-12 Sound (L)
Everything sonic was tuned from measured levels and the owner's ears. The lightning-strike crack
was left "fine for now"; the owner will return to it. Eagle/woodpecker/stream were accepted
after one tuning round; aquarium, desert-night, moon and whale levels were the assistant's
guesses and have had little or no reaction recorded.

## F-13 Memory migration (L, done)
The old project memory lived under `~/.claude/projects/...-asciiquarium/memory/` and was not
visible to sessions in the renamed directory. Its facts (no attribution trailer, fold fixes,
project direction) are now in `CLAUDE.md` and `guardrails.md`, and copied into this project's memory
for Claude Code. The old transcripts are the primary sources for [history.md](history.md); keep them.

## F-14 Naming consistency (L)
`LICENSE` says "Copyright (c) 2026 Christian Ahrens"; `CPACK_PACKAGE_VENDOR` is "Diorascii
contributors". The planned bundle ID is `com.ChristianAhrens.Diorascii` (registration status
unknown). Pick one holder line and use it in LICENSE, CPack and the About/credits text.

## F-15 Engineering hygiene gaps (L)
No sanitizer/UBSan preset or job, no clang-tidy (considered from the Python port's CI, never
adopted), no code-coverage or Doxygen-warning gate. CI covers build + tests + a dependency print.

## F-16 Documentation drift (L)
The sprite/scene reference lives only in the root README (about 430 lines) and is hand-maintained
alongside the code. These AI docs duplicate facts (scene list, behavior table, key list). When
behaviors, actions, scenes or formats change, update README, CHANGELOG, `docs/ai/architecture.md` and
`CLAUDE.md` together, or trim the AI docs to pointers.

## Decisions and outcomes (2026-11-08)

- **F-01:** the old rules stand (no attribution lines, develop/main workflow).
- **F-02:** README Status rewritten for five scenes and sound; `docs/ai/` added to its Layout list.
- **F-03:** the owner will delete `origin/asciiquarium_orig_main` themselves.
- **F-04:** unpushed commits may be rewritten; pushed ones need approval; tagged content is immutable.
- **F-05:** the old tooling need not return. New standing rule instead: all added code is
  commented and all affected documentation is updated in the same change.
- **F-06:** reproduced under ASan/UBSan. It was a **test bug, not a core bug**: the test
  dereferenced the sleeper after a hunter had eaten it (the world frees eaten entities), and
  evaluated `sleeper->x` before `eatenCount()`. The operands are swapped. The whole suite now
  passes under `-fsanitize=address,undefined` (0 failures). A sanitizer preset/CI job is still
  open under F-15.
- **F-08, F-09:** to be explained and investigated in dedicated steps, in plain language.
- **F-10:** cause confirmed (see entry); fix to be chosen.
- **F-11:** no notarized DMGs for 1.1.0/1.1.1; 1.2.0 will supersede them.
- **F-07, F-12, F-14, F-15, F-16:** todo.

- **F-10 fix (2026-11-08):** `package-macos.sh` builds in `build/package-macos` with explicit flags
  instead of the shared preset directory; `dist/` is ignored. The stale `0.1.0` in
  `build/release-macos` is a local, ignored leftover: delete that directory once.
- **tvOS stack decided:** native, CMake-built, no SDL3 (see history, decision 13).

## F-17 tvOS open points (M)
- Siri Remote input (swipes, click, long click, Menu, Play/Pause) has never been exercised: the
  simulator could only be launched headless. Test in the Simulator app or on a device.
- Sound defaults to on, on tvOS and in the terminal (decided).
- `Resources/Icon/` is now committed (drafts, to be refined).
- The Top Shelf images are drafts cropped from the icon art (needed for App Store validation); a dedicated
  design is still to do.
- The grid uses the system monospaced font; glyph coverage of the sprites' characters on tvOS is
  unchecked beyond the aquarium, and the bundled font planned earlier does not exist yet.
- Performance: the grid is redrawn fully each frame with CoreGraphics and cached glyphs (~15 % of a core in the
  simulator after optimising the libraries in Debug); unknown on an Apple TV HD/4K. A Metal glyph atlas is the fallback.
- Remote input: the grid view is focusable and keyboard presses arrive (seen in the log); their handling (keyboard keys, not remote button types) was added afterwards and still needs a run to confirm. The remote's own swipe/click path is untested.
- Deployment target is tvOS 17.0 (a guess); `connect:to:format:` is deprecated from tvOS 27.
- iOS is not wired up; the shared pieces (Session, audio_apple) are ready for it.

## F-18 Unverified art and sound of the 2026-11-09 work (M)
Written after v1.2.0 and listed in CHANGELOG `[Unreleased]`. Judged by the owner by ear and eye
(accepted unless noted): the sandworm's bellow and the avalanche squeal, the worm's chewing (after one
retune) and loudness. **Never heard or seen by the assistant:** the kraken's gulp; the look of the worm's
tongue and the kraken's tentacles in a real terminal (glyphs `= | / \ @`, colors `lash-color`,
reach 14 and 20, three tentacles); the moon's lighter/darker ground and its 13x8 sun; the 8x5
sun and moon in each scene; the larger clouds. Tunables are single values: `strike-reach`,
`strike-lashes`, `lash-color`, `mouth-row` in the sprites; gains and filters in `src/core/ambience.cpp`.

## F-19 Audio parameters are copied by hand in two backends (M)
**Resolved 2026-11-14** (phase 4 of the spatial-audio plan): both backends now hold a `Seqlock<AmbientParams>` and copy the whole struct; the per-field atomics and both field lists are gone, and a test fills every byte of the struct with its own value
and checks it arrives. The `audio_apple.mm` change was **not compiled** (no Apple toolchain here). Original finding:
`src/audio/audio.cpp` and `audio_apple.mm` each copy every `AmbientParams` field into atomics and back.
A new field that is forgotten in either backend is silent in the real program while the synthesis tests
pass; this happened on 2026-11-09 (sandworm, crush and squeal sounds). Noted in `ambience.h` and
the conventions. **Options:** one shared header with an atomic mirror of `AmbientParams` used by both
backends, or a compile-time check on the field count.

## F-20 Real-time mode (L)
`dio::localClockHour()` uses `std::localtime` (not thread safe; called from the main thread only). Not
verified: other time zones, daylight-saving changes, the tvOS build calling `setClockHour`, a host clock
set wrongly. The mode ignores `dayLength` and the scene's `start-hour`. No seasonal sunrise or sunset
(owner: not for now).

## F-21 Scene logic in the core; fragile fixed-seed tests (L)
Adds to F-08: `strike-mode`, tongues and tentacles, `airless` and the crush/squeal counters are generic
mechanisms but `ambientFor` still names `"sandworm"` and the synths are per scene. Fixed-seed tests
depend on the order of the population list: adding the larger clouds changed the random stream and the
goat test had to move to seed 7. Prefer asserting on properties over particular seeds when touching them.

## F-22 The City scene (M)
Added 2026-11-09 in six commits (shading header, summon action and lanes, scene, fire, sound, docs). Judged only from
text renders, tests and measured levels. **Never seen in a real terminal:** the skyline colors and window
patterns, the street and lamp pools, how people and cars sit on their lanes (cars of different speeds in one lane
overlap), the shop and park layout at other widths (decor uses fixed margins and may overlap on narrow screens),
the sprite masks (written quickly; check with `--gallery`), the flames, scaffolding and crane, the train and the
planes. **Never heard:** traffic, horns, siren, train, jet and fire (levels were tuned relative to the other
scenes: calm about 0.035 RMS, events up to about 0.08). Decisions to confirm: buildings are generated to
the terminal height (`skyline: 0.32..0.62` of the sky), the ambulance and fire engine are both `tag: siren`, the
fire is an invisible `blaze` sprite so that the rare scheduler is reused, headlights are not lit at night, the railway
needs a sky of at least 16 rows. Revised the same day after the owner's first look: flatter street (4 rows), taller and more varied buildings, a visible viaduct, vehicles and planes drawn with solid block glyphs (`█`; they vanish in no style, unlike solid mask blocks in Mono). Side streets with turning cars were added after that (cars keep their side-view sprite in the avenue; the trapezoid is a flat painting, not perspective). The plane was redrawn after the owner's pictogram (30x7: far wing above, near wing below, blue-tipped fin, window row, blue belly stripe; the first 4-row version with wings was judged unrecognisable), rush-hour traffic, a longer fire-engine stay with barriers and blue water jets were added at the owner's request (the look of all of it is unseen by the assistant). The vehicles, train and planes were redrawn as line art with a darkened backing after the solid blocks read as blobs (owner); the fire engine now fights the fire and cordons the site (owner). After the owner's screenshot the side streets were redone (broad crossing, short, behind the building rows, glimpses through gaps), cars got five views (side, rear and front, with three-quarters in between), the traffic and crowds were thinned and the planes made slim (20x3); still unseen by the assistant in a terminal. The train sound was reworked to a klack-klack rhythm (only the rhythm is tested, not how it sounds). Fires sometimes get out of hand (30%): 3-5 more buildings burn down and are rebuilt after about 1.5 minutes (look and timing unseen by the assistant; the rubble drawing is a first draft). The landing page (`pages/`), its videos and the README screenshots got the City on 2026-11-11 (owner's recordings, a
simple skyline button icon drawn by the assistant). Adds to F-08: the city is data plus about 350 lines of C++ in `city.cpp`.

## F-23 The second catastrophes (M)
Added 2026-11-10. Judged only from text renders, headless runs, tests and measured levels; **never seen in a terminal or
heard**: the green trail and the brown flood water (the sparse front) (colors and glyphs), the submarine, helicopter, yeti and the two
monsters (drawn by a silhouette rasteriser, solid dark bodies with outline glyphs, 60x33 cells; the yeti 27x17), the
mud, the tracer lines, the roars and thuds (only RMS ratios are tested, at least 1.5 times the calm level). **Decisions to
confirm:** (1) *names*: the owner's pictures show Godzilla, a MUTO and a yeti; Godzilla and MUTO are trademarks of
Toho and Legendary, so the sprites are named `lizard_titan` and `winged_titan` and the docs say "giant lizard" and
"winged monster" (the art is original, drawn from silhouettes and not from the pictures); say if the real names are
wanted. (2) The flood and the yeti spare what flies (birds, butterflies); the helicopter shoots everything. (3) The
yeti appears standing on the highest ridge rather than rising behind it (entities are always drawn over the
terrain). (4) The moon's big meteor is a sprite of 8 frames on one canvas (a bigger rock inside a 28x14 cell), so the "growing" is stepwise; the impact digs a crater to the ground-row limit (at most 3 rows) across the width. (5) In the city the fire spreads out from the fight to about 45% of the width (about 28 buildings on a 150-column screen) and nothing else reacts: people and cars carry on, no cordon, no fire
engine; the winged monster only dies, there is no wreck. (6) A screen under 28 rows or narrower than 110 columns
cancels the monster fight silently (no room); the yeti is 17 rows tall, so it fills most of a 24-row screen.
(7) `World::clashStep`, `maulStep` and the others are special-purpose code in the generic core (adds to F-08, F-21).
Fixed-seed tests of the first catastrophes now call `triggerRare(RarePick::Common)`.

## F-24 Spatial audio, phase-0 spike (M)
Added 2026-11-13. A throwaway program (not committed; it lived in the assistant's scratchpad) queried and opened miniaudio
devices to replace some **[verify]** marks in `docs/ai/spatial-audio-plan.md`. **Measured** on Linux (a ThinkPad with
PipeWire, reached through miniaudio's PulseAudio backend), one playback device, native format 2 channels at 48 kHz:
(1) `channels = 0` opens with the native 2 channels and the map FL FR. (2) The same device also opened with 6, 8, 12 and 16
channels (the sound server remaps), so **a successful open does not show that the speakers exist**; `auto` must start from
the native count. (3) miniaudio's default maps above 8 channels are 7.1 plus `AUX_0..` with **no height positions**; an
explicit 7.1.4 `pChannelMap` with `TOP_FRONT_*` and `TOP_BACK_*` was accepted and reported back unchanged. (4) By reading
the header: miniaudio has **no wide pair and no top-middle pair**, only `TOP_CENTER` and `AUX` channels, so Lw/Rw and
Ltm/Rtm (needed for 9.1.4, 9.1.6, 5.1.2, 7.1.2) can only be routed as `AUX`. The plan (section 5.2) was corrected accordingly.
**Not verified:** anything on CoreAudio, WASAPI or ALSA; the HDMI outputs (they were not listed as separate devices
by this PulseAudio setup); any real surround or Atmos-capable device; whether `channels = 0` means "native" on every
backend; how drivers route `AUX` channels; all of the Apple side (tvOS, AirPods), which is still the gate of phase 0. **Heard by the owner (2026-11-13, same stereo device):** `identify` played left, right, left (the expected order; the 2.2 s run
wraps the cycle) and `sweep` moved from left to right, so the stereo channel order and the map-based positions work
there; the assistant could not hear either. Not tested: any device with more than 2 real speakers, so side, rear,
height and LFE routing remain unheard. **Phase 7a (2026-11-14):** `AudioOutput::start(choice)` now opens with 0 channels, negotiates and (for a named layout) reopens with our own map; on this stereo PulseAudio device `auto` gives 2.0 and every explicit layout opens and is labelled downmixed, as measured above. Still unmeasured: any real multichannel device. **Next:** run the spike on the Mac and on the Apple TV
and receiver setups and record the native formats here.

## F-26 Multichannel sound: what is unverified (H)
Phases 3 to 8 of `spatial-audio-plan.md` are implemented and tested by measurement (levels, directions, determinism, golden stereo values, mutations), on one Linux machine with a stereo PulseAudio device. **Not verified, needs the owner**: (1) how any of it sounds, above all the placed sounds in a real room (strike, siren, flood, meteor), the rising bubble strings, the tiny bubbles and the rain overhead, whose levels were chosen by measurement and are single constants in `src/core/ambience.cpp`; (2) any device with more than two real speakers, on any backend (CoreAudio, WASAPI, ALSA, HDMI), route changes, and whether drivers route `AUX` channels; (3) **the Apple backend (`src/audio/audio_apple.mm`) was written without a compiler**: the Atmos layout tag names, the channel order of each tag, the audio session calls, the mixer path and whether the OS spatializes the stream for AirPods; (4) the tvOS view controller's edits (`takeLayoutChange`, `soundLayout`, `update`); (5) the macOS and Windows builds of the whole history (macOS: see the update below; Windows: only the CI). **Next:** build on macOS and Windows, run `scripts/bench-audio.sh`, listen with a 5.1 or better system and with headphones, and compile the tvOS target (simulator first, then the Apple TV with a receiver), then record the results here.

**Update 2026-11-17: first run on macOS (release build, 16-channel loopback device "BlackHole 16ch", set to 7.1.4 in Audio MIDI Setup).** Measured by recording the device's loopback input and taking the RMS of each channel, not by listening. The app is stable (menu, scene switches, actions, the live Speakers entry, a clean quit). **Found: the heights of 7.1.4 did not reach the device.** CoreAudio does report the layout (`kAudioDevicePropertyPreferredChannelLayout`, output scope: 12 labelled channels L R C LFE LeftSurround RightSurround RearSurroundLeft RearSurroundRight LeftTopFront RightTopFront LeftTopRear RightTopRear, then four "unknown"; Apple Music Atmos plays all twelve), but miniaudio's CoreAudio backend asks the AudioUnit for `kAudioUnitProperty_AudioChannelLayout` on the input scope, which a playback device does not have (error -10879 in a probe; not confirmed inside miniaudio's own setup), and then assumes a default map (7.1 and eight AUX channels). So `auto` showed "custom map on 16 channels" with sound on channels 1 to 8 only, and an explicit 7.1.4 asked the device for `TOP_*` positions that the assumed map lacks, so channels 9 to 12 stayed silent. **Fixed:** `src/audio/coreaudio_layout.*` reads the device's own layout (labels, or the labels of a layout tag) and `positionForCoreAudioLabel()` (`core/negotiate.*`, tested) turns them into positions (`DevicePosition` gained the wide and top-middle pairs, which no library names); `DeviceReport::exact` marks such a map. `auto` now finds the named layout when the channels that have a position (unused ones are ignored, in the middle or at the end) are exactly those of a named layout, in any order, and then **routes** it (`Negotiation::routed`, `routeChannels()`): the device stays open as it is and layout channel `i` is written to the device channel of the same position, no remapping by the library. A named choice on an `exact` device that has a place for each of its channels is routed the same way. **Tested interactively on 2026-11-17** by setting the device to each layout in Audio MIDI Setup and recording the loopback (RMS per channel, forest at night; the menu text is what the app said):
| device set to | what CoreAudio reports | auto | sound on channels (auto) |
|---|---|---|---|
| 9.1.6 | L R C LFE Ls Rs Lrs Rrs, Ltf Rtf Ltr Rtr, Lw Rw, Ltm Rtm (wides and top-middles last) | 9.1.6, routed through the permutation | 1 to 16 |
| 7.1.4 | the 7.1.4 above, then four unused | 7.1.4 | 1 to 12 |
| 5.1.2 | 5.1, six unused, the heights as top-middle on 13 and 14 | 5.1.2, routed across the gap | 1 to 6 and 13, 14 |
| 7.1 (variant 1) | L R C LFE Ls Rs **Lc Rc** (label 7, 8 are left/right *centre*), eight unused | "custom map" (its own angles) | 1 to 8 |
| 7.1 (variant 2) | 5.1, two unused, rear surrounds on **9 and 10** | 7.1, routed across the gap | 1 to 6 and 9, 10 |
| 6.1 | 5.1 and a back-centre (9) on channel 7 | "custom map" | 1 to 7 |
| 5.1 | 1 to 6, ten unused | 5.1 | 1 to 6 |
| 2.0 | 1, 2, fourteen unused | 2.0 | 1, 2 |
An explicit layout that the device has a place for plays on exactly the same channels as auto (explicit 7.1.4 on 7.1.4, 5.1.4 on 7.1.4: front, sides and heights, rear pair silent, 9.1.4 on 9.1.6: top-middles silent). **Resolved the same day (owner: yes):** a layout the device has no place for **steps down** to the richest layout it has, which is routed; the old path (the library mixing it into its assumed map) played on channels the device marks as unused and missed the real ones (7.1.4 on the 5.1 or 7.1 devices above played on channels 7 and 8, 5.1 and 7.1.4 on the 2.0 device on 3 to 8). Measured on the device set to 2.0: 5.1, 7.1.4 and 9.1.6 all give "2.0" (the menu shows `5.1 > 2.0`) with sound on channels 1 and 2 only. A device of one or two channels is left to the system (opened with the layout's map, mixed down, "(mixed down)"). The gaps in some of the maps above (unused channels between the speakers) come from the channel map Audio MIDI Setup defaults to for a channel count above what the layout needs; they are handled but not a design goal. **Still open:** (a) the reader and the routing are macOS-only, and only the default output device is read (the one miniaudio opens); (b) a layout given as a bitmap is not read (the library's assumed map is used); (c) nothing here says how it sounds, only levels per channel, and Windows and Linux are untouched (their maps stay the library's, `exact` is false); (d) the same gap affects any app built on miniaudio on macOS, which could be reported upstream (vendored version 0.11.21); (e) `a_seqlock_reader_never_sees_a_torn_value_while_the_writer_keeps_writing` (`reads > 100`) failed in about one of three full runs on this machine while a build ran next to it: the writer finished its 400000 writes in a few milliseconds before the reader thread was scheduled. **Fixed 2026-11-17**: every 2000 writes the writer waits (at most a second) until the reader has read something new; with the reader started 50 ms late the old test failed 5 of 6 runs, the new one 0 of 6 (checked in a standalone copy of the logic, not under real build load).

**Update 2026-11-17 (later): macOS, tvOS and iOS play through `AVSampleBufferAudioRenderer`; the CoreAudio reader, the routing code and (briefly) an `AVAudioEngine` backend are gone.** What decided it, measured on a MacBook with AirPods Pro and a 16-channel loopback device: (1) the system's "3D audio" (Control Center, head tracking) is offered only for content that comes through `AVPlayerItem` or `AVSampleBufferAudioRenderer` (Apple's WWDC21 session names those two); a plain CoreAudio app (miniaudio) and an `AVAudioEngine` source tagged 7.1.4 with the mixer connected to the output both showed "3D unavailable" (the listener saw it, and the engine run was the probe, not the app); (2) a tagged 7.1.4 and a tagged 9.1.6 sample-buffer stream played through the renderer showed "3D audio available and active" and the 7.1.4 tone moved around the listener (9.1.6: the listener confirmed the indicator, not the placement of the last four channels), and the app's own forest as 7.1.4 did the same. On speaker devices the renderer behaves exactly as the engine's mixer did: with each source channel at its own amplitude on the loopback device set to 9.1.6, 7.1.4 and 5.1, a 5.1, 7.1.4 or 9.1.6 source lands on the right channels by label (including the wides and top-middles, last on the 9.1.6 device), a richer source is mixed into the channels the device has, and the others stay silent. The app plays a layout the device has as it is, steps down for one it has not, and on a one- or two-channel route that spatializes (Bluetooth on macOS: the SDK has no query there; `spatialAudioEnabled` on tvOS and iOS) renders 7.1.4 for `auto`. Of the open items above, (a), (b) and (d) no longer apply to Apple platforms; (c) still does for Windows and Linux. **Not verified:** how any of it sounds; the app on the speaker devices with the final backend beyond the numbers above; the delay of sounds that an action starts (designed at 0.2 to 0.3 s, not measured); whether the AirPods spatializer takes 9.1.6 as 9.1.6 or folds it; tvOS and iOS hardware (the simulator's route is stereo, so `spatialAudioEnabled` and an HDMI receiver are untried); a Bluetooth speaker or other Bluetooth device on macOS; changing the device while running; head tracking and the personalized profile (needs the entitlements, not used).

## F-25 The forest chain runs in every scene (M)
Added 2026-11-13, from the profile in `spatial-audio-plan.md` section 2.4. In `Ambience::render`, the forest's wind (two noise-driven filters per ear), rain, thunder
(when active), crickets (three `sin`-heavy oscillators), the gust and wander state, and `thunderLevels()` run **for every scene**; the result is multiplied by
`mix_[Forest]`, which is 0 except in the forest. The other scenes' own functions are skipped below a presence of 0.001, the forest chain is not. Measured with `perf`
(i3-4010U, all sounds active): `render()` itself takes 49 % of the time in the aquarium, 42 % in the desert, 34 % on the moon, 27 % in the mountains and 24 % in the city (the forest: 96 %);
part of that is legitimately shared (the mix, `softClip`, event handling), but the forest chain is a large part. With one chain per speaker it would be multiplied by up to 7.5.
**Resolved 2026-11-13 (owner: yes, gate it).** `Ambience::render` now makes the forest's wind, rain, thunder and crickets only while its presence is above 0.001, like the other
scenes. The gust and wander state keep running (the desert and mountains use the gust), and a silent forest **still advances the random generator exactly as before** (two draws per
channel per sample, a third while a clap rings), so the noise and events of every other scene are unchanged. Result: ten of the twelve golden cases stay bit-identical (`cmp`); the two
forest cases differ by at most 1.3e-6, only in the first 10 ms of the fade-in (the filters start cold when the gate opens). In a forest storm that is left while thunder rumbles
and re-entered 15 s later, the largest difference to the old synth is 4.5e-4 (-67 dBFS) at the moment the gate closes (about 4 s after the scene change, when the presence falls
below 0.001; the same kind of step the other scenes have at their own gates), then it decays; a recorded golden case from the old synth pins the scene-change scenario. CPU on the
development machine (release preset, % of one core, 2.0, before / after): aquarium 3.65 / 2.43, desert 4.96 / 3.87, moon 4.20 / 3.05, mountains 6.75 / 5.61, city 8.85 / 7.64, forest 2.43 / 2.51 (unchanged).
