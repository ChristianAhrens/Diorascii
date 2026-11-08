# Architecture

State as of v1.1.1. Line counts are approximate and exist to show where the weight is.

## Layers

```
src/core   (no I/O, deterministic per seed; library diorascii_core)
  Rng, Frame/Cell, Sprite/Props, SceneDef, AssetPack, World + Entity + behaviors, Menu,
  Ambience (sound synth)            + generated embedded_assets.cpp and version.h
        |  Frame = grid of {codepoint, fg, bg, attrs}          ^  Action (input)
        v                                                       |
src/term   VT/ANSI encoder (vt.cpp) + Terminal (terminal_posix.cpp | terminal_win.cpp)
src/audio  miniaudio device wrapper; own thread, parameters via std::atomic
src/app    Session (scene, menu, style, time of day, pause, toasts, info line, sound request; no I/O)
           + main.cpp: CLI and the terminal main loop around a Session
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
drawn into the same `Frame` as the scene. Frames are encoded by the pure function
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

Cross-cutting systems, all in `World`:

| System | Notes |
|---|---|
| Layout | surface/seabed/ground rows, river, `lanes` for depth on a bank; the mountains scene has terrain (`ridgeRow`, `zoneAt`, `Zone` bands with wobbling limits) |
| Light / time | `Light{day,sun,glow,stars,haze,hazeColor}`, phase 0..1, `TimeMode` (cycle, dawn, day, dusk, night); sun/moon arcs; scenes without an atmosphere (moon) have no cycle |
| Sleeping | ~70% of most species sleep at night, each with its own bedtime; sleepers do not hunt, eat or get hunted |
| Events | one scheduled visitor at a time per scene (`--- events`) |
| Rare catastrophes | separate slow timer, one at a time; strike at `strike-at` progress; wipes `life` or named sprites; dead things restock gradually; screen flash; `x` summons one |
| Actions | `Drop` (food), `Impact` (meteor + crater that fills with dust), `Weather` (rain, sandstorm, tornado that flings animals), snowstorm; selected by `action:` in the scene |
| Hunting | `hunt`/`chase`: predators tagged `hunt` eat `prey`; lion and bear make one kill each |

Collision is simple tag/position checks, not a physics engine. Randomness comes from an
in-house xoshiro `Rng`, so a seed reproduces a world exactly.

## Sound (src/core/ambience.*, src/audio)

`ambientFor(World)` reduces world state to `AmbientParams` (kind, night, rain, flashes, storm
proximity, nibbles, whale/tornado nearness, impacts, avalanche level, volume). `Ambience::render`
fills a stereo float buffer; everything is smoothed and scenes crossfade, so nothing clicks. The
audio thread reads parameters through atomics. Playback is miniaudio 0.11.21 (Unlicense/MIT-0),
playback only, decoding and engine compiled out, stderr silenced while the device opens (ALSA
noise). Off by default; `--audio`, key `a`, or the menu; silent when no device exists.

One `AmbientKind` per scene (`Silent, Forest, Aquarium, Desert, Moon, Mountains`) with a hand-written
synthesis function each. See [findings](findings.md) F-08 about the coupling this implies.

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

## Apple/tvOS notes gathered so far

- App ID to register: `com.ChristianAhrens.Diorascii` (explicit; select all platforms that might
  ship). No special capabilities, App Services or capability requests are needed (offline, no
  accounts). Game controller / Siri Remote input is a framework, not a capability.
- tvOS needs: layered app icon, Top Shelf image, CMake `CMAKE_SYSTEM_NAME=tvOS` with the Xcode
  generator, a monospace font bundled with the app, coarse cell grid (about 100-160 columns for
  10-foot legibility), Menu button returns to the system at the root, no screensaver possible.
- miniaudio on Apple dlopens system frameworks by default; `MA_NO_RUNTIME_LINKING` was suggested for
  App Store builds but not yet decided (finding F-09). Audio session setup is the first thing to test on tvOS.
