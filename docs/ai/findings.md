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
| F-11 | M | Notarized macOS assets are a manual, undocumented step | deferred: 1.1.x stay ad-hoc, 1.2.0 supersedes |
| F-12 | L | Sound design is unheard by the assistant; lightning crack unfinished | todo |
| F-13 | L | Project memory was not carried over after the directory rename | done (see below) |
| F-14 | L | Copyright holder / vendor naming is inconsistent | todo |
| F-15 | L | No sanitizer, lint or `build.ps1` evidence in CI | todo |
| F-16 | L | Documentation drift risks | todo |
| F-18 | M | Unverified art and sound of the 2026-11-09 work | open |
| F-19 | M | Audio parameters are copied by hand in two backends | open |
| F-20 | L | Real-time mode: time zones, DST, tvOS | open |
| F-21 | L | More scene logic in the generic core; fragile fixed-seed tests | open |
| F-22 | M | The City scene was never seen or heard by the assistant; page and screenshots lack it | open |

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
CI produces ad-hoc signed DMGs; the notarized one is built locally and swapped into the GitHub
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
needs a sky of at least 16 rows. Revised the same day after the owner's first look: flatter street (4 rows), taller and more varied buildings, a visible viaduct, vehicles and planes drawn with solid block glyphs (`█`; they vanish in no style, unlike solid mask blocks in Mono). Side streets with turning cars were added after that (cars keep their side-view sprite in the avenue; the trapezoid is a flat painting, not perspective). The plane was redrawn after the owner's pictogram (30x7: far wing above, near wing below, blue-tipped fin, window row, blue belly stripe; the first 4-row version with wings was judged unrecognisable), rush-hour traffic, a longer fire-engine stay with barriers and blue water jets were added at the owner's request (the look of all of it is unseen by the assistant). The vehicles, train and planes were redrawn as line art with a darkened backing after the solid blocks read as blobs (owner); the fire engine now fights the fire and cordons the site (owner). After the owner's screenshot the side streets were redone (broad crossing, short, behind the building rows, glimpses through gaps), cars got five views (side, rear and front, with three-quarters in between), the traffic and crowds were thinned and the planes made slim (20x3); still unseen by the assistant in a terminal. The train sound was reworked to a klack-klack rhythm (only the rhythm is tested, not how it sounds). Fires sometimes get out of hand (30%): 3-5 more buildings burn down and are rebuilt after about 1.5 minutes (look and timing unseen by the assistant; the rubble drawing is a first draft). Not done: the landing page (`pages/`), its videos and the README screenshots do not
show the City yet; no new icon art. Adds to F-08: the city is data plus about 350 lines of C++ in `city.cpp`.
