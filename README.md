# Diorascii

[![Linux](https://github.com/ChristianAhrens/Diorascii/actions/workflows/linux.yml/badge.svg)](https://github.com/ChristianAhrens/Diorascii/actions/workflows/linux.yml)
[![macOS](https://github.com/ChristianAhrens/Diorascii/actions/workflows/macos.yml/badge.svg)](https://github.com/ChristianAhrens/Diorascii/actions/workflows/macos.yml)
[![Windows](https://github.com/ChristianAhrens/Diorascii/actions/workflows/windows.yml/badge.svg)](https://github.com/ChristianAhrens/Diorascii/actions/workflows/windows.yml)

Animated ASCII scenes for your terminal: an underwater aquarium, a forest with a
river, the surface of the moon, and more to come. Written in C++20 as one small, self-contained binary with
no runtime dependencies. Truecolor with automatic 256-color fallback, or a
terminal-themed look that follows your own colors.

**[Download the latest release](https://github.com/ChristianAhrens/Diorascii/releases/latest)**
&middot; [Project page](https://christianahrens.github.io/Diorascii/)
&middot; [API documentation](https://christianahrens.github.io/Diorascii/doxygen/index.html)

## Download

Every release on the [releases page](https://github.com/ChristianAhrens/Diorascii/releases/latest)
contains one archive per platform. Unpack it and run `bin/diorascii` (`bin\diorascii.exe`
on Windows) from a terminal. Nothing else needs to be installed.

| Platform | Archive | Notes |
|---|---|---|
| macOS | `Diorascii-x.y.z-macOS.dmg` | universal (Apple silicon and Intel), disk image |
| Linux | `Diorascii-x.y.z-Linux.tar.gz` | 64-bit, no extra libraries |
| Windows | `Diorascii-x.y.z-Windows.zip` | Windows Terminal or Windows 10+ console |

The macOS binary is signed ad hoc but not notarized. If macOS refuses to open a
downloaded copy, clear the quarantine flag once: `xattr -d com.apple.quarantine bin/diorascii`.

## Usage

    diorascii                       # m menu, left/right scene, f action, c style, t time of day, i info, p pause, r restart, q quit
    diorascii --scene moon          # start in a specific scene (--list-scenes shows all)
    diorascii --style terminal      # 16 ANSI colors on your own background (also: mono, full)
    diorascii --dump 100            # headless: print the scene after 100 steps
    diorascii --dump 200 --act 20   # ...and trigger the scene's action at step 20
    diorascii --hour 22             # start at 22:00 (scenes with a day and night cycle)
    diorascii --gallery             # print every sprite (art review)
    diorascii --assets ./my-art     # add or override sprites and scenes from a directory

The menu (`m`, Enter or Esc) lists scene, style, time of day, info line, the scene's action, resume
and quit. It is drawn into the same cell grid as the scene and driven only by abstract
actions (up, down, left, right, select, back), so any input device can use it.

### Scenes and the action key

`f` (or `F`) triggers the scene's primary action. What it does depends on the scene:

| Scene | Action | What happens |
|---|---|---|
| Aquarium | Feed the fish | a clump of eight twinkling flakes of food sinks from the surface (at most 30 at once); fish swim over and eat them, and the action's name flashes on screen so you can tell it worked |
| Forest | Make it rain | it rains for about 18 seconds (pressing again keeps it going): drops streak down at a slant and splash on the bank and in the river, clouds roll in and the light dims, then it clears up again |
| Moon | Meteor strike | a meteorite streaks down and hits the ground: it wrecks everything standing there, digs a crater and throws up debris and dust that drift back down slowly under the moon's low gravity. Dust then fills the crater gradually, shallow rim first, until the ground is smooth again. Rovers drive down into the crater and climb out as it fills. |

New scenes define their own action in their `*.scene` file (see below).

### Day and night

The aquarium and the forest have a day and night cycle (a full day takes four minutes).
The sun crosses the sky by day and the moon by night, the sky and the water darken and
turn blue at night with a red glow at sunrise and sunset, and stars come out. Creatures
can belong to the day or the night: butterflies and birds by day, fireflies by night.
Clouds from the forest's rain dim the day, too. The moon has no atmosphere and no cycle.

`t` (or the menu) steps through *cycle, dawn, day, dusk, night*: the first runs the
clock, the others freeze the time of day. `--hour` sets the start time. The time of day
carries over when you switch scenes. The info line (`i`) shows the clock.

In the terminal styles the cycle shows in the stars, the sun and moon and the creatures
coming and going; the colors themselves stay with your terminal theme.

### Styles

| Style | Look |
|---|---|
| `full` (default) | gradient backdrops and the scene's own colors |
| `terminal` | no backdrop: your terminal's background, sprites in the 16 named ANSI colors so they follow your theme |
| `mono` | no color at all: only your terminal's own foreground and background |

Switch with `--style`, the `c` key or the menu.

## Build

    scripts/build.sh            # macOS / Linux  -> build/<preset>/src/diorascii
    scripts/build.ps1           # Windows        -> build\release\src\Release\diorascii.exe

Needs only CMake 3.21 or newer and a C++20 compiler. `scripts/build.sh dev` makes a
debug build and runs the tests. Assets are embedded into the binary at build time.
`-DDIORASCII_VERSION=x.y.z` sets the version the binary reports.
With GCC the C++ runtime is linked statically for a single-file binary when the static library
is installed (Fedora: `dnf install libstdc++-static`); without it the build falls back to the shared one.

## Extending: sprites

A `*.sprite` file is a header of `key: value` lines followed by one or more
`--- art` frames, each optionally followed by a `--- mask`. Drop it into a
directory and pass `--assets DIR` (or put it in `assets/` to ship it).

    name: darter
    facing: right            # direction the art points
    habitat: medium          # medium (water) | surface | sky | ground | river
    speed: 9..18             # cells per second
    depth: 3..16             # draw order, larger = further away
    tag: prey                # lets behaviors find it (prey, food, ...)
    behaviors: seek_food swim emit_bubbles
    pool: warm               # color pool for mask digits 1-9
    --- art
    <=={o>
    --- mask
    2111W1

Header keys:

| Key | Meaning |
|---|---|
| `name`, `facing`, `habitat`, `speed`, `depth`, `tag`, `behaviors`, `pool` | as in the example |
| `fps`, `loop: no` | animation speed; stop on the last frame |
| `die-after-animation: yes` | remove the sprite when its animation has played once |
| `color: #rrggbb` | default color (used for blank mask cells) |
| `random-frame: yes` | start on a random frame (for particles) |
| `ttl: 2..4` | lifetime in seconds |
| `slope` | diagonal flight: vertical speed relative to horizontal |
| `anchor-row` | which sprite row sits on the surface line |
| `mouth-row` | row where it eats or bites |
| `bubble-rate`, `bob-amplitude`, `bob-frequency` | tuning for those behaviors |
| `lanes: 4` | ground sprites stand up to this many rows deep on the bank; the ones further forward are drawn in front |
| `altitude: 8` | sky sprites hover within this many rows above the ground |
| `active: day` or `night` | only shown by day or by night (needs a day and night cycle in the scene) |
| `glow: yes` | not dimmed at night (fireflies, sun, moon) |
| `backing: 0.5` | darkens the background behind the sprite's glyphs by this share (full style only), so light sprites stay readable on bright and dark backdrops |
| `hop-period`, `hop-height` | seconds per hop and hop height in rows, for the `hop` behavior |
| `destructible: no` | survives meteor strikes (ground sprites are destructible by default) |
| `solid: yes` | the sprite may paint solid color blocks (see mask rules) |

Mask rules: `1`-`9` use the creature's randomized palette, letters are fixed
colors (`W`hite `R`ed `G`reen `B`lue `C`yan `Y`ellow `M`agenta `O`range blac`K` `T`an `N`brown `P`ink `L`ight gray;
lower case = darker), a space is the default color. A space in the art with
mask `_` is opaque, with a color letter it is a solid block of that color. A color
letter on a blank art cell is only allowed in sprites that declare `solid: yes`;
the test suite rejects it everywhere else because it is almost always a misaligned
mask.

Behaviors:

| Behavior | Effect |
|---|---|
| `swim` | horizontal travel; leaves the world at the edge |
| `fly` | travel in both axes |
| `walk` | like `swim`, but follows the ground, dipping into craters |
| `bob` | gentle vertical bobbing |
| `rise` | rises and pops at the surface (bubbles) |
| `sink` | falls, then lies on the ground for a while (food) |
| `emit_bubbles` | now and then releases a bubble |
| `seek_food` | turns towards the closest food, swims to it and eats it |
| `hunt` | eats every sprite tagged `prey` that its mouth touches |
| `ballistic` | thrown under the scene's gravity, gone when it lands (debris, dust) |
| `meteorite` | streaks down and strikes the ground where its head arrives |
| `wander` | drifts like `swim` with an organic wobble (butterflies, fireflies, birds) |
| `hop` | moves in hops: rests, then jumps forward in an arc |
| `rain` | a raindrop: falls to its landing row, makes a splash and is gone |

## Extending: scenes

A `*.scene` file describes a whole world as data; sprites are shared between
scenes. See `assets/scenes/aquarium.scene` and `moon.scene`.

    name: aquarium
    title: Aquarium
    sky: #06081c #223462            # top and bottom colors
    medium-colors: #1e7896 #051642  # surface and deep (omit with "medium: no")
    ground-colors: #967c52 #765e3c
    sky-fraction: 0.2               # share of the height above the surface (1 = all sky)
    ground-rows: 2
    stars: 23                       # one star per N sky cells
    action: drop food               # what the f key does: drop | impact | weather <sprite>, or none
    action-label: Feed the fish     # text of the menu button
    gravity: 2.5                    # cells/s^2 for falling debris
    dust-settle: 40                 # seconds for dust to fill one row of a crater
    river-rows: 3                   # rows at the bottom of the ground that are a river
    river-colors: #4aa0d8 #1f6aa5   # river colors from the bank down
    river-flow: 2.5                 # cells per second the ripples drift
    weather-duration: 18            # seconds a rain shower lasts
    day-night: yes                  # sun, moon, stars and dimming light
    day-length: 240                 # seconds for one full day
    start-hour: 10                  # time of day at the start
    sun-sprite: sun_big             # sprites that cross the sky by day and by night
    moon-sprite: moon_big
    event-interval: 12..35          # seconds between rare visitors

    --- population                  # <sprite> per-cells|per-cols|count <n>
    darter  per-cells  230
    @seaweed per-cols  15           # built-in procedural plant

    --- decor                       # <sprite> left|right|center|random <margin> [y <row>]
    castle  right  2

    --- events                      # <sprite> <weight>; one at a time
    shark   3

The scene's colors are its daytime look; the night look is derived from them. The
river takes at most half of the ground rows at the bottom, and a ground of more than a
quarter of the height is shortened, so the scene adapts to small terminals.

Scene files are checked on load: a reference to a sprite that does not exist is
reported as an error instead of being silently ignored.

## Documentation

The [project page](https://christianahrens.github.io/Diorascii/) links the latest
release, this repository and the generated
[API documentation](https://christianahrens.github.io/Diorascii/doxygen/index.html). To
build the documentation yourself, install [Doxygen](https://www.doxygen.nl) and
Graphviz and run `doxygen Doxyfile` (or `cmake --build <dir> --target docs`); the
result is in `docs/html`.

## Releasing

Pushing a tag in the form `x.y.z` (major.minor.fix, no `v` prefix) builds Diorascii on
macOS, Linux and Windows, runs the tests and publishes a GitHub release with one
archive per platform:

    git tag 1.2.3
    git push origin 1.2.3

The release is created as a draft and only published once all three platforms built
and passed their tests. The binaries report the tag as their version. Every push to
`main` also redeploys the project page and the API documentation to GitHub Pages.

The macOS asset is a disk image with the binary, a `usr-local-bin` link to drop it onto
(`/usr/bin` itself is protected by macOS) and an installer script for `~/.local/bin`. The
copy CI builds is only ad-hoc signed. To build the same image with your own Developer ID
signature and Apple's notarization, run on a Mac:

    scripts/package-macos.sh 1.2.3          # -> dist/Diorascii-1.2.3-macOS.dmg

It picks the first "Developer ID Application" identity in your keychain (or
`DIORASCII_SIGN_IDENTITY`), signs the binary with the hardened runtime and the image with
the same identity, submits the image with `notarytool` using the keychain profile
`APP_NOTARIZATION_CREDENTIALS_PROFILE` (`DIORASCII_NOTARY_PROFILE` to use another, created
once with `xcrun notarytool store-credentials`) and staples the ticket. Without an identity, or with
`DIORASCII_ADHOC=1`, it falls back to the ad-hoc signature and skips notarization, which is
what the release workflow does. `DIORASCII_SKIP_NOTARIZE=1` signs only.

## Layout

    src/core   headless simulation, frame buffer, menu (no I/O, deterministic per seed)
    src/term   VT/ANSI encoder and the terminal backends (posix, windows)
    src/app    command line + main loop
    assets/    sprites and scenes, embedded at build time (cmake/embed.cmake)
    pages/     landing page deployed to GitHub Pages
    tests/     dependency-free tests (ctest)

The core only produces a grid of styled cells and consumes abstract actions, so
other frontends (SDL window, tvOS) can be added without touching it.

## Status

Three scenes (aquarium, forest, moon) with original sprites; feeding, predation, rain,
meteor strikes with craters, a day and night cycle, scheduled events, three render styles
and an ASCII menu work.
Planned: more scenes (mountains, desert) with their own actions, and windowed and tvOS
frontends.

## License

[MIT](LICENSE)

## Credits and legal

Diorascii is inspired by [asciiquarium](http://robobunny.com/projects/asciiquarium) by
Kirk Baucom, the classic Perl aquarium for the terminal, whose ASCII art is largely by
Joan Stark. Thanks to them, and to everyone who ported it, for a delightful idea.

Diorascii is an **independent, legally clean rewrite**, not a port or a fork in
substance:

- no source code from asciiquarium or any of its ports is included, and the C++
  code, file formats and engine were written from scratch;
- no ASCII art from asciiquarium or any of its ports is included; every sprite in
  this repository was drawn for Diorascii;
- only the general idea of an animated ASCII aquarium was taken as inspiration, and
  ideas are not covered by copyright.

asciiquarium is licensed under the GPL. Diorascii deliberately is not derived from it
and is released under the permissive [MIT license](LICENSE), so it can be used,
redistributed and shipped (including through app stores) without GPL obligations.
This is a statement of how the project is made, not legal advice.
