# Conventions

## Code

- C++20, `CMAKE_CXX_EXTENSIONS OFF`, warnings on (`-Wall -Wextra -Wpedantic`, `/W4 /utf-8`); **no
  warnings** are tolerated in project code. Namespace `dio`. Headers include what they use:
  macOS's compiler hides missing includes that GCC reports (`<cmath>` for `std::fabs` broke the
  Fedora build), so every file includes the standard headers it uses directly.
- Deterministic core: randomness only through `Rng` seeded by the world; no wall clock, no
  threads, no I/O in `src/core`.
- Prefer data over code: new creatures and scenes are `.sprite`/`.scene` files; add a C++
  behavior only when no existing one fits, and register it in the table in `world.cpp`.
- Platform code is isolated per file (`terminal_posix.cpp`, `terminal_win.cpp`); shared logic is pure
  functions (`encodeFrame`, `Ambience::render`) so it is testable.
- Match the surrounding style: terse comments that explain why, `kName` constants, early returns.

## Documentation (Doxygen)

- The owner expects **full Doxygen documentation from the start**: every class, struct, enum,
  enum value, function, member and constant in headers, plus helper functions in `.cpp` files,
  and an `@file` + `@brief` at the top of every source file. Each field gets its own `///`.
  Missing it once led to a full-history rewrite.
- `Doxyfile` reads `README.md` and `src` (main page = README; `*/docs/*` is excluded, so these
  AI docs are not part of the generated site). `cmake --build <dir> --target docs` runs it.
- The user-facing formats (sprite keys, scene headers, behaviors, CLI) are documented in the
  root README; **any new sprite key, scene header, behavior, scene, key binding or CLI option
  must be added there and to `CHANGELOG.md` in the same commit.**

## Tests

- `tests/test_main.cpp`: own tiny runner (`TEST(name)`, `CHECK(cond)`), registered in ctest as
  `diorascii_tests`, plus `diorascii_reports_version`. No test framework dependency.
- New behavior gets tests; the owner's sessions validated them by **mutation** (break the logic on
  purpose, confirm a test fails, restore). Tests that search for a seed must be bounded (an
  unbounded search once hung).
- Every scene is exercised at odd sizes down to 1x1 and at every hour; determinism per seed is
  asserted. A sprite lint test fails on colored blank cells without `solid: yes`.
- Sound is tested by measured levels (RMS/peak) and structure, not by listening - see below.

## Art conventions

- All art is original; never trace or paste from asciiquarium or its ports.
- `--gallery` prints every sprite for review; `--dump N --scene X --seed S` renders a scene as text.
- Animals must contrast with their backdrop in `full` style (see `backing`, `contrast`); the forest
  deer is bright tan, the rabbit light grey, the duck white because dark browns vanished at dusk.
- Aquarium restraint: the owner found the first version too busy; keep populations moderate and
  speeds slow. Bubbles are small and dim (a big `O` reads as a hole).
- Filled silhouettes for small animals were tried and rejected (featureless blocks).
- Sprite `facing:` states the direction the art points; getting it wrong makes animals swim
  backwards (the whale was fixed by `facing: left`).
- Check new scenes in all three styles and with day/night.

## Sound design

Every sound is synthesized. The owner approves by ear; the assistant cannot hear, so state that
levels were only measured. Accumulated preferences from the forest iterations:

- Calm wind: soft, low-passed, slow irregular swells, quiet. Not surf-like, not "microphone in
  wind", no band-pass whistle, nothing in the 400 Hz-2.6 kHz range with fast flicker (it crackles).
- Storm wind: deeper and about three times stronger with rain; the owner called the rain+wind
  case "perfect" - do not change it when tuning the calm case.
- Thunder: starts as the cloud approaches (rumble and distant claps), strikes sound immediately
  (30-100 ms after the flash), rumble less low-pass and about 9 s long. **The lightning crack is
  unfinished** (the owner will resume it); knobs are in `src/core/ambience.cpp`.
- Desert calm: barely audible. Moon: only a faint ground drone. Mountains: no chiming birds
  (sounded like a cellphone); an eagle cry every 15-35 s and a woodpecker every 15-35 s, by day,
  silenced in a snowstorm; avalanche = roar plus crackle at the break and thumps/clicks while it
  rolls; a stream must not sound like "brushing" (low-passed, rippled).
- Noise level is sample-rate independent (`noiseScale_`); quote levels at 48 kHz.
- Thunder/flash counters must not trigger on world rebuild or when sound is switched on mid-storm
  (the first counter value is the baseline).
- Tuned values are single constants; report which knob you changed and in which direction.
- Sandworm and kraken (2026-11-09, by the owner's ears): bellow (deep, trumpeting) and avalanche squeal
  accepted; the first crunch "sounded like a shot" (one sharp, decaying burst plus a deep thump), so
  an event sound is now a series of soft bites with no sharp start; both worm sounds were too loud at
  first and were halved; keep event sounds well below the storm sounds. Fundamentals below about
  100 Hz are inaudible on small speakers: put the energy in harmonics above 150 Hz.
- A new `AmbientParams` field must also be copied in the audio backends (`audio.cpp`,
  `audio_apple.mm`). Forgetting it passes every test and is silent in the real program.

## Changelog and commit messages

- Keep a Changelog format; entries are sentences starting with "Added/Changed/Fixed ...". New work
  goes under `## [Unreleased]`; the pin commit is named `Pin vX.Y.Z for release`.
- Commit subjects are imperative, no trailing period ("Add the mountain scene"). One commit per
  coherent change; per-scene commits for soundscapes; docs/README updates in the commit that
  needs them or a dedicated documentation commit. No attribution trailer (see guardrails).
- Each commit must build warning-free and pass its tests when checked out alone (the owner's
  sessions verified this in temporary `git worktree`s).
