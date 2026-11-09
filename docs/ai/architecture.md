# Architecture

State as of v1.2.0 plus the unreleased work of 2026-11-09 (see CHANGELOG `[Unreleased]`). Line counts are approximate and exist to show where the weight is.

## Layers

```
src/core   (no I/O, deterministic per seed; library diorascii_core)
  Rng, Frame/Cell, Sprite/Props, SceneDef, AssetPack, World + Entity + behaviors, Menu,
  Ambience (sound synth)            + generated embedded_assets.cpp and version.h
        |  Frame = grid of {codepoint, fg, bg, attrs}          ^  Action (input)
        v                                                       |
src/term   VT/ANSI encoder (vt.cpp) + Terminal (terminal_posix.cpp | terminal_win.cpp)
src/audio  miniaudio device wrapper; own thread, parameters via std::atomic
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

`Session` (src/app/session.*, library `diorascii_app`) holds all interactive state and is driven by
any frontend: `handle(Action)`, `resize`, `step(dt)`, `render(Frame&)`, plus `soundWanted()` /
`soundUnavailable()` / `ambient()` for the frontend's sound device and `takeRepaint()`. The
terminal `main.cpp` adds only pacing, the terminal and the audio device. Fixed 20 fps (`--fps`), world steps at `dt = 1/fps`; input polled in 5 ms slices; the menu is
drawn into the same `Frame` as the scene. It shows the version and the copyright as small print (`Menu::setFooter`, set by `Session`). Frames are encoded by the pure function
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
  glyphs** (`░▒▓`) because console fonts draw them as `#`.
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
chase twister emit_dust catastrophe ledge soar avalanche`

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
| Sleeping | ~70% of most species sleep at night, each with its own bedtime; sleepers do not hunt, eat or get hunted |
| Events | one scheduled visitor at a time per scene (`--- events`) |
| Rare catastrophes | separate slow timer, one at a time; strike at `strike-at` progress (`strike-mode`: `centre` all at once, `sweep` each victim as the attacker's middle passes it, `devour` as its front reaches it, preceded by a tongue or tentacles (`World::lash`, `Tongue`, drawn in `renderTongues`; `strike-aim: around` for the kraken) when `strike-reach` is set); wipes `life` or named sprites; dead things restock gradually; screen flash; `x` summons one |
| Actions | `Drop` (food), `Impact` (meteor + crater that fills with dust), `Weather` (rain, sandstorm, tornado that flings animals), snowstorm; selected by `action:` in the scene |
| Hunting | `hunt`/`chase`: predators tagged `hunt` eat `prey`; lion and bear make one kill each |

Collision is simple tag/position checks, not a physics engine. Randomness comes from an
in-house xoshiro `Rng`, so a seed reproduces a world exactly.

## Sound (src/core/ambience.*, src/audio)

`ambientFor(World)` reduces world state to `AmbientParams` (kind, night, rain, flashes, storm
proximity, nibbles, whale/tornado nearness, impacts, avalanche level, sandworm nearness, crush count (desert: chewing, aquarium: gulping), avalanche squeal count, volume). The audio backends (`audio.cpp`, `audio_apple.mm`) copy `AmbientParams` field by field into atomics, so a new field must be added there too (a missing one is silent in the real program while the synthesis tests pass). `Ambience::render`
fills a stereo float buffer; everything is smoothed and scenes crossfade, so nothing clicks. The
audio thread reads parameters through atomics. Playback is miniaudio 0.11.21 (Unlicense/MIT-0),
playback only, decoding and engine compiled out, stderr silenced while the device opens (ALSA
noise). On by default (owner, 2026-11-08); `--mute`, key `a` or the menu switch it; silent when no device exists (announced only for `--audio` or the `a` key).

Event sounds are driven by counters (flashes, impacts, crushes, squeals) that only count as events when they increase; the first value seen is the baseline.

One `AmbientKind` per scene (`Silent, Forest, Aquarium, Desert, Moon, Mountains`) with a hand-written
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
  `package-macos.sh`, `release-notes.sh`.
- CI: `linux.yml`, `macos.yml`, `windows.yml` on every push/PR; `docs.yml` (Doxygen + landing page
  to `gh-pages`) on push to `main`; `release.yml` on tags `x.y.z` (no `v`): verifies the tag
  against the changelog, builds and tests three platforms, creates a **draft** release with the
  changelog section as text and publishes it only when all three passed.
- macOS asset is a DMG (binary, `usr-local-bin` symlink, `Install to local bin.command`,
  LICENSE, README). CI signs ad-hoc only. Notarized assets are made locally with
  `scripts/package-macos.sh` (keychain profile `APP_NOTARIZATION_CREDENTIALS_PROFILE`) and uploaded
  to the release by hand, because `gh` is not installed on the owner's machine.
- Landing page `pages/index.html`: scene toggle (fish/tree/palm/mountain/star icons) and
  day/night toggle over `Resources/Diorascii_<Scene>_<Day|Night>.mp4` backgrounds; the choice is
  remembered in the browser. Videos are cropped to terminal content only, 20 fps, 1920 px wide.

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
Audio: `src/audio/audio_apple.mm` (AVAudioEngine + AVAudioSourceNode, standard non-interleaved
format; the mixer rejects interleaved). Info.plist is a template (`$(...)` placeholders because
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
